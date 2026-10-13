#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "spell_cast_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysSpellCast {
namespace {

enum CastEventId {
    CAST_SENT          = 590,
    CAST_START         = 591,
    CAST_SUCCESS       = 592,
    CAST_FAILED        = 593,
    CAST_INTERRUPTED   = 594,
    CAST_STOP          = 595,
    CHANNEL_START      = 596,
    CHANNEL_UPDATE     = 597,
    CHANNEL_STOP       = 598,
    CAST_DELAYED       = 599
};

enum Kind {
    KIND_NONE = 0,
    KIND_CAST = 1,
    KIND_CHANNEL = 2
};

enum State {
    STATE_NONE = 0,
    STATE_SENT = 1,
    STATE_CASTING = 2,
    STATE_CHANNELING = 3,
    STATE_SUCCESS = 4,
    STATE_FAILED = 5,
    STATE_INTERRUPTED = 6,
    STATE_STOPPED = 7
};

// R2 separates live phase from terminal result. R1's single `state` field
// necessarily became STOPPED after SUCCESS/INTERRUPTED, so a read-side query
// could not tell why the cast ended. `state` is retained for API30 compatibility;
// `result` is the durable terminal outcome used by Cast.State.*.
enum Result {
    RESULT_NONE = 0,
    RESULT_SUCCESS = 1,
    RESULT_FAILED = 2,
    RESULT_INTERRUPTED = 3,
    RESULT_STOPPED = 4,
    RESULT_EXPIRED = 5
};

constexpr unsigned MAX_RECORDS = 64;
constexpr unsigned EVENT_COUNT_EXPANDED = 700;
constexpr unsigned TARGET_FLAG_UNIT = 0x0002;
constexpr unsigned SPELL_ATTR_EX_CHANNELED = 0x00000044UL;
constexpr unsigned EXPIRE_GRACE_MS = 1500UL;
constexpr unsigned LAST_RECORD_KEEP_MS = 15000UL;
constexpr unsigned PENDING_TTL_MS = 3000UL;
constexpr unsigned CHANNEL_RESTAMP_WINDOW_MS = 1000UL;

struct CastRecord {
    bool used;
    bool active;
    bool successSeen;
    unsigned char kind;
    unsigned char state;
    unsigned char result;
    unsigned char reserved;
    unsigned long long casterGuid;
    unsigned long long targetGuid;
    unsigned long spellId;
    unsigned long startMs;
    unsigned long endMs;
    unsigned long delayMs;
    unsigned long lastEventMs;
    unsigned long sequence;
    unsigned long failureCode;
    unsigned long completedMs;
    unsigned long lastEventId;
    unsigned long worldGeneration;
};

struct PendingCast {
    bool valid;
    unsigned char reserved[3];
    unsigned long spellId;
    unsigned long long targetGuid;
    unsigned long tick;
    unsigned long sequence;
};

#pragma pack(push,1)
struct SpellDbView {
    unsigned char* records;
    unsigned long numRecords;
    unsigned char** recordsById;
    unsigned long maxId;
    int loaded;
};
#pragma pack(pop)

using SignalEventParamFn = int (__cdecl *)(int eventCode, char* format, ...);
using SStrDupAFn = char* (__stdcall *)(char* source, char* source2, int tag);
using ResolveUnitFn = void* (__fastcall *)(const char* token);
using GetActivePlayerFn = unsigned long long (__stdcall *)();
using GetSpellDurationFn = int (__fastcall *)(const unsigned char* spellRecord, int unit, char skipMod);
using GetTimeMsFn = unsigned long long (__stdcall *)();
using ClearCastingSpellFn = void (__fastcall *)(void* unit, void* edx, int spellId, char notify, char cleanup);

static CastRecord g_records[MAX_RECORDS] = {};
static PendingCast g_pending = {};
static volatile LONG g_initOnce = 0;
static volatile LONG g_inSub = 0;
static volatile LONG g_outSub = 0;
static volatile LONG g_tickSub = 0;
static volatile LONG g_clearHook = 0;
static volatile LONG g_eventsReady = 0;
// CAST1-FIX1: event IDs 590..599 are logical identifiers only. SuperWoW
// owns those fixed high slots (UNIT_HEALTH_GUID..KEY_UP), so actual FrameScript
// slots are claimed dynamically from existing NULL entries in the event table.
static int g_eventSlots[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static volatile LONG g_parseFailure = 0;
static volatile LONG g_sentCount = 0;
static volatile LONG g_startCount = 0;
static volatile LONG g_successCount = 0;
static volatile LONG g_failedCount = 0;
static volatile LONG g_interruptCount = 0;
static volatile LONG g_stopCount = 0;
static volatile LONG g_channelStartCount = 0;
static volatile LONG g_channelUpdateCount = 0;
static volatile LONG g_channelStopCount = 0;
static volatile LONG g_delayedCount = 0;
static volatile LONG g_clearCastingCalls = 0;
static volatile LONG g_expiredCount = 0;
static volatile LONG g_castResultAccepted = 0;
static volatile LONG g_castResultRejected = 0;
static volatile LONG g_channelRestamps = 0;
static volatile LONG g_pendingReplaced = 0;
// CAST1-R1-DIAG2: packet-level evidence counters. These are observational
// only; they do not alter cast truth or event emission.
static volatile LONG g_spellStartPackets = 0;
static volatile LONG g_spellStartParsed = 0;
static volatile LONG g_spellStartZeroTime = 0;
static volatile LONG g_spellGoPackets = 0;
static volatile LONG g_spellFailurePackets = 0;
static unsigned long long g_lastStartCaster = 0;
static unsigned long long g_lastStartTarget = 0;
static unsigned long g_lastStartSpell = 0;
static unsigned long g_lastStartCastTime = 0;
static unsigned long g_lastStartMask = 0;
static unsigned long g_sequence = 0;
static unsigned long g_worldGeneration = 1;
static char g_status[128] = "NOT_INITIALIZED";
static ClearCastingSpellFn g_nextClearCasting = 0;
static bool g_createdClearHook = false;
static unsigned long g_clearHookTarget = 0;

static void zeroBytes(void* p, unsigned long n) {
    if (!p) return;
    unsigned char* b = (unsigned char*)p;
    for (unsigned long i = 0; i < n; ++i) b[i] = 0;
}

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i = 0;
    if (s) for (; s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
}

static bool sameText(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;;) {
        if (*a != *b) return false;
        if (!*a) return true;
        ++a; ++b;
    }
}

static bool readable(const void* ptr, unsigned long size) {
    if (!ptr || !size) return false;
    MEMORY_BASIC_INFORMATION m = {};
    if (VirtualQuery(ptr, &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    bool r = p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
             p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
             p == PAGE_EXECUTE_WRITECOPY;
    if (!r) return false;
    unsigned long a = (unsigned long)ptr;
    unsigned long end = (unsigned long)m.BaseAddress + (unsigned long)m.RegionSize;
    return a + size >= a && a + size <= end;
}

static bool executable(unsigned long address) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!address || VirtualQuery((void*)address, &m, sizeof(m)) != sizeof(m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

// If another DLL already placed a standard entry JMP on ClearCastingSpell,
// hook the current chain destination instead of blindly replacing that JMP.
// This is coexistence-by-hook-chain only; CAST1 has no provider/backend mode.
static unsigned long chainedTarget(unsigned long entry) {
    unsigned long current = entry;
    for (unsigned depth = 0; depth < 4; ++depth) {
        if (!executable(current) || !readable((void*)current, 6)) return 0;
        unsigned char* p = (unsigned char*)current;
        unsigned long next = 0;
        if (p[0] == 0xE9) {
            long rel = *(long*)(p + 1);
            next = current + 5UL + (unsigned long)rel;
        } else if (p[0] == 0xFF && p[1] == 0x25) {
            unsigned long slot = *(unsigned long*)(p + 2);
            if (!readable((void*)slot, 4)) break;
            next = *(unsigned long*)slot;
        } else {
            break;
        }
        if (!next || next == current || !executable(next)) break;
        current = next;
    }
    return executable(current) ? current : 0;
}

static unsigned long long playerGuid() {
    if (!executable(WoW112::GET_ACTIVE_PLAYER_GUID)) return 0;
    return ((GetActivePlayerFn)WoW112::GET_ACTIVE_PLAYER_GUID)();
}

static unsigned long long unitGuid(void* unit) {
    if (!unit || !readable((unsigned char*)unit + WoW112::OFF_CGOBJECT_GUID, 8)) return 0;
    return *(unsigned long long*)((unsigned char*)unit + WoW112::OFF_CGOBJECT_GUID);
}

static unsigned long long resolveGuid(const char* token) {
    if (!token || !executable(WoW112::RESOLVE_UNIT_TOKEN)) return 0;
    void* u = ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)(token);
    return unitGuid(u);
}

static const unsigned char* spellRecord(unsigned long spellId) {
    if (!spellId || !readable((void*)WoW112::SPELL_DB, sizeof(SpellDbView))) return 0;
    SpellDbView* db = (SpellDbView*)WoW112::SPELL_DB;
    if (!db->recordsById || spellId > db->maxId ||
        !readable(db->recordsById + spellId, 4)) return 0;
    unsigned char* r = db->recordsById[spellId];
    return r && readable(r, WoW112::OFF_SPELL_NAMES + 36UL) ? r : 0;
}

static bool isChannelSpell(unsigned long spellId) {
    const unsigned char* r = spellRecord(spellId);
    return r && readable(r + WoW112::OFF_SPELL_RECORD_ATTRIBUTES_EX, 4) &&
           ((*(unsigned long*)(r + WoW112::OFF_SPELL_RECORD_ATTRIBUTES_EX) &
             SPELL_ATTR_EX_CHANNELED) != 0);
}

static unsigned long channelDuration(unsigned long spellId, bool local) {
    const unsigned char* r = spellRecord(spellId);
    if (!r || !executable(WoW112::GET_SPELL_DURATION)) return 0;
    int ms = ((GetSpellDurationFn)WoW112::GET_SPELL_DURATION)(r, 0, local ? 0 : 1);
    return ms > 0 ? (unsigned long)ms : 0;
}

static const char* spellName(unsigned long spellId) {
    const unsigned char* r = spellRecord(spellId);
    if (!r || !readable((void*)WoW112::LOCALE_INDEX, 4)) return "";
    int loc = *(int*)WoW112::LOCALE_INDEX;
    if (loc < 0 || loc > 8) loc = 0;
    const char* s = *(const char**)(r + WoW112::OFF_SPELL_NAMES + (unsigned long)loc * 4UL);
    return s && readable(s, 1) ? s : "";
}

static char hexDigit(unsigned v) {
    return (char)(v < 10 ? ('0' + v) : ('A' + v - 10));
}

static void guidText(unsigned long long g, char out[19]) {
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        unsigned sh = (unsigned)(15 - i) * 4U;
        out[2 + i] = hexDigit((unsigned)((g >> sh) & 15ULL));
    }
    out[18] = 0;
}

static const char* kindText(unsigned char k) {
    return k == KIND_CHANNEL ? "CHANNEL" : (k == KIND_CAST ? "CAST" : "NONE");
}

static const char* stateText(unsigned char s) {
    switch (s) {
        case STATE_SENT: return "SENT";
        case STATE_CASTING: return "CASTING";
        case STATE_CHANNELING: return "CHANNELING";
        case STATE_SUCCESS: return "SUCCESS";
        case STATE_FAILED: return "FAILED";
        case STATE_INTERRUPTED: return "INTERRUPTED";
        case STATE_STOPPED: return "STOPPED";
        default: return "NONE";
    }
}

static const char* resultText(unsigned char r) {
    switch (r) {
        case RESULT_SUCCESS: return "SUCCESS";
        case RESULT_FAILED: return "FAILED";
        case RESULT_INTERRUPTED: return "INTERRUPTED";
        case RESULT_STOPPED: return "STOPPED";
        case RESULT_EXPIRED: return "EXPIRED";
        default: return "NONE";
    }
}

static const char* phaseText(const CastRecord& r) {
    if (!r.active) return "IDLE";
    return r.kind == KIND_CHANNEL ? "CHANNELING" : "CASTING";
}

static unsigned long clientNowMs() {
    if (executable(WoW112::OS_GET_ASYNC_TIME_MS)) {
        unsigned long long now = ((GetTimeMsFn)WoW112::OS_GET_ASYNC_TIME_MS)();
        return (unsigned long)(now & 0xffffffffULL);
    }
    return GetTickCount();
}

static bool parseGuid(const char* s, unsigned long long* out) {
    if (!s || !out) return false;
    while (*s == ' ' || *s == '\t') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    unsigned long long v = 0;
    unsigned n = 0;
    while (*s) {
        char c = *s++;
        unsigned d = 0;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else if (c == ' ' || c == '\t') break;
        else return false;
        if (n >= 16) return false;
        v = (v << 4) | d;
        ++n;
    }
    if (!n) return false;
    *out = v;
    return true;
}

static const char* eventText(int id) {
    switch (id) {
        case CAST_SENT: return "TYS_CAST_SENT";
        case CAST_START: return "TYS_CAST_START";
        case CAST_SUCCESS: return "TYS_CAST_SUCCESS";
        case CAST_FAILED: return "TYS_CAST_FAILED";
        case CAST_INTERRUPTED: return "TYS_CAST_INTERRUPTED";
        case CAST_STOP: return "TYS_CAST_STOP";
        case CHANNEL_START: return "TYS_CHANNEL_START";
        case CHANNEL_UPDATE: return "TYS_CHANNEL_UPDATE";
        case CHANNEL_STOP: return "TYS_CHANNEL_STOP";
        case CAST_DELAYED: return "TYS_CAST_DELAYED";
        default: return "";
    }
}

static CastRecord* findRecord(unsigned long long caster) {
    for (unsigned i = 0; i < MAX_RECORDS; ++i)
        if (g_records[i].used && g_records[i].casterGuid == caster) return &g_records[i];
    return 0;
}

static CastRecord* acquireRecord(unsigned long long caster) {
    CastRecord* r = findRecord(caster);
    if (r) return r;
    for (unsigned i = 0; i < MAX_RECORDS; ++i) {
        if (!g_records[i].used) {
            g_records[i].used = true;
            g_records[i].casterGuid = caster;
            return &g_records[i];
        }
    }
    unsigned oldest = 0;
    for (unsigned i = 1; i < MAX_RECORDS; ++i)
        if ((long)(g_records[i].lastEventMs - g_records[oldest].lastEventMs) < 0) oldest = i;
    zeroBytes(&g_records[oldest], sizeof(g_records[oldest]));
    g_records[oldest].used = true;
    g_records[oldest].casterGuid = caster;
    return &g_records[oldest];
}

static int eventIndex(int logicalId) {
    return logicalId >= CAST_SENT && logicalId <= CAST_DELAYED
        ? logicalId - CAST_SENT : -1;
}

static int findEventSlot(char** data, const char* name) {
    if (!data || !name) return -1;
    for (int i = 0; i < (int)EVENT_COUNT_EXPANDED; ++i) {
        char* existing = data[i * 4]; // FrameScript entry stride = 16 bytes.
        if (existing && readable(existing, 1) && sameText(existing, name)) return i;
    }
    return -1;
}

static int claimEventSlot(char** data, const char* name) {
    if (!data || !name || !executable(WoW112::SSTR_DUP_A)) return -1;
    int found = findEventSlot(data, name);
    if (found >= 0) return found;

    // Claim from the LOW end. other 1.12 DLLs use the contested high fixed
    // range; the stock 1.12 table has many NULL gaps below it. Never overwrite
    // an occupied name. This is intentionally the same conflict-avoidance
    // strategy proven by ClassicAPI, implemented independently here.
    for (int i = 0; i < (int)EVENT_COUNT_EXPANDED; ++i) {
        if (data[i * 4] != 0) continue;
        char temp[48] = {};
        copyText(temp, sizeof(temp), name);
        char* stored = ((SStrDupAFn)WoW112::SSTR_DUP_A)(temp, temp, 1308);
        if (!stored) return -1;
        data[i * 4] = stored;
        return i;
    }
    return -1;
}

static int slotForEvent(int logicalId) {
    const int idx = eventIndex(logicalId);
    return idx >= 0 ? g_eventSlots[idx] : -1;
}

static bool ensureEvents() {
    if (InterlockedCompareExchange(&g_eventsReady, 0, 0) != 0) return true;
    if (!readable((void*)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA, 4)) return false;
    char** data = *(char***)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA;
    if (!data || !readable(data, sizeof(char*) * EVENT_COUNT_EXPANDED * 4UL)) return false;
    return installEventNames(data);
}

static void emit(int eventId, const CastRecord& r, unsigned long reason) {
    if (!ensureEvents() || !executable(WoW112::SIGNAL_EVENT_PARAM)) return;
    const int slot = slotForEvent(eventId);
    if (slot < 0) return;
    char cg[19] = {}, tg[19] = {};
    guidText(r.casterGuid, cg);
    guidText(r.targetGuid, tg);
    static char fmt[] = "%s%s%d%d%d%d%d%d";
    ((SignalEventParamFn)WoW112::SIGNAL_EVENT_PARAM)(
        slot, fmt, cg, tg, r.spellId, (unsigned long)r.kind,
        r.startMs, r.endMs,
        (r.endMs >= r.startMs ? r.endMs - r.startMs : 0UL), reason);
}

static unsigned long nextSequenceFor(unsigned long long caster, unsigned long spellId) {
    if (caster == playerGuid() && g_pending.valid && g_pending.spellId == spellId)
        return g_pending.sequence;
    return ++g_sequence;
}

static void startRecord(unsigned long long caster, unsigned long long target,
                        unsigned long spellId, unsigned char kind,
                        unsigned long start, unsigned long end) {
    if (!caster || !spellId) return;
    CastRecord* r = acquireRecord(caster);
    unsigned long seq = nextSequenceFor(caster, spellId);
    zeroBytes(r, sizeof(*r));
    r->used = true;
    r->active = true;
    r->kind = kind;
    r->state = kind == KIND_CHANNEL ? STATE_CHANNELING : STATE_CASTING;
    r->result = RESULT_NONE;
    r->casterGuid = caster;
    r->targetGuid = target;
    r->spellId = spellId;
    r->startMs = start;
    r->endMs = end;
    r->lastEventMs = start;
    r->sequence = seq;
    r->worldGeneration = g_worldGeneration;
    r->lastEventId = kind == KIND_CHANNEL ? CHANNEL_START : CAST_START;
    if (kind == KIND_CHANNEL) {
        ++g_channelStartCount;
        emit(CHANNEL_START, *r, 0);
    } else {
        ++g_startCount;
        emit(CAST_START, *r, 0);
    }
}

static void stopRecord(CastRecord* r, bool channel, unsigned char fallbackResult = RESULT_STOPPED) {
    if (!r || !r->used || !r->active) return;
    const unsigned long now = clientNowMs();
    r->active = false;
    r->state = STATE_STOPPED; // API30 legacy field
    if (r->result == RESULT_NONE) r->result = fallbackResult;
    r->completedMs = now;
    r->lastEventMs = now;
    r->lastEventId = channel ? CHANNEL_STOP : CAST_STOP;
    if (channel) {
        ++g_channelStopCount;
        emit(CHANNEL_STOP, *r, 0);
    } else {
        ++g_stopCount;
        emit(CAST_STOP, *r, 0);
    }
}

static void succeed(unsigned long long caster, unsigned long spellId,
                    unsigned long long target) {
    if (!caster || !spellId) return;
    const unsigned long now = clientNowMs();
    CastRecord* r = findRecord(caster);
    if (!r || !r->active || r->spellId != spellId) {
        r = acquireRecord(caster);
        unsigned long seq = nextSequenceFor(caster, spellId);
        zeroBytes(r, sizeof(*r));
        r->used = true;
        r->casterGuid = caster;
        r->targetGuid = target;
        r->spellId = spellId;
        r->kind = KIND_CAST;
        r->state = STATE_SUCCESS;
        r->result = RESULT_SUCCESS;
        r->startMs = now;
        r->endMs = now;
        r->lastEventMs = now;
        r->sequence = seq;
        r->worldGeneration = g_worldGeneration;
        r->completedMs = now;
        r->lastEventId = CAST_SUCCESS;
        r->successSeen = true;
        ++g_successCount;
        emit(CAST_SUCCESS, *r, 0);
    } else {
        r->successSeen = true;
        r->state = STATE_SUCCESS;
        r->result = RESULT_SUCCESS;
        r->lastEventMs = now;
        r->lastEventId = CAST_SUCCESS;
        if (!r->targetGuid && target) r->targetGuid = target;
        ++g_successCount;
        emit(CAST_SUCCESS, *r, 0);
        if (r->kind == KIND_CAST) {
            stopRecord(r, false);
        } else {
            // SUCCEEDED is an event in the channel lifecycle, not the live
            // state. Keep Cast.Get reporting CHANNELING until CHANNEL_STOP.
            r->state = STATE_CHANNELING;
        }
    }

    if (caster == playerGuid() && g_pending.valid && g_pending.spellId == spellId)
        zeroBytes(&g_pending, sizeof(g_pending));
}

static void abortRecord(unsigned long long caster, unsigned long spellId,
                        unsigned long reason, bool explicitFailure) {
    if (!caster || !spellId) return;
    const unsigned long now = clientNowMs();
    CastRecord* r = findRecord(caster);
    if (r && r->active && r->spellId == spellId) {
        r->failureCode = reason;
        r->lastEventMs = now;
        // Modern channel lifecycle has CHANNEL_STOP but no separate
        // UNIT_SPELLCAST_INTERRUPTED equivalent for an active channel.
        if (r->kind == KIND_CHANNEL) {
            r->result = RESULT_INTERRUPTED;
            stopRecord(r, true);
        } else {
            r->state = STATE_INTERRUPTED;
            r->result = RESULT_INTERRUPTED;
            r->lastEventId = CAST_INTERRUPTED;
            ++g_interruptCount;
            emit(CAST_INTERRUPTED, *r, reason);
            stopRecord(r, false);
        }
    } else if (explicitFailure) {
        r = acquireRecord(caster);
        unsigned long seq = nextSequenceFor(caster, spellId);
        zeroBytes(r, sizeof(*r));
        r->used = true;
        r->casterGuid = caster;
        r->targetGuid = (caster == playerGuid() && g_pending.valid &&
                         g_pending.spellId == spellId) ? g_pending.targetGuid : 0;
        r->spellId = spellId;
        r->kind = KIND_CAST;
        r->state = STATE_FAILED;
        r->result = RESULT_FAILED;
        r->startMs = now;
        r->endMs = now;
        r->lastEventMs = now;
        r->failureCode = reason;
        r->sequence = seq;
        r->worldGeneration = g_worldGeneration;
        r->completedMs = now;
        r->lastEventId = CAST_FAILED;
        ++g_failedCount;
        emit(CAST_FAILED, *r, reason);
    }
    if (caster == playerGuid() && g_pending.valid && g_pending.spellId == spellId)
        zeroBytes(&g_pending, sizeof(g_pending));
}

static void parseSpellStart(TysNativeBus::CDataStoreView* p) {
    ++g_spellStartPackets;
    unsigned long long item = 0, caster = 0, target = 0;
    unsigned long spell = 0, castTime = 0;
    unsigned short flags = 0, mask = 0;
    if (!TysNativeBus::readPackedGuid(p, &item) ||
        !TysNativeBus::readPackedGuid(p, &caster) ||
        !TysNativeBus::read(p, &spell) ||
        !TysNativeBus::read(p, &flags) ||
        !TysNativeBus::read(p, &castTime) ||
        !TysNativeBus::read(p, &mask)) {
        ++g_parseFailure;
        return;
    }
    if ((mask & TARGET_FLAG_UNIT) && !TysNativeBus::readPackedGuid(p, &target)) {
        ++g_parseFailure;
        return;
    }
    if (!caster || !spell) return;
    ++g_spellStartParsed;
    g_lastStartCaster = caster;
    g_lastStartTarget = target;
    g_lastStartSpell = spell;
    g_lastStartCastTime = castTime;
    g_lastStartMask = mask;

    const bool ch = isChannelSpell(spell) && castTime == 0;
    // Pure instant spells do not have a cast lifecycle/bar. Their completion
    // is represented by SMSG_SPELL_GO -> TYS_CAST_SUCCESS only.
    if (!ch && castTime == 0) { ++g_spellStartZeroTime; return; }
    const unsigned long now = clientNowMs();
    unsigned long dur = castTime;
    if (ch) {
        dur = channelDuration(spell, caster == playerGuid());
        if (!dur) return;
    }
    startRecord(caster, target, spell, ch ? KIND_CHANNEL : KIND_CAST,
                now, now + dur);
}

static void parseSpellGo(TysNativeBus::CDataStoreView* p) {
    ++g_spellGoPackets;
    unsigned long long item = 0, caster = 0, target = 0;
    unsigned long spell = 0;
    short flags = 0;
    unsigned char hits = 0;
    if (!TysNativeBus::readPackedGuid(p, &item) ||
        !TysNativeBus::readPackedGuid(p, &caster) ||
        !TysNativeBus::read(p, &spell) ||
        !TysNativeBus::read(p, &flags) ||
        !TysNativeBus::read(p, &hits)) {
        ++g_parseFailure;
        return;
    }
    if (hits > 0) {
        unsigned long long t = 0;
        if (TysNativeBus::read(p, &t)) target = t;
    }
    if (!target && caster == playerGuid() && g_pending.valid &&
        g_pending.spellId == spell) target = g_pending.targetGuid;
    succeed(caster, spell, target);
}

static void parseSpellFailure(TysNativeBus::CDataStoreView* p) {
    ++g_spellFailurePackets;
    unsigned long long guid = 0;
    unsigned long spell = 0;
    if (!TysNativeBus::read(p, &guid) || !TysNativeBus::read(p, &spell)) {
        ++g_parseFailure;
        return;
    }
    // Both remote-abort packets share guid + spellId. SMSG_SPELL_FAILED_OTHER
    // ends exactly here; reading an optional reason byte from it advances the
    // shared cursor past write and falsely trips NativeBus cursorOverruns.
    // Remote lifecycle does not need the reason, so stop at the common prefix.
    abortRecord(guid, spell, 0, false);
}

// Vanilla 1.12 SMSG_CAST_RESULT: spellId(u32), status(u8).
// status 0 = accepted. status 2 = failed and is followed by SpellCastResult(u8)
// plus optional reason-specific payload. Do not mistake the status byte for a
// failure reason: successful casts receive status 0 on normal servers.
static void parseCastResult(TysNativeBus::CDataStoreView* p) {
    unsigned long spell = 0;
    unsigned char status = 0;
    if (!TysNativeBus::read(p, &spell) || !TysNativeBus::read(p, &status)) {
        ++g_parseFailure;
        return;
    }
    if (!spell) return;
    if (status == 0) {
        ++g_castResultAccepted;
        return;
    }
    unsigned char reason = 0;
    if (!TysNativeBus::read(p, &reason)) {
        ++g_parseFailure;
        return;
    }
    ++g_castResultRejected;
    unsigned long long pg = playerGuid();
    if (pg) abortRecord(pg, spell, reason, true);
}

static void parseDelayed(TysNativeBus::CDataStoreView* p) {
    unsigned long long guid = 0;
    unsigned long delay = 0;
    if (!TysNativeBus::read(p, &guid) || !TysNativeBus::read(p, &delay)) {
        ++g_parseFailure;
        return;
    }
    CastRecord* r = findRecord(guid);
    if (!r || !r->active || r->kind != KIND_CAST || !delay) return;
    r->endMs += delay;
    r->delayMs += delay;
    r->lastEventMs = clientNowMs();
    r->lastEventId = CAST_DELAYED;
    ++g_delayedCount;
    emit(CAST_DELAYED, *r, delay);
}

static void parseChannelStart(TysNativeBus::CDataStoreView* p) {
    unsigned long spell = 0, dur = 0;
    if (!TysNativeBus::read(p, &spell) || !TysNativeBus::read(p, &dur)) {
        ++g_parseFailure;
        return;
    }
    unsigned long long pg = playerGuid();
    if (!pg || !spell || !dur) return;
    const unsigned long now = clientNowMs();
    CastRecord* existing = findRecord(pg);
    if (existing && existing->active && existing->kind == KIND_CHANNEL &&
        existing->spellId == spell &&
        (unsigned long)(now - existing->startMs) <= CHANNEL_RESTAMP_WINDOW_MS) {
        existing->endMs = now + dur;
        existing->lastEventMs = now;
        existing->state = STATE_CHANNELING;
        if (!existing->targetGuid && g_pending.valid && g_pending.spellId == spell)
            existing->targetGuid = g_pending.targetGuid;
        ++g_channelRestamps;
        return;
    }
    unsigned long long target =
        (g_pending.valid && g_pending.spellId == spell) ? g_pending.targetGuid : 0;
    startRecord(pg, target, spell, KIND_CHANNEL, now, now + dur);
}

static void parseChannelUpdate(TysNativeBus::CDataStoreView* p) {
    unsigned long remaining = 0;
    if (!TysNativeBus::read(p, &remaining)) {
        ++g_parseFailure;
        return;
    }
    CastRecord* r = findRecord(playerGuid());
    if (!r || !r->active || r->kind != KIND_CHANNEL) return;
    if (!remaining) {
        stopRecord(r, true);
        return;
    }
    const unsigned long now = clientNowMs();
    r->endMs = now + remaining;
    r->lastEventMs = now;
    r->lastEventId = CHANNEL_UPDATE;
    ++g_channelUpdateCount;
    emit(CHANNEL_UPDATE, *r, remaining);
}

static void onIncoming(unsigned long opcode, TysNativeBus::CDataStoreView* p) {
    if (!p) return;
    switch (opcode) {
        case WoW112::SMSG_CAST_RESULT_OPCODE: parseCastResult(p); break;
        case WoW112::SMSG_SPELL_START_OPCODE: parseSpellStart(p); break;
        case WoW112::SMSG_SPELL_GO_OPCODE: parseSpellGo(p); break;
        case WoW112::SMSG_SPELL_FAILURE_OPCODE: parseSpellFailure(p); break;
        case WoW112::SMSG_SPELL_FAILED_OTHER_OPCODE: parseSpellFailure(p); break;
        case WoW112::SMSG_SPELL_DELAYED_OPCODE: parseDelayed(p); break;
        case WoW112::MSG_CHANNEL_START_OPCODE: parseChannelStart(p); break;
        case WoW112::MSG_CHANNEL_UPDATE_OPCODE: parseChannelUpdate(p); break;
        default: break;
    }
}

static void onOutgoing(unsigned long opcode, TysNativeBus::CDataStoreView* p) {
    if (opcode != WoW112::CMSG_CAST_SPELL_OPCODE || !p) return;
    unsigned long spell = 0;
    unsigned short mask = 0;
    unsigned long long target = 0;
    if (!TysNativeBus::read(p, &spell) || !spell) return;
    if (TysNativeBus::read(p, &mask) && (mask & TARGET_FLAG_UNIT))
        TysNativeBus::readPackedGuid(p, &target);

    if (g_pending.valid) ++g_pendingReplaced;
    g_pending.valid = true;
    g_pending.spellId = spell;
    g_pending.targetGuid = target;
    g_pending.tick = clientNowMs();
    g_pending.sequence = ++g_sequence;

    const unsigned long long pg = playerGuid();
    if (!pg) return;
    CastRecord sent = {};
    sent.used = true;
    sent.casterGuid = pg;
    sent.targetGuid = target;
    sent.spellId = spell;
    sent.kind = KIND_CAST;
    sent.state = STATE_SENT;
    sent.result = RESULT_NONE;
    sent.startMs = g_pending.tick;
    sent.endMs = g_pending.tick;
    sent.lastEventMs = g_pending.tick;
    sent.sequence = g_pending.sequence;
    sent.worldGeneration = g_worldGeneration;
    sent.lastEventId = CAST_SENT;
    ++g_sentCount;
    emit(CAST_SENT, sent, 0);
}

// WorldTick only expires the 64 records owned by CAST1. It never scans the
// game's ObjectManager or unit list; network/engine transitions remain the
// authoritative event sources.
static void onTick() {
    const unsigned long now = clientNowMs();
    for (unsigned i = 0; i < MAX_RECORDS; ++i) {
        CastRecord* r = &g_records[i];
        if (!r->used) continue;
        if (r->active && r->endMs &&
            (long)(now - r->endMs) >= (long)EXPIRE_GRACE_MS) {
            ++g_expiredCount;
            stopRecord(r, r->kind == KIND_CHANNEL, RESULT_EXPIRED);
        } else if (!r->active && r->lastEventMs &&
                   (long)(now - r->lastEventMs) > (long)LAST_RECORD_KEEP_MS) {
            zeroBytes(r, sizeof(*r));
        }
    }
    if (g_pending.valid && (long)(now - g_pending.tick) > (long)PENDING_TTL_MS)
        zeroBytes(&g_pending, sizeof(g_pending));
}

static void __fastcall clearCastingHook(void* unit, void* edx, int spellId,
                                        char notify, char cleanup) {
    ++g_clearCastingCalls;
    // Mirror the engine gate before interpreting this call as a real stop.
    // This is important because SuperWoW/Turtle routes remote interrupts here,
    // but unrelated/late calls should not evict a newer cast.
    bool realStop = false;
    if (unit && spellId != 0 &&
        readable((unsigned char*)unit + WoW112::OFF_UNIT_CAST_SPELL, 4)) {
        int current = *(int*)((unsigned char*)unit + WoW112::OFF_UNIT_CAST_SPELL);
        realStop = current != 0 && current == spellId;
    }
    if (realStop) {
        unsigned long long guid = unitGuid(unit);
        CastRecord* r = guid ? findRecord(guid) : 0;
        if (r && r->active && r->spellId == (unsigned long)spellId) {
            if (r->kind == KIND_CHANNEL) {
                if (!r->successSeen) r->result = RESULT_INTERRUPTED;
                stopRecord(r, true);
            } else if (r->successSeen)
                stopRecord(r, false);
            else
                abortRecord(guid, (unsigned long)spellId, 0, false);
        }
    }
    if (g_nextClearCasting) g_nextClearCasting(unit, edx, spellId, notify, cleanup);
}

static void setNumber(Lua50::State L, const char* k, double v) {
    Lua50::PushString(L, k); Lua50::PushNumber(L, v); Lua50::SetTable(L, -3);
}
static void setBool(Lua50::State L, const char* k, bool v) {
    Lua50::PushString(L, k); Lua50::PushBool(L, v); Lua50::SetTable(L, -3);
}
static void setString(Lua50::State L, const char* k, const char* v) {
    Lua50::PushString(L, k); Lua50::PushString(L, v ? v : ""); Lua50::SetTable(L, -3);
}

static void pushRecord(Lua50::State L, const CastRecord& r) {
    Lua50::NewTable(L);
    char cg[19] = {}, tg[19] = {};
    guidText(r.casterGuid, cg); guidText(r.targetGuid, tg);
    setBool(L, "known", r.used);
    setBool(L, "active", r.active);
    setString(L, "casterGuid", cg);
    setString(L, "targetGuid", tg);
    setNumber(L, "spellId", r.spellId);
    setString(L, "spellName", spellName(r.spellId));
    setString(L, "kind", kindText(r.kind));
    setString(L, "state", stateText(r.state)); // API30 legacy
    setString(L, "phase", phaseText(r));
    setString(L, "result", resultText(r.result));
    setBool(L, "casting", r.active && r.kind == KIND_CAST);
    setBool(L, "channeling", r.active && r.kind == KIND_CHANNEL);
    setBool(L, "finished", r.used && !r.active && r.result != RESULT_NONE);
    setNumber(L, "startMs", r.startMs);
    setNumber(L, "endMs", r.endMs);
    setNumber(L, "durationMs", r.endMs >= r.startMs ? r.endMs - r.startMs : 0);
    const unsigned long now = clientNowMs();
    setNumber(L, "remainingMs",
              r.active && r.endMs && (long)(r.endMs - now) > 0 ? r.endMs - now : 0);
    setNumber(L, "delayMs", r.delayMs);
    setNumber(L, "failureCode", r.failureCode);
    setNumber(L, "sequence", r.sequence);
    setNumber(L, "completedMs", r.completedMs);
    setNumber(L, "lastTransitionMs", r.lastEventMs);
    setString(L, "lastEvent", eventText((int)r.lastEventId));
    setNumber(L, "worldGeneration", r.worldGeneration);
    setBool(L, "successSeen", r.successSeen);
}

} // namespace

bool installEventNames(char** data) {
    if (!data) return false;
    for (int id = CAST_SENT; id <= CAST_DELAYED; ++id) {
        const int idx = eventIndex(id);
        const int slot = claimEventSlot(data, eventText(id));
        if (idx < 0 || slot < 0) {
            InterlockedExchange(&g_eventsReady, 0);
            return false;
        }
        g_eventSlots[idx] = slot;
    }
    InterlockedExchange(&g_eventsReady, 1);
    return true;
}

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0)
        return InterlockedCompareExchange(&g_inSub, 0, 0) != 0;

    if (!TysNativeBus::incomingHookInstalled() ||
        !TysNativeBus::outgoingHookInstalled() ||
        !TysNativeBus::worldTickHookInstalled()) {
        copyText(g_status, sizeof(g_status), "NATIVE_BUS_NOT_READY");
        return false;
    }
    if (!TysNativeBus::subscribeIncoming(&onIncoming)) {
        copyText(g_status, sizeof(g_status), "INCOMING_SUBSCRIBE_FAILED");
        return false;
    }
    InterlockedExchange(&g_inSub, 1);
    if (!TysNativeBus::subscribeOutgoing(&onOutgoing)) {
        copyText(g_status, sizeof(g_status), "OUTGOING_SUBSCRIBE_FAILED");
        return false;
    }
    InterlockedExchange(&g_outSub, 1);
    if (!TysNativeBus::subscribeWorldTick(&onTick)) {
        copyText(g_status, sizeof(g_status), "WORLDTICK_SUBSCRIBE_FAILED");
        return false;
    }
    InterlockedExchange(&g_tickSub, 1);

    g_clearHookTarget = chainedTarget(WoW112::UNIT_CLEAR_CASTING_SPELL);
    if (g_clearHookTarget) {
        MH_STATUS cr = MH_CreateHook((void*)g_clearHookTarget,
                                     (void*)&clearCastingHook,
                                     (void**)&g_nextClearCasting);
        if (cr == MH_OK) {
            g_createdClearHook = true;
            MH_STATUS en = MH_EnableHook((void*)g_clearHookTarget);
            if (en == MH_OK || en == MH_ERROR_ENABLED)
                InterlockedExchange(&g_clearHook, 1);
        }
    }

    ensureEvents();
    copyText(g_status, sizeof(g_status),
             InterlockedCompareExchange(&g_clearHook, 0, 0)
                 ? "READY_NATIVEBUS_PLUS_CLEARCAST"
                 : "READY_NATIVEBUS_PACKET_ONLY");
    return true;
}

