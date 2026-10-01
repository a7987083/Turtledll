#pragma once

namespace TysAuraSourceCore {

enum SourceKind : unsigned char {
    SOURCE_NATIVE_HIT_TARGET = 1,
    SOURCE_NATIVE_CASTER_SELF = 2
};

enum AuraState : unsigned long {
    AURA_ADD = 0,
    AURA_REMOVE = 1,
    AURA_STACK = 2
};

enum Resolution : unsigned char {
    RESOLUTION_NONE = 0,
    RESOLUTION_EXISTING_INSTANCE = 1,
    RESOLUTION_FIFO_PENDING = 2,
    RESOLUTION_RETAINED_INSTANCE = 3,
    RESOLUTION_REMOVED = 4
};

enum TimeQuality : unsigned char {
    TIME_UNKNOWN = 0,
    TIME_PREDICTED = 1,
    TIME_EXACT = 2
};

enum TimeSource : unsigned char {
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_LOCAL_CAST_MODIFIED = 1,
    TIME_SOURCE_LOCAL_COMBO_SCALED = 2,
    TIME_SOURCE_REMOTE_BASE = 3
};

struct InstanceView {
    unsigned long long targetGuid;
    unsigned long long casterGuid;
    unsigned long spellId;
    unsigned long rawSlot;
    unsigned long tick;
    unsigned long serial;
    unsigned long durationMs;
    unsigned long expirationMs;
    unsigned char sourceKind;
    unsigned char timeQuality;
    unsigned char timeSource;
    bool slotBound;
    bool valid;
};

struct ObserveResult {
    bool bound;
    Resolution resolution;
    unsigned long cleared;
    InstanceView instance;
};

struct Stats {
    unsigned long castObservations;
    unsigned long pendingWrites;
    unsigned long pendingConsumed;
    unsigned long pendingExpired;
    unsigned long pendingCapacityDrops;
    unsigned long fifoBindings;
    unsigned long instanceWrites;
    unsigned long instanceRefreshes;
    unsigned long instanceRebinds;
    unsigned long instanceClears;
    unsigned long instanceCapacityDrops;
    unsigned long removeHits;
    unsigned long removeMisses;
    unsigned long addPreclears;
    unsigned long existingInstanceReturns;
    unsigned long retainedInstanceReturns;
    unsigned long queryHits;
    unsigned long queryMisses;
    unsigned long descriptorReconciles;
    unsigned long descriptorEmptyPreserves;
    unsigned long descriptorUnbinds;
    unsigned long descriptorClears;
    unsigned long ambiguousRetainedMisses;
    unsigned long timedCastObservations;
    unsigned long castRefreshes;
    unsigned long refreshPendingWrites;
    unsigned long refreshBindings;
    unsigned long staleRemoveIgnores;
    unsigned long worldResets;
};

void initialize();

// Clear transient Aura source state at world teardown while preserving diagnostic
// counters. This prevents Pending/Instance identities from leaking across maps.
void resetWorldState();

// Every observed aura-applying cast becomes one short-lived application
// candidate. A candidate is consumed once; it can never bind sibling slots.
void observeCast(unsigned long long targetGuid,
                 unsigned long long casterGuid,
                 unsigned long spellId,
                 unsigned long now,
                 unsigned char sourceKind,
                 unsigned long durationMs = 0,
                 unsigned char timeSource = TIME_SOURCE_NONE);

// Native descriptor lifecycle. ADD consumes the oldest matching candidate
// (FIFO). STACK keeps a known slot owner and consumes only a matching-caster
// refresh candidate; it never lets another caster steal a bound sibling slot.
ObserveResult observeAura(unsigned long long targetGuid,
                          unsigned long spellId,
                          unsigned long rawSlot,
                          unsigned long state,
                          unsigned long now);

// Query is read-side only: it never consumes Pending. Exact slot identity wins;
// a sole retained instance may be rebound when there is no ambiguity.
bool match(unsigned long long targetGuid,
           unsigned long spellId,
           unsigned long rawSlot,
           unsigned long now,
           InstanceView* out,
           Resolution* resolutionOut);

// UnitFields remains presence authority. An all-empty descriptor is treated as
// a possible visibility teardown and preserves cached instances. A populated
// descriptor reconciles stale slots without guessing between multiple casters.
void cleanupVisibleInstances(unsigned long long targetGuid,
                             const unsigned long auraSpellIds[48]);

Stats stats();
unsigned long activeInstanceCount();
unsigned long activePendingCount(unsigned long now);

constexpr unsigned long correlationWindowMs() { return 3000UL; }
constexpr unsigned long maxAuraSlots() { return 48UL; }
constexpr unsigned long instanceCapacity() { return 2048UL; }
constexpr unsigned long pendingCapacity() { return 512UL; }

} // namespace TysAuraSourceCore
