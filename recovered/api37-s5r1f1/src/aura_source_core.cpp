/*
 * TaiYangShenDian AURA6-D4-R4 AuraSourceCore.
 *
 * One native-only source cache owns caster attribution now. There is no
 * provider switch, no external provider ingress, no separate historical-owner table and no
 * read-side Pending consumption. Identity follows the server-facing tuple
 * (targetGuid, spellId, casterGuid); rawSlot is a descriptor binding attached
 * when the application is observed.
 */

#include "aura_source_core.h"

namespace TysAuraSourceCore {
namespace {

constexpr unsigned long MAX_PENDING = 512UL;
constexpr unsigned long MAX_INSTANCES = 2048UL;
constexpr unsigned long MAX_AURA_SLOTS = 48UL;
constexpr unsigned long CORRELATION_WINDOW_MS = 3000UL;
constexpr unsigned long INVALID_SLOT = 0xFFFFFFFFUL;

struct PendingApplication {
    unsigned long long targetGuid;
    unsigned long long casterGuid;
    unsigned long spellId;
    unsigned long tick;
    unsigned long serial;
    unsigned long durationMs;
    unsigned long expirationMs;
    unsigned char sourceKind;
    unsigned char timeQuality;
    unsigned char timeSource;
    bool refreshOnly;
    bool valid;
};

struct AuraInstance {
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

static PendingApplication g_pending[MAX_PENDING] = {};
static AuraInstance g_instances[MAX_INSTANCES] = {};
static unsigned long g_pendingWrite = 0;
static unsigned long g_serial = 0;
static bool g_initialized = false;
static Stats g_stats = {};

static void zeroBytes(void* p, unsigned long n) {
    unsigned char* b = (unsigned char*)p;
    for (unsigned long i = 0; i < n; ++i) b[i] = 0;
}

static unsigned long ageMs(unsigned long then, unsigned long now) {
    return now - then;
}

static InstanceView toView(const AuraInstance& e) {
    InstanceView v = {};
    v.targetGuid = e.targetGuid;
    v.casterGuid = e.casterGuid;
    v.spellId = e.spellId;
    v.rawSlot = e.slotBound ? e.rawSlot : INVALID_SLOT;
    v.tick = e.tick;
    v.serial = e.serial;
    v.durationMs = e.durationMs;
    v.expirationMs = e.expirationMs;
    v.sourceKind = e.sourceKind;
    v.timeQuality = e.timeQuality;
    v.timeSource = e.timeSource;
    v.slotBound = e.slotBound;
    v.valid = e.valid;
    return v;
}

static void expirePending(unsigned long now) {
    for (unsigned long i = 0; i < MAX_PENDING; ++i) {
        PendingApplication& p = g_pending[i];
        if (!p.valid) continue;
        if (ageMs(p.tick, now) > CORRELATION_WINDOW_MS) {
            p.valid = false;
            ++g_stats.pendingExpired;
        }
    }
}

static PendingApplication* claimPendingSlot(unsigned long now) {
    expirePending(now);
    for (unsigned long pass = 0; pass < MAX_PENDING; ++pass) {
        unsigned long idx = (g_pendingWrite + pass) % MAX_PENDING;
        if (!g_pending[idx].valid) {
            g_pendingWrite = (idx + 1) % MAX_PENDING;
            return &g_pending[idx];
        }
    }
    ++g_stats.pendingCapacityDrops;
    return nullptr;
}

static void addPending(unsigned long long targetGuid, unsigned long long casterGuid,
                       unsigned long spellId, unsigned long now,
                       unsigned char sourceKind, unsigned long durationMs,
                       unsigned char timeSource, bool refreshOnly) {
    if (!targetGuid || !casterGuid || !spellId) return;
    PendingApplication* p = claimPendingSlot(now);
    if (!p) return;
    *p = {};
    p->targetGuid = targetGuid;
    p->casterGuid = casterGuid;
    p->spellId = spellId;
    p->tick = now;
    p->serial = ++g_serial;
    p->durationMs = durationMs;
    p->expirationMs = durationMs ? now + durationMs : 0;
    p->sourceKind = sourceKind;
    p->timeQuality = durationMs ? TIME_PREDICTED : TIME_UNKNOWN;
    p->timeSource = durationMs ? timeSource : TIME_SOURCE_NONE;
    p->refreshOnly = refreshOnly;
    p->valid = true;
    ++g_stats.pendingWrites;
    if (refreshOnly) ++g_stats.refreshPendingWrites;
}

static PendingApplication* findOldestPending(unsigned long long targetGuid,
                                              unsigned long spellId,
                                              unsigned long now) {
    expirePending(now);
    PendingApplication* best = nullptr;
    for (unsigned long i = 0; i < MAX_PENDING; ++i) {
        PendingApplication& p = g_pending[i];
        if (!p.valid || p.targetGuid != targetGuid || p.spellId != spellId) continue;
        if (!best || p.serial < best->serial) best = &p;
    }
    return best;
}

static PendingApplication* findOldestPendingForCaster(unsigned long long targetGuid,
                                                       unsigned long spellId,
                                                       unsigned long long casterGuid,
                                                       unsigned long now) {
    expirePending(now);
    PendingApplication* best = nullptr;
    for (unsigned long i = 0; i < MAX_PENDING; ++i) {
        PendingApplication& p = g_pending[i];
        if (!p.valid || p.targetGuid != targetGuid || p.spellId != spellId ||
            p.casterGuid != casterGuid) continue;
        if (!best || p.serial < best->serial) best = &p;
    }
    return best;
}

static void consumePending(PendingApplication* p) {
    if (!p || !p->valid) return;
    const unsigned long long target = p->targetGuid;
    const unsigned long long caster = p->casterGuid;
    const unsigned long spell = p->spellId;
    p->valid = false;
    ++g_stats.pendingConsumed;
    // One server Aura instance per (target, spell, caster). If duplicate cast
    // observations for the same identity accumulated before seating, they are
    // no longer eligible to manufacture sibling instances after this one binds.
    for (unsigned long i = 0; i < MAX_PENDING; ++i) {
        PendingApplication& q = g_pending[i];
        if (q.valid && q.targetGuid == target && q.casterGuid == caster &&
            q.spellId == spell) {
            q.valid = false;
            ++g_stats.pendingConsumed;
        }
    }
}

static AuraInstance* findByIdentity(unsigned long long targetGuid,
                                    unsigned long spellId,
                                    unsigned long long casterGuid) {
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (e.valid && e.targetGuid == targetGuid && e.spellId == spellId &&
            e.casterGuid == casterGuid) return &e;
    }
    return nullptr;
}

static AuraInstance* findBySlot(unsigned long long targetGuid,
                                unsigned long spellId,
                                unsigned long rawSlot) {
    if (rawSlot >= MAX_AURA_SLOTS) return nullptr;
    AuraInstance* found = nullptr;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (!e.valid || !e.slotBound || e.targetGuid != targetGuid ||
            e.spellId != spellId || e.rawSlot != rawSlot) continue;
        if (found) return nullptr; // invariant violation/ambiguity: never guess
        found = &e;
    }
    return found;
}

static AuraInstance* findSole(unsigned long long targetGuid,
                              unsigned long spellId) {
    AuraInstance* found = nullptr;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (!e.valid || e.targetGuid != targetGuid || e.spellId != spellId) continue;
        if (found) return nullptr;
        found = &e;
    }
    return found;
}