void onWorldLeaving() {
    zeroBytes(g_records, sizeof(g_records));
    zeroBytes(&g_pending, sizeof(g_pending));
    ++g_worldGeneration;
    if (!g_worldGeneration) g_worldGeneration = 1;
}

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    ensureEvents();
    Lua50::NewTable(L);
    setString(L, "status", g_status);
    setString(L, "architecture", "NATIVEBUS_PACKET_LIFECYCLE_PLUS_CLEARCAST_CHOKEPOINT");
    setBool(L, "incomingSubscriber", InterlockedCompareExchange(&g_inSub, 0, 0) != 0);
    setBool(L, "outgoingSubscriber", InterlockedCompareExchange(&g_outSub, 0, 0) != 0);
    setBool(L, "worldTickSubscriber", InterlockedCompareExchange(&g_tickSub, 0, 0) != 0);
    setBool(L, "clearCastingHook", InterlockedCompareExchange(&g_clearHook, 0, 0) != 0);
    setBool(L, "clearCastingHookChained", g_clearHookTarget != 0 &&
            g_clearHookTarget != WoW112::UNIT_CLEAR_CASTING_SPELL);
    setNumber(L, "clearCastingHookTarget", g_clearHookTarget);
    setBool(L, "customEventsReady", InterlockedCompareExchange(&g_eventsReady, 0, 0) != 0);
    setBool(L, "dynamicEventSlots", true);
    setNumber(L, "eventSlotSent", slotForEvent(CAST_SENT));
    setNumber(L, "eventSlotStart", slotForEvent(CAST_START));
    setNumber(L, "eventSlotSuccess", slotForEvent(CAST_SUCCESS));
    setNumber(L, "eventSlotFailed", slotForEvent(CAST_FAILED));
    setNumber(L, "eventSlotInterrupted", slotForEvent(CAST_INTERRUPTED));
    setNumber(L, "eventSlotStop", slotForEvent(CAST_STOP));
    setNumber(L, "eventSlotChannelStart", slotForEvent(CHANNEL_START));
    setNumber(L, "eventSlotChannelUpdate", slotForEvent(CHANNEL_UPDATE));
    setNumber(L, "eventSlotChannelStop", slotForEvent(CHANNEL_STOP));
    setNumber(L, "eventSlotDelayed", slotForEvent(CAST_DELAYED));
    setBool(L, "perOpcodeHooks", false);
    setBool(L, "objectManagerPolling", false);
    setBool(L, "unifiedCastState", true);
    setString(L, "clock", executable(WoW112::OS_GET_ASYNC_TIME_MS) ? "CLIENT_ENGINE_MS" : "GETTICKCOUNT_FALLBACK");
    setNumber(L, "worldGeneration", g_worldGeneration);
    setNumber(L, "sent", InterlockedCompareExchange(&g_sentCount, 0, 0));
    setNumber(L, "starts", InterlockedCompareExchange(&g_startCount, 0, 0));
    setNumber(L, "success", InterlockedCompareExchange(&g_successCount, 0, 0));
    setNumber(L, "failed", InterlockedCompareExchange(&g_failedCount, 0, 0));
    setNumber(L, "interrupted", InterlockedCompareExchange(&g_interruptCount, 0, 0));
    setNumber(L, "stops", InterlockedCompareExchange(&g_stopCount, 0, 0));
    setNumber(L, "channelStarts", InterlockedCompareExchange(&g_channelStartCount, 0, 0));
    setNumber(L, "channelUpdates", InterlockedCompareExchange(&g_channelUpdateCount, 0, 0));
    setNumber(L, "channelStops", InterlockedCompareExchange(&g_channelStopCount, 0, 0));
    setNumber(L, "channelRestamps", InterlockedCompareExchange(&g_channelRestamps, 0, 0));
    setNumber(L, "delayed", InterlockedCompareExchange(&g_delayedCount, 0, 0));
    setNumber(L, "castResultAccepted", InterlockedCompareExchange(&g_castResultAccepted, 0, 0));
    setNumber(L, "castResultRejected", InterlockedCompareExchange(&g_castResultRejected, 0, 0));
    setNumber(L, "parseFailure", InterlockedCompareExchange(&g_parseFailure, 0, 0));
    setNumber(L, "clearCastingCalls", InterlockedCompareExchange(&g_clearCastingCalls, 0, 0));
    setNumber(L, "expired", InterlockedCompareExchange(&g_expiredCount, 0, 0));
    setNumber(L, "pendingReplaced", InterlockedCompareExchange(&g_pendingReplaced, 0, 0));
    setNumber(L, "spellStartPackets", InterlockedCompareExchange(&g_spellStartPackets, 0, 0));
    setNumber(L, "spellStartParsed", InterlockedCompareExchange(&g_spellStartParsed, 0, 0));
    setNumber(L, "spellStartZeroTime", InterlockedCompareExchange(&g_spellStartZeroTime, 0, 0));
    setNumber(L, "spellGoPackets", InterlockedCompareExchange(&g_spellGoPackets, 0, 0));
    setNumber(L, "spellFailurePackets", InterlockedCompareExchange(&g_spellFailurePackets, 0, 0));
    setNumber(L, "lastStartSpellId", g_lastStartSpell);
    setNumber(L, "lastStartCastTimeMs", g_lastStartCastTime);
    setNumber(L, "lastStartMask", g_lastStartMask);
    char lsc[19] = {}, lst[19] = {}, pg[19] = {}, tg[19] = {};
    guidText(g_lastStartCaster, lsc); guidText(g_lastStartTarget, lst);
    guidText(playerGuid(), pg); guidText(resolveGuid("target"), tg);
    setString(L, "lastStartCasterGuid", lsc);
    setString(L, "lastStartTargetGuid", lst);
    setString(L, "playerGuid", pg);
    setString(L, "currentTargetGuid", tg);
    return 1;
}

