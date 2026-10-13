/*
 * TaiYangShenDian AURA6-D4-R4 target-duration evidence.
 *
 * This is deliberately an evidence producer, not an Aura truth store.
 * - SendObserver captures combo points at CMSG_CAST_SPELL time.
 * - SMSG_SPELL_GO resolves one predicted applied duration.
 * - AuraSourceCore remains the sole owner of caster/timing instance state.
 * - UnitFields remains the sole authority for whether an Aura exists.
 */
#include <windows.h>
#include "aura_cast_timing.h"
#include "combo_duration_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysAuraCastTiming {
namespace {

#pragma pack(push, 1)
struct SpellDbView {
    unsigned char* records;
    unsigned long numRecords;
    unsigned char** recordsById;
    unsigned long maxId;
    int loaded;
};
#pragma pack(pop)

using ResolveUnitFn = void* (__fastcall *)(const char* token);
using GetActivePlayerFn = unsigned long long (__stdcall *)();
using GetSpellDurationFn = int (__fastcall *)(const unsigned char* spellRecord,
                                              int unit, char skipMod);

static volatile LONG g_initOnce = 0;
static volatile LONG g_subscribed = 0;
static volatile LONG g_castRequests = 0;
static volatile LONG g_comboCaptures = 0;
static volatile LONG g_comboConsumes = 0;
static volatile LONG g_localDurations = 0;
static volatile LONG g_localComboDurations = 0;
static volatile LONG g_remoteBaseDurations = 0;
static volatile LONG g_durationMisses = 0;
static char g_status[96] = "NOT_INITIALIZED";

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i = 0;
    if (s) for (; s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
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

static unsigned long long activePlayerGuid() {
    if (!executable(WoW112::GET_ACTIVE_PLAYER_GUID)) return 0;
    return ((GetActivePlayerFn)WoW112::GET_ACTIVE_PLAYER_GUID)();
}

static const unsigned char* spellRecord(unsigned long spellId) {
    if (!spellId || !readable((void*)WoW112::SPELL_DB, sizeof(SpellDbView))) return 0;
    SpellDbView* db = (SpellDbView*)WoW112::SPELL_DB;
    if (!db->recordsById || spellId > db->maxId ||
        !readable(db->recordsById + spellId, 4)) return 0;
    unsigned char* rec = db->recordsById[spellId];
    return rec && readable(rec, WoW112::OFF_SPELL_RECORD_FAMILY_FLAGS + 8UL) ? rec : 0;
}

static const unsigned char* durationRow(unsigned long index) {
    if (!index || !readable((void*)WoW112::SPELL_DURATION_RECORDS, 4) ||
        !readable((void*)WoW112::SPELL_DURATION_COUNT, 4)) return 0;
    unsigned long count = *(unsigned long*)WoW112::SPELL_DURATION_COUNT;
    if (index > count) return 0;
    const unsigned char* const* rows =
        *(const unsigned char* const**)WoW112::SPELL_DURATION_RECORDS;
    if (!rows || !readable(rows + index, 4)) return 0;
    const unsigned char* row = rows[index];
    return row && readable(row, 16) ? row : 0;
}

static unsigned long playerSpellFamily() {
    if (!executable(WoW112::RESOLVE_UNIT_TOKEN)) return 0;
    unsigned char* player = (unsigned char*)((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)("player");
    if (!player || !readable(player + WoW112::OFF_CGUNIT_OBJECT_FIELDS, 4)) return 0;
    unsigned char* desc = *(unsigned char**)(player + WoW112::OFF_CGUNIT_OBJECT_FIELDS);
    if (!desc || !readable(desc + WoW112::OFF_UNIT_DESCRIPTOR_CLASS_BYTE, 1)) return 0;
    unsigned long classId = *(unsigned char*)(desc + WoW112::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    if (!classId || !readable((void*)WoW112::CHRCLASSES_COUNT, 4) ||
        !readable((void*)WoW112::CHRCLASSES_RECORDS, 4)) return 0;
    unsigned long count = *(unsigned long*)WoW112::CHRCLASSES_COUNT;
    if (classId > count) return 0;
    const unsigned char* const* rows =
        *(const unsigned char* const**)WoW112::CHRCLASSES_RECORDS;
    if (!rows || !readable(rows + classId, 4)) return 0;
    const unsigned char* row = rows[classId];
    if (!row || !readable(row + WoW112::OFF_CHRCLASSES_SPELL_FAMILY, 4)) return 0;
    return *(unsigned long*)(row + WoW112::OFF_CHRCLASSES_SPELL_FAMILY);
}

static unsigned long applyLocalDurationMods(const unsigned char* rec,
                                            unsigned long baseMs) {
    if (!rec || !baseMs) return baseMs;
    const unsigned long family =
        *(unsigned long*)(rec + WoW112::OFF_SPELL_RECORD_FAMILY_NAME);
    const unsigned long attrEx3 =
        *(unsigned long*)(rec + WoW112::OFF_SPELL_RECORD_ATTRIBUTES_EX3);
    if (!family || (attrEx3 & WoW112::SPELL_ATTR_EX3_IGNORE_CASTER_MODIFIERS))
        return baseMs;
    const unsigned long playerFamily = playerSpellFamily();
    if (!playerFamily || playerFamily != family) return baseMs;

    const unsigned long long flags =
        *(unsigned long long*)(rec + WoW112::OFF_SPELL_RECORD_FAMILY_FLAGS);
    if (!flags ||
        !readable((void*)WoW112::SPELLMOD_FLAT_TABLE,
                  WoW112::SPELLMOD_SLOT_COUNT * WoW112::SPELLMOD_SLOT_STRIDE) ||
        !readable((void*)WoW112::SPELLMOD_PCT_TABLE,
                  WoW112::SPELLMOD_SLOT_COUNT * WoW112::SPELLMOD_SLOT_STRIDE))
        return baseMs;

    long flat = 0;
    long pct = 0;
    const unsigned long opOffset = WoW112::SPELLMOD_OP_DURATION * 4UL;
    for (unsigned long bit = 0; bit < WoW112::SPELLMOD_SLOT_COUNT; ++bit) {
        if ((flags & (1ULL << bit)) == 0) continue;
        const unsigned long off = bit * WoW112::SPELLMOD_SLOT_STRIDE + opOffset;
        flat += *(long*)(WoW112::SPELLMOD_FLAT_TABLE + off);
        pct += *(long*)(WoW112::SPELLMOD_PCT_TABLE + off);
    }
    long value = (long)baseMs + flat;
    if (value <= 0) return 0;
    long pctTotal = 100L + pct;
    if (pctTotal <= 0) return 0;
    if (value > 0x7fffffffL / pctTotal) return 0;
    value = value * pctTotal / 100L;
    return value > 0 ? (unsigned long)value : 0UL;
}

static bool currentCombo(unsigned long* cpOut, unsigned long long* targetOut) {
    if (cpOut) *cpOut = 0;
    if (targetOut) *targetOut = 0;
    if (!executable(WoW112::RESOLVE_UNIT_TOKEN)) return false;
    unsigned char* player = (unsigned char*)((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)("player");
    if (!player || !readable(player + WoW112::OFF_CGPLAYER_INFO, 4)) return false;
    unsigned char* info = *(unsigned char**)(player + WoW112::OFF_CGPLAYER_INFO);
    if (!info || !readable(info + WoW112::OFF_CGPLAYER_COMBO_POINTS, 1) ||
        !readable(info + WoW112::OFF_CGPLAYER_COMBO_TARGET, 8)) return false;
    unsigned long cp = *(unsigned char*)(info + WoW112::OFF_CGPLAYER_COMBO_POINTS);
    unsigned long long target = *(unsigned long long*)(info + WoW112::OFF_CGPLAYER_COMBO_TARGET);
    if (cpOut) *cpOut = cp;
    if (targetOut) *targetOut = target;
    return true;
}

static void onOutgoing(unsigned long opcode, TysNativeBus::CDataStoreView* packet) {
    if (opcode != WoW112::CMSG_CAST_SPELL_OPCODE || !packet) return;
    ++g_castRequests;
    unsigned long spellId = 0;
    if (!TysNativeBus::read(packet, &spellId) || !spellId) return;
    unsigned long cp = 0;
    unsigned long long comboTarget = 0;
    if (!currentCombo(&cp, &comboTarget) || cp < 1 || cp > 5) return;
    TysComboDurationCore::remember(spellId, cp, comboTarget, GetTickCount());
    ++g_comboCaptures;
}

static unsigned long regularDuration(const unsigned char* rec, bool localCaster) {
    if (!rec || !executable(WoW112::GET_SPELL_DURATION)) return 0;
    int ms = ((GetSpellDurationFn)WoW112::GET_SPELL_DURATION)(
        rec, 0, localCaster ? 0 : 1);
    return ms > 0 ? (unsigned long)ms : 0UL;
}

} // namespace

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0)
        return InterlockedCompareExchange(&g_subscribed, 0, 0) != 0;
    TysComboDurationCore::initialize();
    if (!TysNativeBus::outgoingHookInstalled()) {
        copyText(g_status, sizeof(g_status), "NATIVE_BUS_OUTGOING_NOT_READY");
        return false;
    }
    if (!TysNativeBus::subscribeOutgoing(&onOutgoing)) {
        copyText(g_status, sizeof(g_status), "SUBSCRIBE_CMSG_CAST_SPELL_FAILED");
        return false;
    }
    InterlockedExchange(&g_subscribed, 1);
    copyText(g_status, sizeof(g_status), "READY_TARGET_DURATION_EVIDENCE");
    return true;
}

void onWorldLeaving() {
    TysComboDurationCore::resetWorldState();
}

Evidence resolve(unsigned long long casterGuid, unsigned long spellId,
                 unsigned long now) {
    Evidence out = {};
    if (!casterGuid || !spellId) {
        ++g_durationMisses;
        return out;
    }
    const unsigned char* rec = spellRecord(spellId);
    if (!rec) {
        ++g_durationMisses;
        return out;
    }

    const bool localCaster = casterGuid == activePlayerGuid();
    if (localCaster) {
        TysComboDurationCore::CaptureView cap = {};
        if (TysComboDurationCore::consume(spellId, now, &cap)) {
            ++g_comboConsumes;
            const unsigned long durationIndex =
                *(unsigned long*)(rec + WoW112::OFF_SPELL_RECORD_DURATION_INDEX);
            const unsigned char* row = durationRow(durationIndex);
            if (row) {
                const long base = *(long*)(row + 4);
                const long max = *(long*)(row + 12);
                if (base > 0 && max > 0 && base != max && max > base) {
                    unsigned long raw = TysComboDurationCore::scaleDuration(
                        (unsigned long)base, (unsigned long)max, cap.comboPoints, 0, 0);
                    unsigned long modified = applyLocalDurationMods(rec, raw);
                    if (modified) {
                        out.durationMs = modified;
                        out.source = TIME_SOURCE_LOCAL_COMBO_SCALED;
                        out.valid = true;
                        ++g_localComboDurations;
                        return out;
                    }
                }
            }
            // The CP capture belonged to a non-combo aura spell or unusable DBC
            // row. Fall through to the engine's ordinary local duration path.
        }
        unsigned long ms = regularDuration(rec, true);
        if (ms) {
            out.durationMs = ms;
            out.source = TIME_SOURCE_LOCAL_CAST_MODIFIED;
            out.valid = true;
            ++g_localDurations;
            return out;
        }
    } else {
        unsigned long ms = regularDuration(rec, false);
        if (ms) {
            out.durationMs = ms;
            out.source = TIME_SOURCE_REMOTE_BASE;
            out.valid = true;
            ++g_remoteBaseDurations;
            return out;
        }
    }
    ++g_durationMisses;
    return out;
}

const char* sourceName(unsigned char source) {
    if (source == TIME_SOURCE_LOCAL_CAST_MODIFIED) return "LOCAL_CAST_MODIFIED";
    if (source == TIME_SOURCE_LOCAL_COMBO_SCALED) return "LOCAL_COMBO_SCALED";
    if (source == TIME_SOURCE_REMOTE_BASE) return "REMOTE_BASE";
    return "NONE";
}

Stats stats() {
    Stats s = {};
    s.castRequestsSeen = (unsigned long)InterlockedCompareExchange(&g_castRequests, 0, 0);
    s.comboCaptures = (unsigned long)InterlockedCompareExchange(&g_comboCaptures, 0, 0);
    s.comboConsumes = (unsigned long)InterlockedCompareExchange(&g_comboConsumes, 0, 0);
    s.localDurations = (unsigned long)InterlockedCompareExchange(&g_localDurations, 0, 0);
    s.localComboDurations = (unsigned long)InterlockedCompareExchange(&g_localComboDurations, 0, 0);
    s.remoteBaseDurations = (unsigned long)InterlockedCompareExchange(&g_remoteBaseDurations, 0, 0);
    s.durationMisses = (unsigned long)InterlockedCompareExchange(&g_durationMisses, 0, 0);
    return s;
}

const char* status() { return g_status; }

} // namespace TysAuraCastTiming