static unsigned long countInstances(unsigned long long targetGuid,
                                    unsigned long spellId) {
    unsigned long n = 0;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        const AuraInstance& e = g_instances[i];
        if (e.valid && e.targetGuid == targetGuid && e.spellId == spellId) ++n;
    }
    return n;
}

static AuraInstance* claimInstance() {
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i)
        if (!g_instances[i].valid) return &g_instances[i];

    AuraInstance* oldestUnbound = nullptr;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (!e.slotBound && (!oldestUnbound || e.serial < oldestUnbound->serial))
            oldestUnbound = &e;
    }
    if (oldestUnbound) {
        oldestUnbound->valid = false;
        ++g_stats.instanceClears;
        return oldestUnbound;
    }
    ++g_stats.instanceCapacityDrops;
    return nullptr;
}

static unsigned long invalidateSlot(unsigned long long targetGuid,
                                    unsigned long rawSlot,
                                    const AuraInstance* except) {
    unsigned long cleared = 0;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (!e.valid || !e.slotBound || &e == except ||
            e.targetGuid != targetGuid || e.rawSlot != rawSlot) continue;
        e.valid = false;
        ++cleared;
    }
    if (cleared) g_stats.instanceClears += cleared;
    return cleared;
}

static AuraInstance* bindIdentity(unsigned long long targetGuid,
                                  unsigned long long casterGuid,
                                  unsigned long spellId,
                                  unsigned long rawSlot,
                                  unsigned long now,
                                  unsigned char sourceKind,
                                  unsigned long durationMs,
                                  unsigned long expirationMs,
                                  unsigned char timeQuality,
                                  unsigned char timeSource,
                                  unsigned long* clearedOut) {
    AuraInstance* e = findByIdentity(targetGuid, spellId, casterGuid);
    unsigned long cleared = invalidateSlot(targetGuid, rawSlot, e);
    if (clearedOut) *clearedOut += cleared;

    if (!e) {
        e = claimInstance();
        if (!e) return nullptr;
        *e = {};
        e->targetGuid = targetGuid;
        e->casterGuid = casterGuid;
        e->spellId = spellId;
        e->durationMs = durationMs;
        e->expirationMs = expirationMs;
        e->timeQuality = timeQuality;
        e->timeSource = timeSource;
        e->valid = true;
        ++g_stats.instanceWrites;
    } else {
        if (!e->slotBound || e->rawSlot != rawSlot) ++g_stats.instanceRebinds;
        else ++g_stats.instanceRefreshes;
    }

    e->rawSlot = rawSlot;
    e->slotBound = true;
    e->tick = now;
    e->serial = ++g_serial;
    e->sourceKind = sourceKind;
    if (durationMs) {
        e->durationMs = durationMs;
        e->expirationMs = expirationMs;
        e->timeQuality = timeQuality;
        e->timeSource = timeSource;
    }
    return e;
}

