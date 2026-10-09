/*
 * TaiYangShenDian AURA6-D4-R2 Unified Aura State Service.
 *
 * Design:
 *   - UnitFields is the single authority for Aura existence and slot contents.
 *   - AuraSourceCore supplies caster/isMine attribution from one native instance cache.
 *   - The active-player CGBuffBar expiration array supplies exact remaining
 *     time only when a real expiration timestamp exists.
 *   - Target exact duration remains UNKNOWN in the standard 1.12 client.
 *   - No OnUpdate, worker thread, periodic scan, ObjectManager poll, or extra
 *     Aura hook is installed by this module.
 *
 * The tiny delta table below is observational metadata only. It records the
 * most recent ADD/STACK seen for a concrete GUID+rawSlot+spellId so unified
 * queries can expose lifecycle provenance. It is never consulted to decide
 * whether an Aura currently exists.
 */

#include <windows.h>
#include "aura_state.h"
#include "aura_native.h"
#include "aura_source_core.h"
#include "wow112_offsets.h"

namespace TysAuraState {
namespace {

constexpr unsigned long MAX_AURA_SLOTS = 48UL;
constexpr unsigned long UNIT_GUID_OFF = 0x30UL;
constexpr unsigned long UNIT_FIELDS_PTR_OFF = 0x110UL;
constexpr unsigned long DELTA_CELL_COUNT = 512UL;
constexpr unsigned long DELTA_CELL_MASK = DELTA_CELL_COUNT - 1UL;
constexpr unsigned long SPELL_AURA_TRACK_CREATURES = 44UL;
constexpr unsigned long SPELL_AURA_TRACK_RESOURCES = 45UL;
constexpr unsigned long SPELL_AURA_TRACK_STEALTHED = 151UL;
constexpr unsigned long SPELL_ATTR_HIDDEN_CLIENTSIDE = 0x00000080UL;
constexpr unsigned long SPELL_ATTR_EX_NO_AURA_ICON = 0x10000000UL;

#pragma pack(push, 1)
struct UnitFieldsAuraView {
    unsigned char preAura[0xA4];
    unsigned long aura[48];
    unsigned long auraFlags[6];
    unsigned char auraLevels[48];
    unsigned char auraApplications[48];
};

// Exact SpellRec prefix through EffectApplyAuraName[], matching the existing
// AURA3 native visibility rules and the established hidden-aura behavior.
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
    unsigned int EquippedItemSubClassMask;
    unsigned int EquippedItemInventoryTypeMask;
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

struct DeltaCell {
    unsigned long long guid;
    unsigned long spellId;
    unsigned long rawSlot;
    unsigned long state;
    unsigned long tick;
    unsigned long generation;
};

struct UnitView {
    const char* token;
    void* object;
    UnitFieldsAuraView* fields;
    unsigned long long guid;
};

struct RowSummary {
    bool casterKnown;
    bool exactTimer;
    bool predictedTimer;
};

using ResolveUnitFn = void* (__fastcall *)(const char* token);
using GetActivePlayerFn = unsigned long long (__stdcall *)();
using GetTimeMsFn = unsigned long long (__stdcall *)();

static volatile LONG g_initOnce = 0;
static volatile LONG g_observeCount = 0;
static volatile LONG g_addCount = 0;
static volatile LONG g_removeCount = 0;
static volatile LONG g_stackCount = 0;
static volatile LONG g_queryCount = 0;
static volatile LONG g_listCount = 0;
static volatile LONG g_getCount = 0;
static volatile LONG g_queryUnavailable = 0;
static volatile LONG g_generation = 0;
static volatile LONG g_worldResetCount = 0;
static char g_status[96] = "NOT_INITIALIZED";
static DeltaCell g_deltaCells[DELTA_CELL_COUNT] = {};

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i = 0;
    if (s) for (; s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
}

static bool sameTextNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        if (!ca) return true;
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

static char hexDigit(unsigned v) { return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10))); }
static void guidString(char* out, unsigned cap, unsigned long long guid) {
    if (!out || cap < 19) return;
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        unsigned shift = (unsigned)(15 - i) * 4U;
        out[2 + i] = hexDigit((unsigned)((guid >> shift) & 0xFULL));
    }
    out[18] = 0;
}

