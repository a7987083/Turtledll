/*
 * TaiYangShenDian AURA6-D4-R1 native Aura backend.
 *
 * Single native ownership only: no external-provider detection, no backend
 * switching, and no runtime suppression based on other DLLs. Aura callbacks
 * remain the existing event-driven source while network evidence moves to the
 * shared NativeBus.
 */

#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "aura_native.h"
#include "aura_caster.h"
#include "aura_duration.h"
#include "spell_cast_core.h"
#include "wow112_offsets.h"

namespace TysAuraNative {
namespace {

// TYS custom Aura event ids. Vanilla ends before this range.
enum AuraEventId {
    DEBUFF_ADDED_SELF   = 551,
    DEBUFF_REMOVED_SELF = 552,
    DEBUFF_ADDED_OTHER  = 553,
    DEBUFF_REMOVED_OTHER= 554,
    BUFF_ADDED_SELF     = 555,
    BUFF_REMOVED_SELF   = 556,
    BUFF_ADDED_OTHER    = 557,
    BUFF_REMOVED_OTHER  = 558
};

constexpr unsigned long EVENT_COUNT_EXPANDED = 700UL;
constexpr unsigned long MAX_AURA_SLOTS = 48UL;
constexpr unsigned long UNIT_FIELDS_PTR_OFF = 0x110UL;
constexpr unsigned long UNIT_GUID_OFF = 0x30UL;
constexpr unsigned long SPELL_AURA_TRACK_CREATURES = 44UL;
constexpr unsigned long SPELL_AURA_TRACK_RESOURCES = 45UL;
constexpr unsigned long SPELL_AURA_TRACK_STEALTHED = 151UL;
constexpr unsigned long SPELL_ATTR_HIDDEN_CLIENTSIDE = 0x00000080UL;
constexpr unsigned long SPELL_ATTR_EX_NO_AURA_ICON = 0x10000000UL;

static volatile LONG g_initOnce = 0;
static volatile LONG g_nativeHooks = 0;
static volatile LONG g_eventsReady = 0;
static volatile LONG g_emitEnabled = 0;
static char g_backend[48] = "NOT_INITIALIZED";
static char g_status[96] = "NOT_INITIALIZED";

// AURA3 diagnostic counters. Aura callbacks execute on the client/game path;
// these values are observational only and never drive Aura state.
static volatile LONG g_countAdded = 0;
static volatile LONG g_countRemoved = 0;
static volatile LONG g_countStack = 0;
static volatile LONG g_invalidSlot = 0;
static volatile LONG g_invalidGuid = 0;
static volatile LONG g_invalidFields = 0;
static volatile LONG g_duplicateRemoveSuspect = 0;

// AURA3 explicit Snapshot diagnostics. These are incremented only when Lua explicitly
// calls Aura.Snapshot; there is no background snapshot/polling path.
static volatile LONG g_snapshotCalls = 0;
static volatile LONG g_snapshotSuccess = 0;
static volatile LONG g_snapshotGeneration = 0;
static volatile LONG g_snapshotUnavailable = 0;
static unsigned long g_lastSnapshotTick = 0;
static unsigned long long g_lastSnapshotGuid = 0;
static unsigned long g_lastSnapshotCount = 0;
static char g_lastSnapshotUnit[12] = "NONE";

static unsigned long long g_lastGuid = 0;
static unsigned long g_lastSpellId = 0;
static unsigned long g_lastRawSlot = 0;
static int g_lastLuaSlot = 0;
static unsigned long g_lastStacks = 0;
static unsigned long g_lastState = 0;
static unsigned long g_lastEventTick = 0;
static char g_lastEventName[40] = "NONE";

static unsigned long long g_lastRemoveGuid = 0;
static unsigned long g_lastRemoveSpell = 0;
static unsigned long g_lastRemoveSlot = 0;
static unsigned long g_lastRemoveTick = 0;

// The pointer at CGUnit + 0x110 is the UnitFields view used by the 1.12 client
// Aura callbacks. aura[] begins 0xA4 bytes into UnitFields.
#pragma pack(push, 1)
struct UnitFieldsAuraView {
    unsigned char preAura[0xA4];
    unsigned long aura[48];
    unsigned long auraFlags[6];
    unsigned char auraLevels[48];
    unsigned char auraApplications[48];
};

// Exact prefix of the 1.12 SpellRec layout through EffectApplyAuraName[].
// Keeping the exact prefix lets the native path apply the established hidden-aura
// filtering without loading/parsing MPQ data on each Aura event.
struct SpellRecPrefix {
    unsigned int Id;
    unsigned int School;
    unsigned int Category;
    unsigned int castUI;
    unsigned int Dispel;
    unsigned int Mechanic;
    unsigned int Attributes;
    unsigned int AttributesEx;
    unsigned int AttributesEx2;
    unsigned int AttributesEx3;
    unsigned int AttributesEx4;
    unsigned int Stances;
    unsigned int StancesNot;
    unsigned int Targets;
    unsigned int TargetCreatureType;
    unsigned int RequiresSpellFocus;
    unsigned int CasterAuraState;
    unsigned int TargetAuraState;
    unsigned int CastingTimeIndex;
    unsigned int RecoveryTime;
    unsigned int CategoryRecoveryTime;
    unsigned int InterruptFlags;
    unsigned int AuraInterruptFlags;
    unsigned int ChannelInterruptFlags;
    unsigned int procFlags;
    unsigned int procChance;
    unsigned int procCharges;
    unsigned int maxLevel;
    unsigned int baseLevel;
    unsigned int spellLevel;
    unsigned int DurationIndex;
    unsigned int powerType;
    unsigned int manaCost;
    unsigned int manaCostPerlevel;
    unsigned int manaPerSecond;
    unsigned int manaPerSecondPerLevel;
    unsigned int rangeIndex;
    float speed;
    unsigned int modalNextSpell;
    unsigned int StackAmount;
    unsigned int Totem[2];
    int Reagent[8];
    unsigned int ReagentCount[8];
    int EquippedItemClass;
    int EquippedItemSubClassMask;
    int EquippedItemInventoryTypeMask;
    unsigned int Effect[3];
    int EffectDieSides[3];
    unsigned int EffectBaseDice[3];
    float EffectDicePerLevel[3];
    float EffectRealPointsPerLevel[3];
    int EffectBasePoints[3];
    unsigned int EffectMechanic[3];
    unsigned int EffectImplicitTargetA[3];
    unsigned int EffectImplicitTargetB[3];
    unsigned int EffectRadiusIndex[3];
    unsigned int EffectApplyAuraName[3];
};

struct SpellDbView {
    SpellRecPrefix* records;
    unsigned long numRecords;
    SpellRecPrefix** recordsById;
    unsigned long maxId;
    int loaded;
};
#pragma pack(pop)

using AuraAddedFn = void (__fastcall *)(void* unit, void* dummyEdx, unsigned long slot, unsigned long spellId);
using AuraRemovedFn = void (__fastcall *)(void* unit, void* dummyEdx, unsigned long slot, unsigned long spellId);
using AuraStacksFn = void (__fastcall *)(void* unit, void* dummyEdx, int slot, unsigned char stackCount);
using CreateEventsFn = void (__fastcall *)(int param1, unsigned long maxEventId);
using SetEventCountFn = void (__fastcall *)(void* thisPtr, void* dummyEdx, unsigned long count);
using SignalEventParamFn = int (__cdecl *)(int eventCode, char* format, ...);
using SStrDupAFn = char* (__stdcall *)(char* source, char* source2, int tag);
using GetActivePlayerFn = unsigned long long (__stdcall *)();
using ResolveUnitFn = void* (__fastcall *)(const char* token);

static AuraAddedFn g_nextAuraAdded = 0;
static AuraRemovedFn g_nextAuraRemoved = 0;
static AuraStacksFn g_nextAuraStacks = 0;
static CreateEventsFn g_nextCreateEvents = 0;
static SetEventCountFn g_nextSetEventCount = 0;

static bool g_createdAuraAdded = false;
static bool g_createdAuraRemoved = false;
static bool g_createdAuraStacks = false;
static bool g_createdCreateEvents = false;
static bool g_createdSetEventCount = false;

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
    if (VirtualQuery(ptr, &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
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
    if (!address || VirtualQuery((void*)address, &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static void appendText(char* out, unsigned cap, const char* s) {
    if (!out || !cap || !s) return;
    unsigned n = 0;
    while (n < cap && out[n]) ++n;
    if (n >= cap) return;
    unsigned i = 0;
    while (s[i] && n + i + 1 < cap) {
        out[n + i] = s[i];
        ++i;
    }
    out[n + i] = 0;
}

static const char* fileBaseName(const char* p) {
    if (!p) return "";
    const char* r = p;
    while (*p) {
        if (*p == '\\' || *p == '/') r = p + 1;
        ++p;
    }
    return r;
}

static HMODULE selfModule() {
    MEMORY_BASIC_INFORMATION m = {};
    if (VirtualQuery((void*)&selfModule, &m, sizeof(m)) == sizeof(m) && m.AllocationBase)
        return (HMODULE)m.AllocationBase;
    return 0;
}

static unsigned long moduleImageSize(HMODULE module) {
    if (!module || !readable(module, 0x40)) return 0;
    unsigned char* base = (unsigned char*)module;
    if (*(unsigned short*)base != 0x5A4D) return 0;
    unsigned long peOff = *(unsigned long*)(base + 0x3C);
    if (!readable(base + peOff, 0x80) || *(unsigned long*)(base + peOff) != 0x00004550UL) return 0;
    // IMAGE_OPTIONAL_HEADER32.SizeOfImage is +56 from the optional-header start.
    return *(unsigned long*)(base + peOff + 24 + 56);
}

static bool addressInModule(unsigned long address, HMODULE module) {
    if (!address || !module) return false;
    unsigned long base = (unsigned long)module;
    unsigned long size = moduleImageSize(module);
    return size && address >= base && address < base + size;
}

static bool patchDestination(unsigned long address, unsigned long* destination) {
    if (!destination || !readable((void*)address, 6)) return false;
    const unsigned char* p = (const unsigned char*)address;
    if (p[0] == 0xE9) {
        long rel = *(const long*)(p + 1);
        *destination = address + 5 + (unsigned long)rel;
        return true;
    }
    if (p[0] == 0xFF && p[1] == 0x25) {
        unsigned long q = *(const unsigned long*)(p + 2);
        if (!readable((void*)q, 4)) return false;
        *destination = *(const unsigned long*)q;
        return true;
    }
    if (p[0] == 0x68 && p[5] == 0xC3) {
        *destination = *(const unsigned long*)(p + 1);
        return true;
    }
    return false;
}

static void classifyPatchedTarget(unsigned long target, char out[96]) {
    if (!out) return;
    out[0] = 0;
    if (!readable((void*)target, 6)) {
        copyText(out, 96, "UNREADABLE");
        return;
    }

    unsigned long dest = 0;
    if (!patchDestination(target, &dest)) {
        copyText(out, 96, "CLEAN_WOW");
        return;
    }

    HMODULE tys = selfModule();
    // Follow a few relay jumps; MinHook/Hadesmem can use an intermediate stub.
    for (int depth = 0; depth < 4 && dest; ++depth) {
        if (addressInModule(dest, tys)) {
            copyText(out, 96, "TYS");
            return;
        }
        unsigned long next = 0;
        if (patchDestination(dest, &next) && next != dest) {
            dest = next;
            continue;
        }

        MEMORY_BASIC_INFORMATION m = {};
        if (VirtualQuery((void*)dest, &m, sizeof(m)) == sizeof(m) && m.AllocationBase) {
            char full[MAX_PATH] = {};
            if (GetModuleFileNameA((HMODULE)m.AllocationBase, full, MAX_PATH)) {
                copyText(out, 96, "FOREIGN:");
                appendText(out, 96, fileBaseName(full));
                return;
            }
        }
        copyText(out, 96, "PATCH_UNKNOWN");
        return;
    }
    copyText(out, 96, "PATCH_UNKNOWN");
}

// 0 = not ready/unavailable, 1 = exact compatible names, 2 = conflict/mismatch.
static int eventRegistryState() {
    if (!readable((void*)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA, 4)) return 0;
    char** data = *(char***)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA;
    if (!data || !readable(data, sizeof(char*) * EVENT_COUNT_EXPANDED * 4UL)) return 0;
    const int ids[8] = {
        DEBUFF_ADDED_SELF, DEBUFF_REMOVED_SELF, DEBUFF_ADDED_OTHER, DEBUFF_REMOVED_OTHER,
        BUFF_ADDED_SELF, BUFF_REMOVED_SELF, BUFF_ADDED_OTHER, BUFF_REMOVED_OTHER
    };
    const char* names[8] = {
        "DEBUFF_ADDED_SELF", "DEBUFF_REMOVED_SELF", "DEBUFF_ADDED_OTHER", "DEBUFF_REMOVED_OTHER",
        "BUFF_ADDED_SELF", "BUFF_REMOVED_SELF", "BUFF_ADDED_OTHER", "BUFF_REMOVED_OTHER"
    };
    for (int i = 0; i < 8; ++i) {
        char* existing = data[ids[i] * 4];
        if (!existing) return 0;
        if (!readable(existing, 1) || !sameText(existing, names[i])) return 2;
    }
    return 1;
}

static bool ownerIs(const char* owner, const char* expected) {
    return owner && expected && sameText(owner, expected);
}

static bool ownerSuspicious(const char* owner) {
    if (!owner) return true;
    if (sameText(owner, "CLEAN_WOW") || sameText(owner, "TYS")) return false;
    return true;
}

static void hookOwners(char added[96], char removed[96], char stack[96], char createEvents[96], char setCount[96]) {
    classifyPatchedTarget(WoW112::CGUNIT_ON_AURA_ADDED, added);
    classifyPatchedTarget(WoW112::CGUNIT_ON_AURA_REMOVED, removed);
    classifyPatchedTarget(WoW112::CGUNIT_ON_AURA_STACKS_CHANGED, stack);
    classifyPatchedTarget(WoW112::FRAMESCRIPT_CREATE_EVENTS, createEvents);
    classifyPatchedTarget(WoW112::FRAMESCRIPT_SET_EVENT_COUNT, setCount);
}

static const char* eventNameFor(int eventId) {
    switch (eventId) {
        case DEBUFF_ADDED_SELF: return "DEBUFF_ADDED_SELF";
        case DEBUFF_REMOVED_SELF: return "DEBUFF_REMOVED_SELF";
        case DEBUFF_ADDED_OTHER: return "DEBUFF_ADDED_OTHER";
        case DEBUFF_REMOVED_OTHER: return "DEBUFF_REMOVED_OTHER";
        case BUFF_ADDED_SELF: return "BUFF_ADDED_SELF";
        case BUFF_REMOVED_SELF: return "BUFF_REMOVED_SELF";
        case BUFF_ADDED_OTHER: return "BUFF_ADDED_OTHER";
        case BUFF_REMOVED_OTHER: return "BUFF_REMOVED_OTHER";
        default: return "UNKNOWN";
    }
}

static void observeEvent(int eventId, unsigned long long guid, unsigned long spellId,
                         unsigned long rawSlot, int luaSlot, unsigned long stacks, unsigned long state) {
    unsigned long now = GetTickCount();
    if (state == 0) {
        ++g_countAdded;
        if (g_lastRemoveGuid == guid && g_lastRemoveSpell == spellId && g_lastRemoveSlot == rawSlot) {
            g_lastRemoveTick = 0; // legitimate re-application breaks duplicate-remove sequence
        }
    } else if (state == 1) {
        ++g_countRemoved;
        if (g_lastRemoveTick && g_lastRemoveGuid == guid && g_lastRemoveSpell == spellId &&
            g_lastRemoveSlot == rawSlot && (now - g_lastRemoveTick) <= 250UL) {
            ++g_duplicateRemoveSuspect;
        }
        g_lastRemoveGuid = guid;
        g_lastRemoveSpell = spellId;
        g_lastRemoveSlot = rawSlot;
        g_lastRemoveTick = now;
    } else if (state == 2) {
        ++g_countStack;
    }

    g_lastGuid = guid;
    g_lastSpellId = spellId;
    g_lastRawSlot = rawSlot;
    g_lastLuaSlot = luaSlot;
    g_lastStacks = stacks;
    g_lastState = state;
    g_lastEventTick = now;
    copyText(g_lastEventName, sizeof(g_lastEventName), eventNameFor(eventId));
}

static void setTableString(Lua50::State L, const char* key, const char* value) {
    Lua50::PushString(L, key); Lua50::PushString(L, value ? value : ""); Lua50::SetTable(L, -3);
}
static void setTableNumber(Lua50::State L, const char* key, double value) {
    Lua50::PushString(L, key); Lua50::PushNumber(L, value); Lua50::SetTable(L, -3);
}
static void setTableBool(Lua50::State L, const char* key, bool value) {
    Lua50::PushString(L, key); Lua50::PushBool(L, value); Lua50::SetTable(L, -3);
}

static UnitFieldsAuraView* unitFields(void* unit) {
    if (!unit || !readable((unsigned char*)unit + UNIT_FIELDS_PTR_OFF, 4)) return 0;
    UnitFieldsAuraView* f = *(UnitFieldsAuraView**)((unsigned char*)unit + UNIT_FIELDS_PTR_OFF);
    if (!f || !readable(f, sizeof(UnitFieldsAuraView))) return 0;
    return f;
}

static unsigned long long unitGuid(void* unit) {
    if (!unit || !readable((unsigned char*)unit + UNIT_GUID_OFF, 8)) return 0;
    return *(unsigned long long*)((unsigned char*)unit + UNIT_GUID_OFF);
}

static unsigned long long activePlayerGuid() {
    if (!executable(WoW112::GET_ACTIVE_PLAYER_GUID)) return 0;
    return ((GetActivePlayerFn)WoW112::GET_ACTIVE_PLAYER_GUID)();
}

static void guidToString(unsigned long long guid, char out[19]) {
    static const char h[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        unsigned shift = (unsigned)(15 - i) * 4U;
        out[2 + i] = h[(unsigned)((guid >> shift) & 0xFULL)];
    }
    out[18] = 0;
}

static const SpellRecPrefix* spellRec(unsigned long spellId) {
    if (!spellId || !readable((void*)WoW112::SPELL_DB, sizeof(SpellDbView))) return 0;
    SpellDbView* db = (SpellDbView*)WoW112::SPELL_DB;
    if (!db->recordsById || spellId > db->maxId || !readable(db->recordsById + spellId, 4)) return 0;
    SpellRecPrefix* r = db->recordsById[spellId];
    return (r && readable(r, sizeof(SpellRecPrefix))) ? r : 0;
}

static bool trackingAura(const SpellRecPrefix* r) {
    if (!r) return false;
    for (int i = 0; i < 3; ++i) {
        unsigned long a = r->EffectApplyAuraName[i];
        if (a == SPELL_AURA_TRACK_CREATURES || a == SPELL_AURA_TRACK_RESOURCES || a == SPELL_AURA_TRACK_STEALTHED) return true;
    }
    return false;
}

static bool hiddenForLua(unsigned long spellId) {
    if (!spellId) return true;
    const SpellRecPrefix* r = spellRec(spellId);
    if (!r) return false; // fail open, preserving the established native behavior
    if ((r->Attributes & SPELL_ATTR_HIDDEN_CLIENTSIDE) != 0) return true;
    if ((r->AttributesEx & SPELL_ATTR_EX_NO_AURA_ICON) != 0) return true;
    return trackingAura(r);
}

static bool visiblePackedFlag(const UnitFieldsAuraView* f, unsigned long slot) {
    if (!f || slot >= MAX_AURA_SLOTS) return false;
    const unsigned char* flags = (const unsigned char*)f->auraFlags;
    unsigned char packed = flags[slot / 2];
    unsigned char nibble = (slot & 1UL) ? (unsigned char)(packed >> 4) : packed;
    return (nibble & 0x0E) != 0;
}

static bool countsEmptyForLua(const UnitFieldsAuraView* f, unsigned long slot) {
    if (!f || slot >= MAX_AURA_SLOTS) return true;
    unsigned long id = f->aura[slot];
    if (!id) return true;
    if (hiddenForLua(id)) return true;
    return !visiblePackedFlag(f, slot);
}

static bool hiddenAura(const UnitFieldsAuraView* f, unsigned long slot, unsigned long spellId, unsigned long state) {
    if (spellId && hiddenForLua(spellId)) return true;
    // Removed hook runs after the client cleared the slot/flags.
    if (state == 1) return false;
    return countsEmptyForLua(f, slot);
}

static int luaSlotFromAuraSlot(const UnitFieldsAuraView* f, unsigned long slot) {
    bool isBuff = slot < 32;
    int luaSlot = (int)slot + 1;
    if (!isBuff) luaSlot -= 32;
    unsigned long first = isBuff ? 0UL : 32UL;
    int empty = 0;
    for (unsigned long i = first; i < slot; ++i) if (countsEmptyForLua(f, i)) ++empty;
    return luaSlot - empty;
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
    if (data[id * 4]) return true; // already compatible
    SStrDupAFn dup = (SStrDupAFn)WoW112::SSTR_DUP_A;
    char* stored = dup(name, name, 1308);
    if (!stored) return false;
    data[id * 4] = stored;
    return true;
}

static bool injectAuraEvents() {
    if (!readable((void*)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA, 4)) {
        copyText(g_status, sizeof(g_status), "EVENT_TABLE_POINTER_UNREADABLE");
        return false;
    }
    char** data = *(char***)WoW112::FRAMESCRIPT_EVENT_OBJECT_DATA;
    if (!data || !readable(data, sizeof(char*) * EVENT_COUNT_EXPANDED * 4UL)) {
        copyText(g_status, sizeof(g_status), "EVENT_TABLE_UNAVAILABLE");
        return false;
    }

    char dAs[] = "DEBUFF_ADDED_SELF";
    char dRs[] = "DEBUFF_REMOVED_SELF";
    char dAo[] = "DEBUFF_ADDED_OTHER";
    char dRo[] = "DEBUFF_REMOVED_OTHER";
    char bAs[] = "BUFF_ADDED_SELF";
    char bRs[] = "BUFF_REMOVED_SELF";
    char bAo[] = "BUFF_ADDED_OTHER";
    char bRo[] = "BUFF_REMOVED_OTHER";

    if (!installEventName(data, DEBUFF_ADDED_SELF, dAs) ||
        !installEventName(data, DEBUFF_REMOVED_SELF, dRs) ||
        !installEventName(data, DEBUFF_ADDED_OTHER, dAo) ||
        !installEventName(data, DEBUFF_REMOVED_OTHER, dRo) ||
        !installEventName(data, BUFF_ADDED_SELF, bAs) ||
        !installEventName(data, BUFF_REMOVED_SELF, bRs) ||
        !installEventName(data, BUFF_ADDED_OTHER, bAo) ||
        !installEventName(data, BUFF_REMOVED_OTHER, bRo)) {
        copyText(g_status, sizeof(g_status), "CUSTOM_EVENT_ID_CONFLICT");
        InterlockedExchange(&g_emitEnabled, 0);
        InterlockedExchange(&g_eventsReady, 0);
        return false;
    }

    // AURA4-D2 extends the same already-owned FrameScript table with the two
    // TYS self-duration event names (580/581). A duration-event
    // conflict must not disable the frozen ADD/REMOVE/STACK event family.
    TysAuraDuration::installEventNames(data);
    // CAST1-R1 shares the already-owned expanded FrameScript event table.
    // Failure here does not disable the frozen Aura event family.
    TysSpellCast::installEventNames(data);

    InterlockedExchange(&g_eventsReady, 1);
    InterlockedExchange(&g_emitEnabled, 1);
    copyText(g_status, sizeof(g_status), "READY_NATIVE_EVENTS");
    return true;
}

static void __fastcall createEventsHook(int param1, unsigned long maxEventId) {
    if (g_nextCreateEvents) g_nextCreateEvents(param1, maxEventId);
    if (maxEventId > 200 && InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0) injectAuraEvents();
}

static void __fastcall setEventCountHook(void* thisPtr, void* dummyEdx, unsigned long count) {
    if (count > 200 && count < EVENT_COUNT_EXPANDED) count = EVENT_COUNT_EXPANDED;
    if (g_nextSetEventCount) g_nextSetEventCount(thisPtr, dummyEdx, count);
}

static void triggerAuraEvent(void* unit, unsigned long slot, unsigned long spellId, bool wasAdded, unsigned long state) {
    if (InterlockedCompareExchange(&g_emitEnabled, 0, 0) == 0) return;
    if (!unit || slot >= MAX_AURA_SLOTS || !spellId) {
        if (slot >= MAX_AURA_SLOTS) ++g_invalidSlot;
        return;
    }
    UnitFieldsAuraView* f = unitFields(unit);
    if (!f) { ++g_invalidFields; return; }

    unsigned long long ug = unitGuid(unit);
    if (!ug) { ++g_invalidGuid; return; }
    unsigned long long pg = activePlayerGuid();
    bool self = pg != 0 && ug == pg;
    bool buff = slot < 32;

    int eventId = 0;
    if (buff) eventId = wasAdded ? (self ? BUFF_ADDED_SELF : BUFF_ADDED_OTHER)
                                 : (self ? BUFF_REMOVED_SELF : BUFF_REMOVED_OTHER);
    else eventId = wasAdded ? (self ? DEBUFF_ADDED_SELF : DEBUFF_ADDED_OTHER)
                            : (self ? DEBUFF_REMOVED_SELF : DEBUFF_REMOVED_OTHER);

    int luaSlot = luaSlotFromAuraSlot(f, slot);
    bool hidden = hiddenAura(f, slot, spellId, state);
    int eventLuaSlot = hidden ? 0 : luaSlot;
    unsigned long stacks = (state == 1) ? (unsigned long)f->auraApplications[slot]
                                        : (unsigned long)f->auraApplications[slot] + 1UL;
    unsigned long level = (unsigned long)f->auraLevels[slot];

    // AURA5-D1 closeout: keep caster attribution lifetime synchronized with
    // the already-established native Aura delta. No new hook/event is added.
    // This runs before SignalEventParam so Lua queries observe the new binding
    // (or the cleared REMOVE state) synchronously.
    if (state == 0) TysAuraCaster::onAuraAdded(ug, spellId, slot);
    else if (state == 1) TysAuraCaster::onAuraRemoved(ug, spellId, slot);
    else if (state == 2) TysAuraCaster::onAuraStackChanged(ug, spellId, slot);

    char guid[19] = {};
    guidToString(self && pg ? pg : ug, guid);
    static char format[] = "%s%d%d%d%d%d%d";
    ((SignalEventParamFn)WoW112::SIGNAL_EVENT_PARAM)(eventId, format,
        guid, eventLuaSlot, spellId, stacks, level, slot, state);
    observeEvent(eventId, ug, spellId, slot, eventLuaSlot, stacks, state);
}

static void __fastcall auraRemovedHook(void* unit, void* dummyEdx, unsigned long slot, unsigned long spellId) {
    if (g_nextAuraRemoved) g_nextAuraRemoved(unit, dummyEdx, slot, spellId);
    triggerAuraEvent(unit, slot, spellId, false, 1);
}

static void __fastcall auraAddedHook(void* unit, void* dummyEdx, unsigned long slot, unsigned long spellId) {
    if (g_nextAuraAdded) g_nextAuraAdded(unit, dummyEdx, slot, spellId);
    triggerAuraEvent(unit, slot, spellId, true, 0);
}

static void __fastcall auraStacksHook(void* unit, void* dummyEdx, int slot, unsigned char stackCount) {
    if (!unit || slot < 0 || slot >= (int)MAX_AURA_SLOTS) {
        if (slot < 0 || slot >= (int)MAX_AURA_SLOTS) ++g_invalidSlot;
        if (g_nextAuraStacks) g_nextAuraStacks(unit, dummyEdx, slot, stackCount);
        return;
    }
    UnitFieldsAuraView* f = unitFields(unit);
    if (!f) ++g_invalidFields;
    unsigned long spellId = f ? f->aura[slot] : 0;
    unsigned char currentStacks = f ? f->auraApplications[slot] : 0;
    if (g_nextAuraStacks) g_nextAuraStacks(unit, dummyEdx, slot, stackCount);
    // Preserve the established TYS direction semantic: *_ADDED_* for stack gain,
    // *_REMOVED_* for stack loss, with state=2 for both.
    bool wasAdded = stackCount < currentStacks;
    if (spellId) triggerAuraEvent(unit, (unsigned long)slot, spellId, wasAdded, 2);
}

static bool createAndEnable(void* target, void* hook, void** original, bool* created) {
    if (!target || !hook || !original || !created) return false;
    MH_STATUS cr = MH_CreateHook(target, hook, original);
    if (cr != MH_OK) return false;
    *created = true;
    MH_STATUS en = MH_EnableHook(target);
    return en == MH_OK || en == MH_ERROR_ENABLED;
}

static void rollbackNativeHooks() {
    InterlockedExchange(&g_emitEnabled, 0);
    InterlockedExchange(&g_eventsReady, 0);
    InterlockedExchange(&g_nativeHooks, 0);
    if (g_createdAuraStacks) { MH_DisableHook((void*)WoW112::CGUNIT_ON_AURA_STACKS_CHANGED); MH_RemoveHook((void*)WoW112::CGUNIT_ON_AURA_STACKS_CHANGED); g_createdAuraStacks=false; g_nextAuraStacks=0; }
    if (g_createdAuraRemoved) { MH_DisableHook((void*)WoW112::CGUNIT_ON_AURA_REMOVED); MH_RemoveHook((void*)WoW112::CGUNIT_ON_AURA_REMOVED); g_createdAuraRemoved=false; g_nextAuraRemoved=0; }
    if (g_createdAuraAdded) { MH_DisableHook((void*)WoW112::CGUNIT_ON_AURA_ADDED); MH_RemoveHook((void*)WoW112::CGUNIT_ON_AURA_ADDED); g_createdAuraAdded=false; g_nextAuraAdded=0; }
    if (g_createdCreateEvents) { MH_DisableHook((void*)WoW112::FRAMESCRIPT_CREATE_EVENTS); MH_RemoveHook((void*)WoW112::FRAMESCRIPT_CREATE_EVENTS); g_createdCreateEvents=false; g_nextCreateEvents=0; }
    if (g_createdSetEventCount) { MH_DisableHook((void*)WoW112::FRAMESCRIPT_SET_EVENT_COUNT); MH_RemoveHook((void*)WoW112::FRAMESCRIPT_SET_EVENT_COUNT); g_createdSetEventCount=false; g_nextSetEventCount=0; }
}

static bool installNativeHooks() {
    if (!executable(WoW112::FRAMESCRIPT_CREATE_EVENTS) ||
        !executable(WoW112::FRAMESCRIPT_SET_EVENT_COUNT) ||
        !executable(WoW112::CGUNIT_ON_AURA_ADDED) ||
        !executable(WoW112::CGUNIT_ON_AURA_REMOVED) ||
        !executable(WoW112::CGUNIT_ON_AURA_STACKS_CHANGED) ||
        !executable(WoW112::SIGNAL_EVENT_PARAM) ||
        !executable(WoW112::SSTR_DUP_A)) {
        copyText(g_status, sizeof(g_status), "AURA_ADDRESS_PREFLIGHT_FAILED");
        return false;
    }

    // D4-R1 owns one native Aura path. Existing detours are diagnostic-only;
    // there is no provider arbitration or alternate backend.

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        copyText(g_status, sizeof(g_status), "MH_INIT_FAILED");
        return false;
    }

    // Create all hooks first. If creation fails, do not claim a working backend.
    if (!createAndEnable((void*)WoW112::FRAMESCRIPT_SET_EVENT_COUNT, (void*)&setEventCountHook,
                         (void**)&g_nextSetEventCount, &g_createdSetEventCount)) {
        copyText(g_status, sizeof(g_status), "HOOK_SET_EVENT_COUNT_FAILED"); rollbackNativeHooks(); return false;
    }
    if (!createAndEnable((void*)WoW112::FRAMESCRIPT_CREATE_EVENTS, (void*)&createEventsHook,
                         (void**)&g_nextCreateEvents, &g_createdCreateEvents)) {
        copyText(g_status, sizeof(g_status), "HOOK_CREATE_EVENTS_FAILED"); rollbackNativeHooks(); return false;
    }
    if (!createAndEnable((void*)WoW112::CGUNIT_ON_AURA_ADDED, (void*)&auraAddedHook,
                         (void**)&g_nextAuraAdded, &g_createdAuraAdded)) {
        copyText(g_status, sizeof(g_status), "HOOK_AURA_ADDED_FAILED"); rollbackNativeHooks(); return false;
    }
    if (!createAndEnable((void*)WoW112::CGUNIT_ON_AURA_REMOVED, (void*)&auraRemovedHook,
                         (void**)&g_nextAuraRemoved, &g_createdAuraRemoved)) {
        copyText(g_status, sizeof(g_status), "HOOK_AURA_REMOVED_FAILED"); rollbackNativeHooks(); return false;
    }
    if (!createAndEnable((void*)WoW112::CGUNIT_ON_AURA_STACKS_CHANGED, (void*)&auraStacksHook,
                         (void**)&g_nextAuraStacks, &g_createdAuraStacks)) {
        copyText(g_status, sizeof(g_status), "HOOK_AURA_STACKS_FAILED"); rollbackNativeHooks(); return false;
    }

    InterlockedExchange(&g_nativeHooks, 1);
    copyText(g_backend, sizeof(g_backend), "TYS_NATIVE");
    copyText(g_status, sizeof(g_status), "HOOKS_INSTALLED_WAITING_EVENT_TABLE");
    return true;
}

} // namespace

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0)
        return InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0;
    if (!installNativeHooks()) {
        copyText(g_backend, sizeof(g_backend), "ERROR");
        return false;
    }
    return true;
}

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    Lua50::PushBool(L, InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0);
    Lua50::PushString(L, g_backend);
    Lua50::PushString(L, g_status);
    Lua50::PushBool(L, InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0);
    Lua50::PushBool(L, eventsReady());
    Lua50::PushString(L, "TYS_AURA_EVENT_V1");
    return 6;
}

int dispatchDiagnostics(Lua50::State L) {
    if (!L) return 0;

    bool native = InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0;
    char oa[96]={}, orr[96]={}, os[96]={}, oce[96]={}, osc[96]={};
    hookOwners(oa, orr, os, oce, osc);
    int registry = eventRegistryState();
    const char* integrity = "ERROR";
    if (native) integrity = registry == 1 ? "PASS_TYS_NATIVE" : (registry == 2 ? "EVENT_ID_CONFLICT" : "WAITING_EVENT_TABLE");
    else if (!sameText(g_backend, "ERROR")) integrity = "NOT_INITIALIZED";

    char lastGuid[19] = {};
    guidToString(g_lastGuid, lastGuid);
    unsigned long age = g_lastEventTick ? (GetTickCount() - g_lastEventTick) : 0;

    Lua50::NewTable(L);
    setTableString(L, "backend", g_backend);
    setTableString(L, "status", g_status);
    setTableString(L, "integrity", integrity);
    setTableString(L, "eventApi", "TYS_AURA_EVENT_V1");
    setTableString(L, "arbitration", "NONE_NATIVE_ONLY");
    setTableBool(L, "hookOwnershipAdvisory", true);
    setTableBool(L, "nativeHooksInstalled", native);
    setTableBool(L, "emitEnabled", InterlockedCompareExchange(&g_emitEnabled, 0, 0) != 0);
    setTableBool(L, "eventsReady", registry == 1);
    setTableString(L, "eventRegistry", registry == 1 ? "READY_TYS" : (registry == 2 ? "CONFLICT" : "NOT_READY"));


    setTableString(L, "ownerAuraAdded", oa);
    setTableString(L, "ownerAuraRemoved", orr);
    setTableString(L, "ownerAuraStack", os);
    setTableString(L, "ownerCreateEvents", oce);
    setTableString(L, "ownerSetEventCount", osc);

    setTableNumber(L, "nativeAdded", (double)g_countAdded);
    setTableNumber(L, "nativeRemoved", (double)g_countRemoved);
    setTableNumber(L, "nativeStack", (double)g_countStack);
    setTableNumber(L, "invalidSlot", (double)g_invalidSlot);
    setTableNumber(L, "invalidGuid", (double)g_invalidGuid);
    setTableNumber(L, "invalidFields", (double)g_invalidFields);
    setTableNumber(L, "duplicateRemoveSuspect", (double)g_duplicateRemoveSuspect);

    setTableNumber(L, "snapshotCalls", (double)g_snapshotCalls);
    setTableNumber(L, "snapshotSuccess", (double)g_snapshotSuccess);
    setTableNumber(L, "snapshotUnavailable", (double)g_snapshotUnavailable);
    setTableNumber(L, "snapshotGeneration", (double)g_snapshotGeneration);
    setTableString(L, "lastSnapshotUnit", g_lastSnapshotUnit);
    char lastSnapshotGuid[19] = {};
    guidToString(g_lastSnapshotGuid, lastSnapshotGuid);
    setTableString(L, "lastSnapshotGuid", g_lastSnapshotGuid ? lastSnapshotGuid : "");
    setTableNumber(L, "lastSnapshotCount", (double)g_lastSnapshotCount);
    setTableNumber(L, "lastSnapshotAgeMs", (double)(g_lastSnapshotTick ? (GetTickCount() - g_lastSnapshotTick) : 0));

    setTableString(L, "lastEvent", g_lastEventName);
    setTableString(L, "lastGuid", g_lastGuid ? lastGuid : "");
    setTableNumber(L, "lastSpellId", (double)g_lastSpellId);
    setTableNumber(L, "lastRawSlot", (double)g_lastRawSlot);
    setTableNumber(L, "lastLuaSlot", (double)g_lastLuaSlot);
    setTableNumber(L, "lastStacks", (double)g_lastStacks);
    setTableNumber(L, "lastState", (double)g_lastState);
    setTableNumber(L, "lastAgeMs", (double)age);
    return 1;
}

int dispatchSnapshot(Lua50::State L) {
    if (!L) return 0;
    ++g_snapshotCalls;

    const char* token = "target";
    if (Lua50::GetTop(L) >= 2 && Lua50::IsString(L, 2)) token = Lua50::ToString(L, 2);
    if (!token || (!sameText(token, "target") && !sameText(token, "player"))) {
        ++g_snapshotUnavailable;
        Lua50::PushNil(L); Lua50::PushString(L, "BAD_UNIT"); return 2;
    }
    if (!executable(WoW112::RESOLVE_UNIT_TOKEN)) {
        ++g_snapshotUnavailable;
        Lua50::PushNil(L); Lua50::PushString(L, "RESOLVE_UNIT_UNAVAILABLE"); return 2;
    }

    void* unit = ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)(token);
    if (!unit) {
        ++g_snapshotUnavailable;
        Lua50::PushNil(L); Lua50::PushString(L, "UNIT_UNAVAILABLE"); return 2;
    }
    UnitFieldsAuraView* f = unitFields(unit);
    if (!f) {
        ++g_snapshotUnavailable;
        Lua50::PushNil(L); Lua50::PushString(L, "UNIT_FIELDS_UNAVAILABLE"); return 2;
    }
    unsigned long long guid = unitGuid(unit);
    if (!guid) {
        ++g_snapshotUnavailable;
        Lua50::PushNil(L); Lua50::PushString(L, "GUID_UNAVAILABLE"); return 2;
    }