int dispatchGet(Lua50::State L) {
    if (!L || Lua50::GetTop(L) < 2 || !Lua50::IsString(L, 2)) {
        Lua50::PushNil(L);
        return 1;
    }
    const char* token = Lua50::ToString(L, 2);
    unsigned long long guid = resolveGuid(token);
    if (!guid) {
        Lua50::PushNil(L);
        return 1;
    }
    CastRecord* r = findRecord(guid);
    if (!r) {
        Lua50::PushNil(L);
        return 1;
    }
    pushRecord(L, *r);
    return 1;
}

static void pushPending(Lua50::State L, unsigned long long pg) {
    CastRecord r = {};
    r.used = true;
    r.active = false;
    r.kind = KIND_CAST;
    r.state = STATE_SENT;
    r.result = RESULT_NONE;
    r.casterGuid = pg;
    r.targetGuid = g_pending.targetGuid;
    r.spellId = g_pending.spellId;
    r.startMs = g_pending.tick;
    r.endMs = g_pending.tick;
    r.lastEventMs = g_pending.tick;
    r.sequence = g_pending.sequence;
    r.lastEventId = CAST_SENT;
    r.worldGeneration = g_worldGeneration;
    pushRecord(L, r);
    Lua50::PushString(L, "phase"); Lua50::PushString(L, "PENDING"); Lua50::SetTable(L, -3);
    Lua50::PushString(L, "pending"); Lua50::PushBool(L, true); Lua50::SetTable(L, -3);
}