static unsigned long long activePlayerGuid() {
    if (!executable(WoW112::GET_ACTIVE_PLAYER_GUID)) return 0;
    return ((GetActivePlayerFn)WoW112::GET_ACTIVE_PLAYER_GUID)();
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

static bool resolveUnit(const char* token, UnitView* out) {
    if (!out || !token || (!sameTextNoCase(token, "player") && !sameTextNoCase(token, "target")) ||
        !executable(WoW112::RESOLVE_UNIT_TOKEN)) return false;
    void* object = ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)(token);
    if (!object || !readable((unsigned char*)object + UNIT_GUID_OFF, 8) ||
        !readable((unsigned char*)object + UNIT_FIELDS_PTR_OFF, 4)) return false;
    unsigned long long guid = *(unsigned long long*)((unsigned char*)object + UNIT_GUID_OFF);
    UnitFieldsAuraView* fields = *(UnitFieldsAuraView**)((unsigned char*)object + UNIT_FIELDS_PTR_OFF);
    if (!guid || !fields || !readable(fields, sizeof(UnitFieldsAuraView))) return false;
    out->token = token; out->object = object; out->fields = fields; out->guid = guid;
    return true;
}

static const SpellRecPrefix* spellRec(unsigned long spellId) {
    if (!spellId || !readable((void*)WoW112::SPELL_DB, sizeof(SpellDbView))) return 0;
    SpellDbView* db = (SpellDbView*)WoW112::SPELL_DB;
    if (!db->recordsById || spellId > db->maxId || !readable(db->recordsById + spellId, 4)) return 0;
    SpellRecPrefix* rec = db->recordsById[spellId];
    return rec && readable(rec, sizeof(SpellRecPrefix)) ? rec : 0;
}

static const char* spellName(unsigned long spellId) {
    const SpellRecPrefix* rec = spellRec(spellId);
    if (!rec || !readable((const unsigned char*)rec + WoW112::OFF_SPELL_NAMES, 9UL * 4UL) ||
        !readable((void*)WoW112::LOCALE_INDEX, 4)) return "";
    int locale = *(int*)WoW112::LOCALE_INDEX;
    if (locale < 0 || locale > 8) locale = 0;
    const char* const* names = (const char* const*)((const unsigned char*)rec + WoW112::OFF_SPELL_NAMES);
    const char* name = names[locale];
    return name ? name : "";
}

static bool trackingAura(const SpellRecPrefix* r) {
    if (!r) return false;
    for (int i = 0; i < 3; ++i) {
        unsigned long a = r->EffectApplyAuraName[i];
        if (a == SPELL_AURA_TRACK_CREATURES || a == SPELL_AURA_TRACK_RESOURCES ||
            a == SPELL_AURA_TRACK_STEALTHED) return true;
    }
    return false;
}