static bool bindFromPending(PendingApplication* p, unsigned long rawSlot,
                            unsigned long now, AuraInstance** out,
                            unsigned long* clearedOut) {
    if (!p || !p->valid || rawSlot >= MAX_AURA_SLOTS) return false;
    const unsigned long long target = p->targetGuid;
    const unsigned long long caster = p->casterGuid;
    const unsigned long spell = p->spellId;
    const unsigned char source = p->sourceKind;
    const bool refreshOnly = p->refreshOnly;

    // A refresh candidate may only move/confirm an already-known identity. If
    // the Aura was genuinely removed before its refresh callback arrives, never
    // resurrect it from stale packet evidence.
    if (refreshOnly && !findByIdentity(target, spell, caster)) {
        consumePending(p);
        return false;
    }

    AuraInstance* e = bindIdentity(target, caster, spell, rawSlot, now, source,
                                   p->durationMs, p->expirationMs, p->timeQuality,
                                   p->timeSource, clearedOut);
    if (!e) return false;
    consumePending(p);
    ++g_stats.fifoBindings;
    if (refreshOnly) ++g_stats.refreshBindings;
    if (out) *out = e;
    return true;
}

static void touchExisting(AuraInstance* e, unsigned long now) {
    if (!e) return;
    e->tick = now;
    e->serial = ++g_serial;
    ++g_stats.instanceRefreshes;
}

} // namespace

void initialize() {
    if (g_initialized) return;
    zeroBytes(g_pending, sizeof(g_pending));
    zeroBytes(g_instances, sizeof(g_instances));
    zeroBytes(&g_stats, sizeof(g_stats));
    g_pendingWrite = 0;
    g_serial = 0;
    g_initialized = true;
}

void resetWorldState() {
    if (!g_initialized) initialize();
    zeroBytes(g_pending, sizeof(g_pending));
    zeroBytes(g_instances, sizeof(g_instances));
    g_pendingWrite = 0;
    g_serial = 0;
    ++g_stats.worldResets;
}

void observeCast(unsigned long long targetGuid, unsigned long long casterGuid,
                 unsigned long spellId, unsigned long now,
                 unsigned char sourceKind, unsigned long durationMs,
                 unsigned char timeSource) {
    if (!g_initialized) initialize();
    if (!targetGuid || !casterGuid || !spellId) return;
    ++g_stats.castObservations;
    if (durationMs) ++g_stats.timedCastObservations;

    // A repeat cast by the same caster refreshes the same Aura identity. Update
    // timing immediately, but also retain one short-lived refresh candidate so
    // an ADD callback can safely move the identity to a new rawSlot. This is
    // essential when multiple casters own the same SpellID: FIFO refresh
    // evidence disambiguates slot movement without guessing.
    AuraInstance* existing = findByIdentity(targetGuid, spellId, casterGuid);
    if (existing) {
        existing->tick = now;
        existing->serial = ++g_serial;
        existing->sourceKind = sourceKind;
        if (durationMs) {
            existing->durationMs = durationMs;
            existing->expirationMs = now + durationMs;
            existing->timeQuality = TIME_PREDICTED;
            existing->timeSource = timeSource;
        } else {
            // A known refresh with unknown duration invalidates the previous
            // estimate; existence remains authoritative in UnitFields.
            existing->durationMs = 0;
            existing->expirationMs = 0;
            existing->timeQuality = TIME_UNKNOWN;
            existing->timeSource = TIME_SOURCE_NONE;
        }
        ++g_stats.instanceRefreshes;
        ++g_stats.castRefreshes;
        addPending(targetGuid, casterGuid, spellId, now, sourceKind, durationMs,
                   timeSource, true);
        return;
    }

    addPending(targetGuid, casterGuid, spellId, now, sourceKind, durationMs,
               timeSource, false);
}

