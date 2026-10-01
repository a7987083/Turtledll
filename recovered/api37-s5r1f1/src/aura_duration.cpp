/*
 * TaiYangShenDian AURA6-D4-R1 self exact duration path.
 *
 * The standard 1.12 SMSG_UPDATE_AURA_DURATION path is active-player only:
 * SMSG_UPDATE_AURA_DURATION -> CGBuffBar_UpdateDuration -> player expiration
 * table. D4-R1 keeps this exact native source and removes all external-backend
 * arbitration. Target duration remains unknown at this stage.
 */

#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "aura_duration.h"
#include "aura_native.h"
#include "wow112_offsets.h"

namespace TysAuraDuration {
namespace {

constexpr unsigned long MAX_AURA_SLOTS = 48UL;
constexpr unsigned long UNIT_FIELDS_PTR_OFF = 0x110UL;
constexpr int BUFF_UPDATE_DURATION_SELF = 580;
constexpr int DEBUFF_UPDATE_DURATION_SELF = 581;

#pragma pack(push, 1)
struct UnitFieldsAuraView {
    unsigned char preAura[0xA4];
    unsigned long aura[48];
    unsigned long auraFlags[6];
    unsigned char auraLevels[48];
    unsigned char auraApplications[48];
};
#pragma pack(pop)

using ResolveUnitFn = void* (__fastcall *)(const char* token);
using GetTimeMsFn = unsigned long long (__stdcall *)();
using BuffBarUpdateDurationFn = void (__fastcall *)(unsigned char auraSlot, int durationMs);
using SignalEventParamFn = int (__cdecl *)(int eventCode, char* format, ...);
using SStrDupAFn = char* (__stdcall *)(char* source, char* source2, int tag);

static volatile LONG g_initOnce = 0;
static volatile LONG g_hookInstalled = 0;
static volatile LONG g_eventsReady = 0;
static volatile LONG g_emitEnabled = 0;
static volatile LONG g_eventCount = 0;
static volatile LONG g_invalidSlot = 0;
static volatile LONG g_spellIdZero = 0;
static unsigned long g_lastRawSlot = 0;
static unsigned long g_lastDurationMs = 0;
static unsigned long g_lastExpirationMs = 0;
static unsigned long g_lastSpellId = 0;
static unsigned long g_lastEventTick = 0;
static char g_status[96] = "NOT_INITIALIZED";
static char g_backend[48] = "NOT_INITIALIZED";

static BuffBarUpdateDurationFn g_nextUpdateDuration = 0;
static bool g_createdHook = false;

static bool sameText(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;;) {
        if (*a != *b) return false;
        if (!*a) return true;
        ++a; ++b;
    }
}

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
             p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
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

static void setString(Lua50::State L, const char* key, const char* value) {
    Lua50::PushString(L, key); Lua50::PushString(L, value ? value : ""); Lua50::SetTable(L, -3);
}
static void setNumber(Lua50::State L, const char* key, double value) {
    Lua50::PushString(L, key); Lua50::PushNumber(L, value); Lua50::SetTable(L, -3);
}
static void setBool(Lua50::State L, const char* key, bool value) {
    Lua50::PushString(L, key); Lua50::PushBool(L, value); Lua50::SetTable(L, -3);
}

static UnitFieldsAuraView* unitFields(void* unit) {
    if (!unit || !readable((unsigned char*)unit + UNIT_FIELDS_PTR_OFF, 4)) return 0;
    UnitFieldsAuraView* f = *(UnitFieldsAuraView**)((unsigned char*)unit + UNIT_FIELDS_PTR_OFF);
    if (!f || !readable(f, sizeof(UnitFieldsAuraView))) return 0;
    return f;
}

static unsigned long clientNowMs() {
    if (!executable(WoW112::OS_GET_ASYNC_TIME_MS)) return GetTickCount();
    unsigned long long now = ((GetTimeMsFn)WoW112::OS_GET_ASYNC_TIME_MS)();
    return (unsigned long)(now & 0xffffffffULL);
}

static unsigned long remainingMs(unsigned long expiration, unsigned long now) {
    if (!expiration) return 0;
    long delta = (long)(expiration - now);
    return delta > 0 ? (unsigned long)delta : 0UL;
}

static unsigned long playerSpellAt(unsigned long slot) {
    if (slot >= MAX_AURA_SLOTS || !executable(WoW112::RESOLVE_UNIT_TOKEN)) return 0;
    void* unit = ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)("player");
    UnitFieldsAuraView* f = unitFields(unit);
    return f ? f->aura[slot] : 0;
}