static bool hiddenForLua(unsigned long spellId) {
    if (!spellId) return true;
    const SpellRecPrefix* r = spellRec(spellId);
    if (!r) return false; // fail-open, preserving the established native path
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

// Turtle's polarity nibble differs from stock 1.12 effect-index flags.
// Enable the interpretation only when the in-world client exposes its marker;
// invalid nibble encodings always fall back to legacy slot polarity.
static bool turtleAuraPolarity(Lua50::State L) {
    if (!L) return false;
    const int top = Lua50::GetTop(L);
    Lua50::PushString(L, "TURTLE_WOW_VERSION");
    Lua50::GetTable(L, WoW112::LUA_GLOBALSINDEX);
    const bool enabled = Lua50::IsString(L, -1);
    Lua50::SetTop(L, top);
    return enabled;
}

static bool auraHarmful(const UnitFieldsAuraView* f, unsigned long slot, bool turtle) {
    const bool stock = slot >= 32UL;
    if (!turtle || !f || slot >= MAX_AURA_SLOTS) return stock;
    const unsigned char packed = ((const unsigned char*)f->auraFlags)[slot / 2UL];
    const unsigned char nibble = (unsigned char)((packed >> ((slot & 1UL) * 4UL)) & 0xFUL);
    if (nibble == 0x04U || nibble == 0x06U) return false;
    if (nibble == 0x08U) return true;
    return stock; // unknown layout: do not manufacture a polarity
}

static bool countsEmptyForLua(const UnitFieldsAuraView* f, unsigned long slot) {
    if (!f || slot >= MAX_AURA_SLOTS) return true;
    unsigned long id = f->aura[slot];
    if (!id || hiddenForLua(id)) return true;
    return !visiblePackedFlag(f, slot);
}

static int luaSlotFromAuraSlot(const UnitFieldsAuraView* f, unsigned long slot) {
    bool buff = slot < 32;
    int luaSlot = (int)slot + 1;
    if (!buff) luaSlot -= 32;
    unsigned long first = buff ? 0UL : 32UL;
    int empty = 0;
    for (unsigned long i = first; i < slot; ++i) if (countsEmptyForLua(f, i)) ++empty;
    return luaSlot - empty;
}

static const char* casterSourceName(unsigned char sourceKind) {
    if (sourceKind == TysAuraSourceCore::SOURCE_NATIVE_HIT_TARGET) return "SPELL_GO_HIT_TARGET_CORRELATED";
    if (sourceKind == TysAuraSourceCore::SOURCE_NATIVE_CASTER_SELF) return "SPELL_GO_CASTER_SELF_FALLBACK";
    return "APPLICATION_NOT_OBSERVED";
}

static const char* casterQualityName(unsigned char sourceKind) {
    if (sourceKind == TysAuraSourceCore::SOURCE_NATIVE_HIT_TARGET) return "OBSERVED_TARGET_HIT";
    if (sourceKind == TysAuraSourceCore::SOURCE_NATIVE_CASTER_SELF) return "OBSERVED_CASTER_CAST_FALLBACK";
    return "UNKNOWN";
}

static const char* casterResolutionName(TysAuraSourceCore::Resolution resolution) {
    if (resolution == TysAuraSourceCore::RESOLUTION_EXISTING_INSTANCE) return "EXISTING_INSTANCE";
    if (resolution == TysAuraSourceCore::RESOLUTION_FIFO_PENDING) return "FIFO_PENDING_CORRELATED";
    if (resolution == TysAuraSourceCore::RESOLUTION_RETAINED_INSTANCE) return "RETAINED_INSTANCE_REBOUND";
    if (resolution == TysAuraSourceCore::RESOLUTION_REMOVED) return "REMOVED";
    return "UNKNOWN";
}

static const char* predictedTimeSourceName(unsigned char source) {
    if (source == TysAuraSourceCore::TIME_SOURCE_LOCAL_CAST_MODIFIED)
        return "LOCAL_CAST_MODIFIED";
    if (source == TysAuraSourceCore::TIME_SOURCE_LOCAL_COMBO_SCALED)
        return "LOCAL_COMBO_SCALED";
    if (source == TysAuraSourceCore::TIME_SOURCE_REMOTE_BASE)
        return "REMOTE_BASE";
    return "UNKNOWN";
}

static unsigned long hashDelta(unsigned long long guid, unsigned long spellId, unsigned long rawSlot) {
    unsigned long lo = (unsigned long)(guid & 0xffffffffULL);
    unsigned long hi = (unsigned long)((guid >> 32) & 0xffffffffULL);
    unsigned long h = lo ^ (hi * 0x9E3779B9UL) ^ (spellId * 0x85EBCA6BUL) ^ (rawSlot * 0xC2B2AE35UL);
    h ^= h >> 16;
    return h & DELTA_CELL_MASK;
}

static bool lookupDelta(unsigned long long guid, unsigned long spellId, unsigned long rawSlot, DeltaCell* out) {
    unsigned long i = hashDelta(guid, spellId, rawSlot);
    const DeltaCell& c = g_deltaCells[i];
    if (c.guid != guid || c.spellId != spellId || c.rawSlot != rawSlot || c.state == TysAuraSourceCore::AURA_REMOVE)
        return false;
    if (out) *out = c;
    return true;
}

static const char* deltaName(unsigned long state) {
    if (state == TysAuraSourceCore::AURA_ADD) return "ADD";
    if (state == TysAuraSourceCore::AURA_STACK) return "STACK";
    if (state == TysAuraSourceCore::AURA_REMOVE) return "REMOVE";
    return "UNKNOWN";
}

static bool playerExactTimer(unsigned long rawSlot, unsigned long* expirationOut,
                             unsigned long* remainingOut, unsigned long now) {
    if (expirationOut) *expirationOut = 0;
    if (remainingOut) *remainingOut = 0;
    if (rawSlot >= MAX_AURA_SLOTS ||
        !readable((void*)WoW112::BUFFBAR_EXPIRATION_ARRAY, MAX_AURA_SLOTS * 4UL)) return false;
    unsigned long expiration = ((unsigned long*)WoW112::BUFFBAR_EXPIRATION_ARRAY)[rawSlot];
    unsigned long remain = remainingMs(expiration, now);
    if (expirationOut) *expirationOut = expiration;
    if (remainingOut) *remainingOut = remain;
    return expiration != 0 && remain != 0;
}

static RowSummary pushAuraRow(Lua50::State L, const UnitView& u, unsigned long rawSlot,
                                unsigned long queryTick, unsigned long clockNow,
                                unsigned long long playerGuid, bool turtle) {
    UnitFieldsAuraView* f = u.fields;
    unsigned long spellId = f->aura[rawSlot];
    bool buff = !auraHarmful(f, rawSlot, turtle);
    bool hidden = countsEmptyForLua(f, rawSlot);
    // Stock Lua UnitBuff/UnitDebuff are physically range-based; a Turtle
    // spilled debuff has no corresponding vanilla Lua slot.
    int luaSlot = (hidden || buff != (rawSlot < 32UL)) ? 0 : luaSlotFromAuraSlot(f, rawSlot);
    unsigned long stacks = (unsigned long)f->auraApplications[rawSlot] + 1UL;
    unsigned long auraLevel = (unsigned long)f->auraLevels[rawSlot];

    Lua50::NewTable(L);
    char guidText[20] = {}; guidString(guidText, sizeof(guidText), u.guid);
    setString(L, "unit", u.token);
    setString(L, "guid", guidText);
    setString(L, "backend", TysAuraNative::backend());
    setNumber(L, "capturedAtMs", (double)queryTick);
    setBool(L, "present", true);
    setString(L, "state", "PRESENT");
    setNumber(L, "spellId", (double)spellId);
    setString(L, "name", spellName(spellId));
    setNumber(L, "rawSlot", (double)rawSlot);
    setNumber(L, "luaSlot", (double)luaSlot);
    setNumber(L, "stacks", (double)stacks);
    setNumber(L, "auraLevel", (double)auraLevel);
    setBool(L, "isBuff", buff);
    setBool(L, "isDebuff", !buff);
    setBool(L, "spilled", buff != (rawSlot < 32UL));
    setString(L, "type", buff ? "BUFF" : "DEBUFF");
    setBool(L, "hidden", hidden);

    TysAuraSourceCore::InstanceView binding = {};
    TysAuraSourceCore::Resolution resolution = TysAuraSourceCore::RESOLUTION_NONE;
    bool casterKnown = TysAuraSourceCore::match(u.guid, spellId, rawSlot, queryTick, &binding, &resolution);
    setBool(L, "casterKnown", casterKnown);
    if (casterKnown) {
        char casterText[20] = {}; guidString(casterText, sizeof(casterText), binding.casterGuid);
        bool mine = binding.casterGuid != 0 && binding.casterGuid == playerGuid;
        setString(L, "casterGuid", casterText);
        setBool(L, "isMine", mine);
        setBool(L, "isOther", !mine);
        setString(L, "casterQuality", casterQualityName(binding.sourceKind));
        setString(L, "casterSource", casterSourceName(binding.sourceKind));
        setString(L, "casterResolution", casterResolutionName(resolution));
        setNumber(L, "casterAgeMs", (double)(queryTick - binding.tick));
    } else {
        setString(L, "casterGuid", "");
        setBool(L, "isMine", false);
        setBool(L, "isOther", false);
        setString(L, "casterQuality", "UNKNOWN");
        setString(L, "casterSource", "APPLICATION_NOT_OBSERVED");
        setString(L, "casterResolution", "UNKNOWN");
        setNumber(L, "casterAgeMs", 0.0);
    }

    unsigned long duration = 0, expiration = 0, remain = 0;
    bool durationKnown = false, hasTimer = false, predictedTimer = false;
    bool isPlayer = u.guid == playerGuid;
    bool exactTimer = isPlayer && playerExactTimer(rawSlot, &expiration, &remain, clockNow);

    if (exactTimer) {
        hasTimer = true;
        setString(L, "timeQuality", "EXACT");
        setString(L, "timeSource", "SELF_CLIENT_BUFFBAR_EXPIRATION");
    } else if (isPlayer) {
        setString(L, "timeQuality", "UNKNOWN");
        setString(L, "timeSource", "SELF_NO_TIMED_VALUE");
    } else if (casterKnown && binding.timeQuality == TysAuraSourceCore::TIME_PREDICTED &&
               binding.durationMs != 0 && binding.expirationMs != 0) {
        // AuraSourceCore stores predicted expiration in GetTickCount epoch.
        // Expose expiration in the client's GetTime/async-clock epoch without
        // mixing epochs: clientNow + remaining.
        unsigned long predictedRemain = remainingMs(binding.expirationMs, queryTick);
        duration = binding.durationMs;
        durationKnown = true;
        if (predictedRemain != 0) {
            remain = predictedRemain;
            expiration = clockNow + predictedRemain;
            hasTimer = true;
            predictedTimer = true;
            setString(L, "timeQuality", "PREDICTED");
            setString(L, "timeSource", predictedTimeSourceName(binding.timeSource));
        } else {
            // UnitFields still says the Aura exists, so an elapsed estimate
            // cannot be used as proof of removal. Preserve caster/existence and
            // degrade only the remaining-time quality.
            expiration = 0;
            remain = 0;
            setString(L, "timeQuality", "UNKNOWN");
            setString(L, "timeSource", "PREDICTED_EXPIRED_AURA_STILL_PRESENT");
        }
    } else {
        setString(L, "timeQuality", "UNKNOWN");
        setString(L, "timeSource", "TARGET_APPLICATION_TIME_NOT_OBSERVED");
    }

    setNumber(L, "durationMs", (double)duration);
    setBool(L, "durationKnown", durationKnown);
    setNumber(L, "expirationMs", (double)expiration);
    setNumber(L, "remainingMs", (double)remain);
    setBool(L, "hasTimer", hasTimer);

    DeltaCell delta = {};
    bool deltaKnown = lookupDelta(u.guid, spellId, rawSlot, &delta);
    setBool(L, "lastDeltaKnown", deltaKnown);
    if (deltaKnown) {
        setString(L, "lastDelta", deltaName(delta.state));
        setNumber(L, "lastDeltaAgeMs", (double)(queryTick - delta.tick));
        setNumber(L, "stateGeneration", (double)delta.generation);
    } else {
        setString(L, "lastDelta", "SNAPSHOT_ONLY");
        setNumber(L, "lastDeltaAgeMs", 0.0);
        setNumber(L, "stateGeneration", 0.0);
    }
    RowSummary summary = { casterKnown, exactTimer, predictedTimer };
    return summary;
}

static bool prepareUnit(const char* token, UnitView* u, const char** error) {
    if (!token || (!sameTextNoCase(token, "player") && !sameTextNoCase(token, "target"))) {
        if (error) *error = "UNIT_NOT_SUPPORTED";
        return false;
    }
    if (!resolveUnit(token, u)) {
        if (error) *error = "UNIT_UNAVAILABLE";
        return false;
    }
    // UnitFields remains slot-presence authority. AuraSourceCore preserves
    // cached identities across an all-empty visibility teardown and only
    // reconciles them when a populated descriptor provides evidence.
    TysAuraSourceCore::cleanupVisibleInstances(u->guid, u->fields->aura);
    return true;
}

} // namespace

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0) return true;
    for (unsigned long i = 0; i < DELTA_CELL_COUNT; ++i) g_deltaCells[i] = {};
    copyText(g_status, sizeof(g_status), "READY_UNIFIED_AURA_STATE_QUERY_SERVICE");
    return true;
}