static int pushStateForGuid(Lua50::State L, unsigned long long guid) {
    if (!guid) { Lua50::PushNil(L); return 1; }
    CastRecord* r = findRecord(guid);
    const unsigned long long pg = playerGuid();
    // Prefer a terminal record for the same sequence over the still-live
    // outgoing pending snapshot. TYS_CAST_SUCCESS is emitted synchronously
    // before g_pending is cleared, so returning PENDING here would hide the
    // success from an event handler querying Cast.State.Get("player").
    if (guid == pg && g_pending.valid &&
        (!r || (!r->active && r->sequence != g_pending.sequence))) {
        pushPending(L, pg);
        return 1;
    }
    if (!r) { Lua50::PushNil(L); return 1; }
    pushRecord(L, *r);
    Lua50::PushString(L, "pending"); Lua50::PushBool(L, false); Lua50::SetTable(L, -3);
    return 1;
}

int dispatchStateStatus(Lua50::State L) {
    if (!L) return 0;
    unsigned used = 0, active = 0, casting = 0, channeling = 0, finished = 0;
    for (unsigned i = 0; i < MAX_RECORDS; ++i) {
        const CastRecord& r = g_records[i];
        if (!r.used) continue;
        ++used;
        if (r.active) {
            ++active;
            if (r.kind == KIND_CHANNEL) ++channeling; else ++casting;
        } else if (r.result != RESULT_NONE) ++finished;
    }
    Lua50::NewTable(L);
    const bool ready = InterlockedCompareExchange(&g_inSub, 0, 0) != 0 &&
                       InterlockedCompareExchange(&g_outSub, 0, 0) != 0 &&
                       InterlockedCompareExchange(&g_tickSub, 0, 0) != 0;
    setString(L, "status", ready ? "READY_UNIFIED_CAST_STATE" : g_status);
    setString(L, "authority", "NATIVEBUS_EVENTS_PLUS_CLEARCAST_TRANSITIONS");
    setString(L, "phaseModel", "PENDING_CASTING_CHANNELING_IDLE");
    setString(L, "resultModel", "NONE_SUCCESS_FAILED_INTERRUPTED_STOPPED_EXPIRED");
    setString(L, "clock", executable(WoW112::OS_GET_ASYNC_TIME_MS) ? "CLIENT_ENGINE_MS" : "GETTICKCOUNT_FALLBACK");
    setNumber(L, "capacity", MAX_RECORDS);
    setNumber(L, "used", used);
    setNumber(L, "active", active);
    setNumber(L, "casting", casting);
    setNumber(L, "channeling", channeling);
    setNumber(L, "finishedRecent", finished);
    setNumber(L, "worldGeneration", g_worldGeneration);
    setBool(L, "pendingPlayerCast", g_pending.valid);
    setNumber(L, "pendingSpellId", g_pending.valid ? g_pending.spellId : 0);
    setNumber(L, "pendingSequence", g_pending.valid ? g_pending.sequence : 0);
    return 1;
}