static bool eventSlotCompatible(char** data, int id, const char* expected) {
    if (!data || !expected) return false;
    char* existing = data[id * 4];
    if (!existing) return true;
    if (!readable(existing, 1)) return false;
    return sameText(existing, expected);
}

static bool installEventName(char** data, int id, char* name) {
    if (!data || !name || !executable(WoW112::SSTR_DUP_A)) return false;
    if (!eventSlotCompatible(data, id, name)) return false;
    if (data[id * 4]) return true;
    SStrDupAFn dup = (SStrDupAFn)WoW112::SSTR_DUP_A;
    char* stored = dup(name, name, 1308);
    if (!stored) return false;
    data[id * 4] = stored;
    return true;
}

// 0 not ready, 1 compatible, 2 conflict.
static int durationEventRegistryState() {
    if (!readable((void*)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA, 4)) return 0;
    char** data = *(char***)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA;
    if (!data || !readable(data, sizeof(char*) * 700UL * 4UL)) return 0;
    char* b = data[BUFF_UPDATE_DURATION_SELF * 4];
    char* d = data[DEBUFF_UPDATE_DURATION_SELF * 4];
    if (!b || !d) return 0;
    if (!readable(b, 1) || !readable(d, 1)) return 2;
    return sameText(b, "BUFF_UPDATE_DURATION_SELF") && sameText(d, "DEBUFF_UPDATE_DURATION_SELF") ? 1 : 2;
}

static void setCommon(Lua50::State L) {
    setString(L, "version", "AURA4-D2");
    setString(L, "mode", "SELF_EXACT_DURATION_EVENT");
    setBool(L, "eventDriven", true);
    setBool(L, "backgroundPolling", false);
    setBool(L, "auraScanning", false);
    setBool(L, "luaCountdownRecommended", true);
    setBool(L, "selfExactAvailable", true);
    setString(L, "selfSource", "SMSG_UPDATE_AURA_DURATION_0x137 -> CGBuffBar_UpdateDuration");
    setBool(L, "targetExactAvailable", false);
    setString(L, "targetSource", "NOT_PRESENT_STANDARD_1_12_CLIENT_STATE");
    setString(L, "targetReason", "UNIT_FIELDS_NO_DURATION; OPCODE_0x137_SELF_ONLY; EXTRA_AURA_INFO_NOT_REGISTERED");
    setBool(L, "targetEstimateReturned", false);
    setString(L, "targetEstimatePolicy", "D2_DOES_NOT_FAKE_STATIC_SPELL_DURATION_AS_EXACT");
    setNumber(L, "smsgUpdateAuraDurationOpcode", 0x137);
    setNumber(L, "packetHandler", (double)WoW112::SMSG_UPDATE_AURA_DURATION_HANDLER);
    setNumber(L, "buffBarUpdateDuration", (double)WoW112::CGBUFFBAR_UPDATE_DURATION);
    setNumber(L, "expirationArray", (double)WoW112::BUFFBAR_EXPIRATION_ARRAY);
    setNumber(L, "clockFunction", (double)WoW112::OS_GET_ASYNC_TIME_MS);
    setNumber(L, "buffDurationEventId", (double)BUFF_UPDATE_DURATION_SELF);
    setNumber(L, "debuffDurationEventId", (double)DEBUFF_UPDATE_DURATION_SELF);
}

static void __fastcall updateDurationHook(unsigned char auraSlot, int durationMs) {
    if (g_nextUpdateDuration) g_nextUpdateDuration(auraSlot, durationMs);

    if (auraSlot >= MAX_AURA_SLOTS) {
        ++g_invalidSlot;
        return;
    }
    if (InterlockedCompareExchange(&g_emitEnabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_eventsReady, 0, 0) == 0) return;

    unsigned long expiration = 0;
    if (readable((void*)WoW112::BUFFBAR_EXPIRATION_ARRAY, MAX_AURA_SLOTS * 4UL)) {
        expiration = ((unsigned long*)WoW112::BUFFBAR_EXPIRATION_ARRAY)[auraSlot];
    }
    if (durationMs > 0 && expiration == 0) expiration = clientNowMs() + (unsigned long)durationMs;
    if (durationMs <= 0) expiration = 0;

    unsigned long spellId = playerSpellAt((unsigned long)auraSlot);
    if (!spellId) ++g_spellIdZero; // valid on some first-application callbacks

    int eventId = auraSlot < 32 ? BUFF_UPDATE_DURATION_SELF : DEBUFF_UPDATE_DURATION_SELF;
    static char format[] = "%d%d%d%d";
    ((SignalEventParamFn)WoW112::SIGNAL_EVENT_PARAM)(eventId, format,
        (unsigned long)auraSlot, durationMs, expiration, spellId);

    ++g_eventCount;
    g_lastRawSlot = (unsigned long)auraSlot;
    g_lastDurationMs = durationMs > 0 ? (unsigned long)durationMs : 0UL;
    g_lastExpirationMs = expiration;
    g_lastSpellId = spellId;
    g_lastEventTick = GetTickCount();
}

} // namespace