void onWorldLeaving() {
    if (InterlockedCompareExchange(&g_initOnce, 0, 0) == 0) return;
    for (unsigned long i = 0; i < DELTA_CELL_COUNT; ++i) g_deltaCells[i] = {};
    ++g_generation;
    ++g_worldResetCount;
}

void observeAura(unsigned long long targetGuid, unsigned long spellId,
                 unsigned long rawSlot, unsigned long state, unsigned long nowMs) {
    if (!targetGuid || !spellId || rawSlot >= MAX_AURA_SLOTS || state > TysAuraSourceCore::AURA_STACK) return;
    ++g_observeCount;
    if (state == TysAuraSourceCore::AURA_ADD) ++g_addCount;
    else if (state == TysAuraSourceCore::AURA_REMOVE) ++g_removeCount;
    else if (state == TysAuraSourceCore::AURA_STACK) ++g_stackCount;
    unsigned long generation = (unsigned long)(++g_generation);
    unsigned long index = hashDelta(targetGuid, spellId, rawSlot);
    DeltaCell& c = g_deltaCells[index];
    c.guid = targetGuid; c.spellId = spellId; c.rawSlot = rawSlot;
    c.state = state; c.tick = nowMs; c.generation = generation;
}

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    Lua50::NewTable(L);
    setString(L, "version", "AURA6-D4-R4");
    setString(L, "status", g_status);
    setString(L, "backend", TysAuraNative::backend());
    setString(L, "architecture", "UNITFIELDS_AUTHORITY_PLUS_AURA_SOURCE_CORE_PLUS_EXACT_SELF_PLUS_PREDICTED_TARGET_TIME");
    setBool(L, "eventDriven", true);
    setBool(L, "backgroundPolling", false);
    setBool(L, "periodicAuraScan", false);
    setBool(L, "ownsAuraHook", false);
    setBool(L, "ownsTimerThread", false);
    setBool(L, "unitFieldsAuthoritative", true);
    setBool(L, "casterCoreShared", false);
    setBool(L, "auraSourceCoreShared", true);
    setString(L, "auraSourceIdentity", "TARGET_GUID_PLUS_SPELL_ID_PLUS_CASTER_GUID");
    setString(L, "auraSourceSeatPolicy", "FIFO");
    setBool(L, "selfExactRemainingAvailable", true);
    setBool(L, "targetExactRemainingAvailable", false);
    setBool(L, "targetPredictedRemainingAvailable", true);
    setString(L, "targetTimePolicy", "PREDICTED_WHEN_APPLICATION_OBSERVED; UNKNOWN_ON_MISS_OR_ELAPSED_ESTIMATE");
    setNumber(L, "serviceGeneration", (double)InterlockedCompareExchange(&g_generation, 0, 0));
    setNumber(L, "worldResetCount", (double)InterlockedCompareExchange(&g_worldResetCount, 0, 0));
    setNumber(L, "observedDeltaCount", (double)InterlockedCompareExchange(&g_observeCount, 0, 0));
    setNumber(L, "observedAddCount", (double)InterlockedCompareExchange(&g_addCount, 0, 0));
    setNumber(L, "observedRemoveCount", (double)InterlockedCompareExchange(&g_removeCount, 0, 0));
    setNumber(L, "observedStackCount", (double)InterlockedCompareExchange(&g_stackCount, 0, 0));
    setNumber(L, "queryCount", (double)InterlockedCompareExchange(&g_queryCount, 0, 0));
    setNumber(L, "listCount", (double)InterlockedCompareExchange(&g_listCount, 0, 0));
    setNumber(L, "getCount", (double)InterlockedCompareExchange(&g_getCount, 0, 0));
    setNumber(L, "queryUnavailable", (double)InterlockedCompareExchange(&g_queryUnavailable, 0, 0));
    setNumber(L, "slotCapacity", 48.0);
    setNumber(L, "deltaMetadataCells", (double)DELTA_CELL_COUNT);
    return 1;
}