ObserveResult observeAura(unsigned long long targetGuid, unsigned long spellId,
                          unsigned long rawSlot, unsigned long state,
                          unsigned long now) {
    if (!g_initialized) initialize();
    ObserveResult r = {};
    if (!targetGuid || !spellId || rawSlot >= MAX_AURA_SLOTS || state > AURA_STACK)
        return r;

    AuraInstance* existing = findBySlot(targetGuid, spellId, rawSlot);

    if (state == AURA_ADD) {
        PendingApplication* p = findOldestPending(targetGuid, spellId, now);
        if (p) {
            AuraInstance* e = nullptr;
            if (bindFromPending(p, rawSlot, now, &e, &r.cleared)) {
                if (r.cleared) g_stats.addPreclears += r.cleared;
                r.bound = true;
                r.resolution = RESOLUTION_FIFO_PENDING;
                r.instance = toView(*e);
                return r;
            }
        }
        if (existing) {
            ++g_stats.existingInstanceReturns;
            r.bound = true;
            r.resolution = RESOLUTION_EXISTING_INSTANCE;
            r.instance = toView(*existing);
            return r;
        }

        AuraInstance* sole = findSole(targetGuid, spellId);
        if (sole && !sole->slotBound) {
            r.cleared += invalidateSlot(targetGuid, rawSlot, sole);
            sole->rawSlot = rawSlot;
            sole->slotBound = true;
            touchExisting(sole, now);
            ++g_stats.retainedInstanceReturns;
            ++g_stats.instanceRebinds;
            r.bound = true;
            r.resolution = RESOLUTION_RETAINED_INSTANCE;
            r.instance = toView(*sole);
            return r;
        }
        if (countInstances(targetGuid, spellId) > 1)
            ++g_stats.ambiguousRetainedMisses;
        r.cleared += invalidateSlot(targetGuid, rawSlot, nullptr);
        if (r.cleared) g_stats.addPreclears += r.cleared;
        return r;
    }

    if (state == AURA_REMOVE) {
        AuraInstance* e = existing;
        if (!e) {
            AuraInstance* sole = findSole(targetGuid, spellId);
            if (sole && !sole->slotBound) e = sole;
        }
        if (e) {
            e->valid = false;
            ++g_stats.instanceClears;
            ++g_stats.removeHits;
            r.cleared = 1;
        } else {
            ++g_stats.removeMisses;
            // If another bound instance for this target/spell still exists but
            // none owns the removed rawSlot, this is a delayed/stale slot remove
            // after refresh/reseat. Never clear a different caster/slot.
            if (countInstances(targetGuid, spellId) != 0)
                ++g_stats.staleRemoveIgnores;
        }
        r.resolution = RESOLUTION_REMOVED;
        return r;
    }

    // STACK: the descriptor slot is stronger than a fresh generic cast
    // candidate. Keep its known caster. If the same caster just recast, consume
    // that candidate as a refresh confirmation so it cannot bind a sibling.
    if (existing) {
        PendingApplication* same = findOldestPendingForCaster(
            targetGuid, spellId, existing->casterGuid, now);
        if (same) consumePending(same);
        touchExisting(existing, now);
        ++g_stats.existingInstanceReturns;
        r.bound = true;
        r.resolution = RESOLUTION_EXISTING_INSTANCE;
        r.instance = toView(*existing);
        return r;
    }

    PendingApplication* p = findOldestPending(targetGuid, spellId, now);
    if (p) {
        AuraInstance* e = nullptr;
        if (bindFromPending(p, rawSlot, now, &e, &r.cleared)) {
            r.bound = true;
            r.resolution = RESOLUTION_FIFO_PENDING;
            r.instance = toView(*e);
            return r;
        }
    }

    AuraInstance* sole = findSole(targetGuid, spellId);
    if (sole && !sole->slotBound) {
        sole->rawSlot = rawSlot;
        sole->slotBound = true;
        touchExisting(sole, now);
        ++g_stats.retainedInstanceReturns;
        ++g_stats.instanceRebinds;
        r.bound = true;
        r.resolution = RESOLUTION_RETAINED_INSTANCE;
        r.instance = toView(*sole);
    } else if (countInstances(targetGuid, spellId) > 1) {
        ++g_stats.ambiguousRetainedMisses;
    }
    return r;
}