bool installEventNames(char** eventData) {
    char b[] = "BUFF_UPDATE_DURATION_SELF";
    char d[] = "DEBUFF_UPDATE_DURATION_SELF";
    if (!installEventName(eventData, BUFF_UPDATE_DURATION_SELF, b) ||
        !installEventName(eventData, DEBUFF_UPDATE_DURATION_SELF, d)) {
        InterlockedExchange(&g_eventsReady, 0);
        InterlockedExchange(&g_emitEnabled, 0);
        copyText(g_status, sizeof(g_status), "DURATION_EVENT_ID_CONFLICT");
        return false;
    }
    InterlockedExchange(&g_eventsReady, 1);
    if (InterlockedCompareExchange(&g_hookInstalled, 0, 0) != 0) InterlockedExchange(&g_emitEnabled, 1);
    copyText(g_status, sizeof(g_status), "READY_SELF_EXACT_DURATION_EVENTS");
    return true;
}

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0) {
        return InterlockedCompareExchange(&g_hookInstalled, 0, 0) != 0;
    }

    copyText(g_backend, sizeof(g_backend), "TYS_NATIVE");
    if (!TysAuraNative::nativeHooksInstalled()) {
        copyText(g_status, sizeof(g_status), "AURA_NATIVE_BACKEND_NOT_READY");
        return false;
    }
    if (!executable(WoW112::CGBUFFBAR_UPDATE_DURATION) ||
        !executable(WoW112::SIGNAL_EVENT_PARAM) ||
        !executable(WoW112::RESOLVE_UNIT_TOKEN)) {
        copyText(g_status, sizeof(g_status), "DURATION_ADDRESS_PREFLIGHT_FAILED");
        return false;
    }

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        copyText(g_status, sizeof(g_status), "MH_INIT_FAILED");
        return false;
    }
    MH_STATUS cr = MH_CreateHook((void*)WoW112::CGBUFFBAR_UPDATE_DURATION,
                                 (void*)&updateDurationHook,
                                 (void**)&g_nextUpdateDuration);
    if (cr != MH_OK) {
        copyText(g_status, sizeof(g_status), "HOOK_CGBUFFBAR_UPDATE_DURATION_FAILED");
        return false;
    }
    g_createdHook = true;
    MH_STATUS en = MH_EnableHook((void*)WoW112::CGBUFFBAR_UPDATE_DURATION);
    if (en != MH_OK && en != MH_ERROR_ENABLED) {
        MH_RemoveHook((void*)WoW112::CGBUFFBAR_UPDATE_DURATION);
        g_createdHook = false;
        g_nextUpdateDuration = 0;
        copyText(g_status, sizeof(g_status), "HOOK_CGBUFFBAR_UPDATE_DURATION_ENABLE_FAILED");
        return false;
    }

    InterlockedExchange(&g_hookInstalled, 1);
    // Event names are injected later from the existing FrameScript CreateEvents
    // hook. Emission stays disabled until ids 580/581 are confirmed registered.
    copyText(g_status, sizeof(g_status), "DURATION_HOOK_INSTALLED_WAITING_EVENT_TABLE");
    return true;
}

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    int registry = durationEventRegistryState();

    Lua50::NewTable(L);
    setCommon(L);
    setString(L, "backend", g_backend);
    setString(L, "status", g_status);
    setBool(L, "installsHook", InterlockedCompareExchange(&g_hookInstalled, 0, 0) != 0);
    setBool(L, "durationHookInstalled", InterlockedCompareExchange(&g_hookInstalled, 0, 0) != 0);
    setBool(L, "durationEventsReady", registry == 1);
    setString(L, "durationEventRegistry", registry == 1 ? "READY_TYS" : (registry == 2 ? "CONFLICT" : "NOT_READY"));
    setBool(L, "emitEnabled", InterlockedCompareExchange(&g_emitEnabled, 0, 0) != 0);
    setNumber(L, "nativeDurationEvents", (double)InterlockedCompareExchange(&g_eventCount, 0, 0));
    setNumber(L, "invalidSlot", (double)InterlockedCompareExchange(&g_invalidSlot, 0, 0));
    setNumber(L, "spellIdZeroEvents", (double)InterlockedCompareExchange(&g_spellIdZero, 0, 0));
    setNumber(L, "lastRawSlot", (double)g_lastRawSlot);
    setNumber(L, "lastDurationMs", (double)g_lastDurationMs);
    setNumber(L, "lastExpirationMs", (double)g_lastExpirationMs);
    setNumber(L, "lastSpellId", (double)g_lastSpellId);
    setNumber(L, "lastAgeMs", g_lastEventTick ? (double)(GetTickCount() - g_lastEventTick) : 0.0);
    setBool(L, "clockExecutable", executable(WoW112::OS_GET_ASYNC_TIME_MS));
    setBool(L, "buffBarUpdateExecutable", executable(WoW112::CGBUFFBAR_UPDATE_DURATION));
    setBool(L, "packetHandlerExecutable", executable(WoW112::SMSG_UPDATE_AURA_DURATION_HANDLER));
    setBool(L, "expirationArrayReadable", readable((void*)WoW112::BUFFBAR_EXPIRATION_ARRAY, MAX_AURA_SLOTS * 4UL));
    setBool(L, "resolveUnitExecutable", executable(WoW112::RESOLVE_UNIT_TOKEN));
    setString(L, "verdict", "SELF_EXACT_EVENT_DRIVEN_TARGET_EXACT_NOT_FOUND");
    return 1;
}