int dispatchGet(Lua50::State L) {
    if (!L || Lua50::GetTop(L) < 3 || !Lua50::IsString(L, 2) || !Lua50::IsNumber(L, 3)) {
        if (L) { Lua50::PushNil(L); Lua50::PushString(L, "BAD_ARGUMENT"); }
        return L ? 2 : 0;
    }
    ++g_queryCount; ++g_getCount;
    const char* token = Lua50::ToString(L, 2);
    double slotNumber = Lua50::ToNumber(L, 3);
    if (!(slotNumber >= 0.0 && slotNumber < 48.0)) {
        Lua50::PushNil(L); Lua50::PushString(L, "RAWSLOT_OUT_OF_RANGE"); return 2;
    }
    unsigned long rawSlot = (unsigned long)slotNumber;
    if ((double)rawSlot != slotNumber) {
        Lua50::PushNil(L); Lua50::PushString(L, "BAD_ARGUMENT"); return 2;
    }
    UnitView u = {}; const char* error = 0;
    if (!prepareUnit(token, &u, &error)) {
        ++g_queryUnavailable; Lua50::PushNil(L); Lua50::PushString(L, error ? error : "UNIT_UNAVAILABLE"); return 2;
    }
    unsigned long spellId = u.fields->aura[rawSlot];
    if (!spellId) { Lua50::PushNil(L); Lua50::PushString(L, "AURA_SLOT_EMPTY"); return 2; }
    unsigned long queryTick = GetTickCount();
    unsigned long clockNow = clientNowMs();
    pushAuraRow(L, u, rawSlot, queryTick, clockNow, activePlayerGuid(), turtleAuraPolarity(L));
    return 1;
}