bool match(unsigned long long targetGuid, unsigned long spellId,
           unsigned long rawSlot, unsigned long now, InstanceView* out,
           Resolution* resolutionOut) {
    if (!g_initialized) initialize();
    if (resolutionOut) *resolutionOut = RESOLUTION_NONE;
    if (!targetGuid || !spellId || rawSlot >= MAX_AURA_SLOTS) {
        ++g_stats.queryMisses;
        return false;
    }

    AuraInstance* e = findBySlot(targetGuid, spellId, rawSlot);
    if (e) {
        ++g_stats.queryHits;
        ++g_stats.existingInstanceReturns;
        if (out) *out = toView(*e);
        if (resolutionOut) *resolutionOut = RESOLUTION_EXISTING_INSTANCE;
        return true;
    }

    // Read-side fallback is permitted only when one cached identity exists for
    // this (target,spell); the currently-present UnitFields slot supplied by the
    // caller then disambiguates its new seat. Pending is deliberately ignored.
    AuraInstance* sole = findSole(targetGuid, spellId);
    if (sole && !sole->slotBound) {
        sole->rawSlot = rawSlot;
        sole->slotBound = true;
        sole->tick = now;
        sole->serial = ++g_serial;
        ++g_stats.queryHits;
        ++g_stats.retainedInstanceReturns;
        ++g_stats.instanceRebinds;
        if (out) *out = toView(*sole);
        if (resolutionOut) *resolutionOut = RESOLUTION_RETAINED_INSTANCE;
        return true;
    }
    if (countInstances(targetGuid, spellId) > 1)
        ++g_stats.ambiguousRetainedMisses;
    ++g_stats.queryMisses;
    return false;
}

void cleanupVisibleInstances(unsigned long long targetGuid,
                             const unsigned long auraSpellIds[48]) {
    if (!g_initialized) initialize();
    if (!targetGuid || !auraSpellIds) return;
    ++g_stats.descriptorReconciles;

    bool anyPresent = false;
    for (unsigned long s = 0; s < MAX_AURA_SLOTS; ++s) {
        if (auraSpellIds[s] != 0) { anyPresent = true; break; }
    }
    if (!anyPresent) {
        ++g_stats.descriptorEmptyPreserves;
        return;
    }

    for (unsigned long i = 0; i < MAX_INSTANCES; ++i) {
        AuraInstance& e = g_instances[i];
        if (!e.valid || e.targetGuid != targetGuid) continue;
        if (e.slotBound && e.rawSlot < MAX_AURA_SLOTS &&
            auraSpellIds[e.rawSlot] == e.spellId) continue;

        unsigned long foundSlot = INVALID_SLOT;
        unsigned long occurrences = 0;
        for (unsigned long s = 0; s < MAX_AURA_SLOTS; ++s) {
            if (auraSpellIds[s] == e.spellId) {
                foundSlot = s;
                ++occurrences;
            }
        }
        if (occurrences == 0) {
            e.valid = false;
            ++g_stats.instanceClears;
            ++g_stats.descriptorClears;
            continue;
        }

        // One descriptor copy + one cached identity is safe to re-seat.
        if (occurrences == 1 && countInstances(targetGuid, e.spellId) == 1) {
            e.rawSlot = foundSlot;
            e.slotBound = true;
            e.serial = ++g_serial;
            ++g_stats.instanceRebinds;
            continue;
        }

        // Several same-spell copies: retain caster identity but detach the slot
        // rather than randomly assigning one caster to another's descriptor row.
        if (e.slotBound) {
            e.rawSlot = INVALID_SLOT;
            e.slotBound = false;
            ++g_stats.descriptorUnbinds;
        }
    }
}

Stats stats() { return g_stats; }

unsigned long activeInstanceCount() {
    unsigned long n = 0;
    for (unsigned long i = 0; i < MAX_INSTANCES; ++i)
        if (g_instances[i].valid) ++n;
    return n;
}

unsigned long activePendingCount(unsigned long now) {
    expirePending(now);
    unsigned long n = 0;
    for (unsigned long i = 0; i < MAX_PENDING; ++i)
        if (g_pending[i].valid) ++n;
    return n;
}

} // namespace TysAuraSourceCore