int dispatchStateGet(Lua50::State L) {
    if (!L || Lua50::GetTop(L) < 2 || !Lua50::IsString(L, 2)) {
        Lua50::PushNil(L); return 1;
    }
    return pushStateForGuid(L, resolveGuid(Lua50::ToString(L, 2)));
}

int dispatchStateGetByGuid(Lua50::State L) {
    if (!L || Lua50::GetTop(L) < 2 || !Lua50::IsString(L, 2)) {
        Lua50::PushNil(L); return 1;
    }
    unsigned long long guid = 0;
    if (!parseGuid(Lua50::ToString(L, 2), &guid)) { Lua50::PushNil(L); return 1; }
    return pushStateForGuid(L, guid);
}

int dispatchStateList(Lua50::State L) {
    if (!L) return 0;
    const bool includeRecent = Lua50::GetTop(L) >= 2 && Lua50::IsString(L, 2) &&
        sameText(Lua50::ToString(L, 2), "recent");
    Lua50::NewTable(L);
    int out = 1;
    for (unsigned i = 0; i < MAX_RECORDS; ++i) {
        CastRecord& r = g_records[i];
        if (!r.used || (!includeRecent && !r.active)) continue;
        Lua50::PushNumber(L, out++);
        pushRecord(L, r);
        Lua50::SetTable(L, -3);
    }
    return 1;
}

} // namespace TysSpellCast