int dispatchSnapshot(Lua50::State L) {
    if (!L) return 0;
    const char* token = "player";
    if (Lua50::GetTop(L) >= 2 && Lua50::IsString(L, 2)) token = Lua50::ToString(L, 2);
    if (!token || (!sameText(token, "player") && !sameText(token, "target"))) {
        Lua50::PushNil(L); Lua50::PushString(L, "BAD_UNIT"); return 2;
    }

    Lua50::NewTable(L);
    setCommon(L);
    setString(L, "unit", token);

    if (sameText(token, "target")) {
        setBool(L, "exact", false);
        setString(L, "source", "TARGET_EXACT_UNAVAILABLE_STANDARD_1_12");
        setString(L, "reason", "NO_TARGET_DURATION_FIELD_OR_PACKET_IN_THIS_CLIENT_BUILD");
        setNumber(L, "count", 0.0);
        return 1;
    }

    if (!executable(WoW112::RESOLVE_UNIT_TOKEN) ||
        !readable((void*)WoW112::BUFFBAR_EXPIRATION_ARRAY, MAX_AURA_SLOTS * 4UL)) {
        Lua50::PushNil(L); Lua50::PushString(L, "DURATION_SOURCE_UNAVAILABLE"); return 2;
    }

    void* unit = ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)("player");
    UnitFieldsAuraView* f = unitFields(unit);
    if (!unit || !f) {
        Lua50::PushNil(L); Lua50::PushString(L, "PLAYER_AURA_STATE_UNAVAILABLE"); return 2;
    }

    unsigned long* expirations = (unsigned long*)WoW112::BUFFBAR_EXPIRATION_ARRAY;
    unsigned long now = clientNowMs();
    setBool(L, "exact", true);
    setString(L, "source", "CLIENT_BUFFBAR_EXPIRATION_ARRAY");
    setNumber(L, "nowMs", (double)now);
    setNumber(L, "slotCapacity", 48.0);

    int row = 0;
    int timed = 0;
    for (unsigned long slot = 0; slot < MAX_AURA_SLOTS; ++slot) {
        unsigned long spellId = f->aura[slot];
        if (!spellId) continue;
        unsigned long expiration = expirations[slot];
        unsigned long remain = remainingMs(expiration, now);
        bool hasExactTimer = expiration != 0 && remain != 0;
        if (hasExactTimer) ++timed;

        ++row;
        Lua50::PushNumber(L, (double)row);
        Lua50::NewTable(L);
        setNumber(L, "spellId", (double)spellId);
        setNumber(L, "rawSlot", (double)slot);
        setBool(L, "isBuff", slot < 32);
        setNumber(L, "stacks", (double)((unsigned long)f->auraApplications[slot] + 1UL));
        setNumber(L, "auraLevel", (double)f->auraLevels[slot]);
        setNumber(L, "expirationMs", (double)expiration);
        setNumber(L, "remainingMs", (double)remain);
        setBool(L, "hasExactTimer", hasExactTimer);
        setString(L, "timerSource", expiration ? "SMSG_UPDATE_AURA_DURATION" : "NO_TIMED_VALUE");
        Lua50::SetTable(L, -3);
    }

    setNumber(L, "count", (double)row);
    setNumber(L, "timedCount", (double)timed);
    return 1;
}

} // namespace TysAuraDuration