int dispatchList(Lua50::State L) {
    if (!L) return 0;
    ++g_queryCount; ++g_listCount;
    const char* token = "target";
    if (Lua50::GetTop(L) >= 2 && Lua50::IsString(L, 2)) token = Lua50::ToString(L, 2);
    UnitView u = {}; const char* error = 0;
    if (!prepareUnit(token, &u, &error)) {
        ++g_queryUnavailable; Lua50::PushNil(L); Lua50::PushString(L, error ? error : "UNIT_UNAVAILABLE"); return 2;
    }

    unsigned long queryTick = GetTickCount();
    unsigned long clockNow = clientNowMs();
    unsigned long long playerGuid = activePlayerGuid();
    const bool turtle = turtleAuraPolarity(L);
    char guidText[20] = {}; guidString(guidText, sizeof(guidText), u.guid);

    Lua50::NewTable(L);
    setString(L, "unit", token);
    setString(L, "guid", guidText);
    setString(L, "backend", TysAuraNative::backend());
    setString(L, "service", "UNIFIED_AURA_STATE");
    setNumber(L, "capturedAtMs", (double)queryTick);
    setNumber(L, "clockNowMs", (double)clockNow);
    setNumber(L, "serviceGeneration", (double)InterlockedCompareExchange(&g_generation, 0, 0));
    setNumber(L, "slotCapacity", 48.0);
    setNumber(L, "buffSlotCapacity", 32.0);
    setNumber(L, "debuffSlotCapacity", 16.0);

    unsigned long row = 0, visible = 0, hiddenCount = 0, buffs = 0, debuffs = 0;
    unsigned long casterKnown = 0, casterUnknown = 0, exactTimed = 0, predictedTimed = 0;
    for (unsigned long slot = 0; slot < MAX_AURA_SLOTS; ++slot) {
        unsigned long spellId = u.fields->aura[slot];
        if (!spellId) continue;
        bool hidden = countsEmptyForLua(u.fields, slot);
        if (hidden) ++hiddenCount; else ++visible;
        if (!auraHarmful(u.fields, slot, turtle)) ++buffs; else ++debuffs;

        ++row;
        Lua50::PushNumber(L, (double)row);
        RowSummary summary = pushAuraRow(L, u, slot, queryTick, clockNow, playerGuid, turtle);
        Lua50::SetTable(L, -3);
        if (summary.casterKnown) ++casterKnown; else ++casterUnknown;
        if (summary.exactTimer) ++exactTimed;
        if (summary.predictedTimer) ++predictedTimed;
    }

    setNumber(L, "count", (double)row);
    setNumber(L, "visibleCount", (double)visible);
    setNumber(L, "hiddenCount", (double)hiddenCount);
    setNumber(L, "buffCount", (double)buffs);
    setNumber(L, "debuffCount", (double)debuffs);
    setNumber(L, "casterKnownCount", (double)casterKnown);
    setNumber(L, "casterUnknownCount", (double)casterUnknown);
    setNumber(L, "exactTimedCount", (double)exactTimed);
    setNumber(L, "predictedTimedCount", (double)predictedTimed);
    setString(L, "timeQualityPolicy", u.guid == playerGuid
        ? "SELF_EXACT_WHEN_EXPIRATION_PRESENT"
        : "TARGET_PREDICTED_WHEN_CAST_EVIDENCE_EXISTS");
    return 1;
}

const char* status() { return g_status; }

} // namespace TysAuraState