    char guidStr[19] = {};
    guidToString(guid, guidStr);
    Lua50::NewTable(L);
    setTableString(L, "unit", token);
    setTableString(L, "guid", guidStr);
    setTableString(L, "backend", g_backend);
    setTableNumber(L, "capturedAtMs", (double)GetTickCount());
    setTableNumber(L, "generation", (double)(++g_snapshotGeneration));
    setTableNumber(L, "slotCapacity", 48.0);
    setTableNumber(L, "buffSlotCapacity", 32.0);
    setTableNumber(L, "debuffSlotCapacity", 16.0);

    int row = 0;
    int visibleCount = 0;
    int hiddenCount = 0;
    int buffCount = 0;
    int debuffCount = 0;
    int visibleBuffCount = 0;
    int visibleDebuffCount = 0;
    for (unsigned long slot = 0; slot < MAX_AURA_SLOTS; ++slot) {
        unsigned long spellId = f->aura[slot];
        if (!spellId) continue;

        bool hidden = countsEmptyForLua(f, slot);
        int luaSlot = hidden ? 0 : luaSlotFromAuraSlot(f, slot);
        unsigned long stacks = (unsigned long)f->auraApplications[slot] + 1UL;
        unsigned long level = (unsigned long)f->auraLevels[slot];
        bool buff = slot < 32;
        if (buff) {
            ++buffCount;
            if (!hidden) ++visibleBuffCount;
        } else {
            ++debuffCount;
            if (!hidden) ++visibleDebuffCount;
        }
        if (hidden) ++hiddenCount; else ++visibleCount;

        ++row;
        Lua50::PushNumber(L, (double)row);
        Lua50::NewTable(L);
        setTableNumber(L, "spellId", (double)spellId);
        setTableNumber(L, "rawSlot", (double)slot);
        setTableNumber(L, "luaSlot", (double)luaSlot);
        setTableNumber(L, "stacks", (double)stacks);
        setTableNumber(L, "auraLevel", (double)level);
        setTableBool(L, "isBuff", buff);
        setTableBool(L, "hidden", hidden);
        setTableString(L, "type", buff ? "BUFF" : "DEBUFF");
        Lua50::SetTable(L, -3);
    }

    setTableNumber(L, "count", (double)row);
    setTableNumber(L, "visibleCount", (double)visibleCount);
    setTableNumber(L, "hiddenCount", (double)hiddenCount);
    setTableNumber(L, "buffCount", (double)buffCount);
    setTableNumber(L, "debuffCount", (double)debuffCount);
    setTableNumber(L, "visibleBuffCount", (double)visibleBuffCount);
    setTableNumber(L, "visibleDebuffCount", (double)visibleDebuffCount);

    ++g_snapshotSuccess;
    g_lastSnapshotTick = GetTickCount();
    g_lastSnapshotGuid = guid;
    g_lastSnapshotCount = (unsigned long)row;
    copyText(g_lastSnapshotUnit, sizeof(g_lastSnapshotUnit), token);
    return 1;
}

const char* backend() { return g_backend; }
const char* status() { return g_status; }
bool nativeHooksInstalled() { return InterlockedCompareExchange(&g_nativeHooks, 0, 0) != 0; }
bool eventsReady() {
    int state = eventRegistryState();
    if (state == 1) return true;
    return InterlockedCompareExchange(&g_eventsReady, 0, 0) != 0;
}

} // namespace TysAuraNative
