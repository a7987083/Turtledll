#include <cassert>
#include <cstdio>
#include "../src/aura_source_core.h"
using namespace TysAuraSourceCore;

static InstanceView mustMatch(unsigned long long g, unsigned long s,
                              unsigned long r, unsigned long now,
                              Resolution expected = RESOLUTION_EXISTING_INSTANCE) {
    InstanceView v = {};
    Resolution q = RESOLUTION_NONE;
    bool ok = match(g, s, r, now, &v, &q);
    assert(ok);
    if (expected != RESOLUTION_NONE) assert(q == expected);
    return v;
}

static void mustMiss(unsigned long long g, unsigned long s,
                     unsigned long r, unsigned long now) {
    InstanceView v = {};
    Resolution q = RESOLUTION_NONE;
    assert(!match(g, s, r, now, &v, &q));
    assert(q == RESOLUTION_NONE);
}

int main() {
    initialize();
    unsigned long t = 10000;

    // Basic native application.
    observeCast(0xA1, 0x111, 1001, t, SOURCE_NATIVE_HIT_TARGET);
    auto a = observeAura(0xA1, 1001, 34, AURA_ADD, t + 10);
    assert(a.bound && a.resolution == RESOLUTION_FIFO_PENDING);
    assert(a.instance.casterGuid == 0x111 && a.instance.rawSlot == 34);
    assert(mustMatch(0xA1, 1001, 34, t + 20).casterGuid == 0x111);

    // FIFO: two casters, same target + spell, applications seat in cast order.
    observeCast(0xB1, 0x211, 2001, t + 100, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xB1, 0x222, 2001, t + 101, SOURCE_NATIVE_HIT_TARGET);
    auto b1 = observeAura(0xB1, 2001, 32, AURA_ADD, t + 110);
    auto b2 = observeAura(0xB1, 2001, 33, AURA_ADD, t + 111);
    assert(b1.bound && b1.instance.casterGuid == 0x211);
    assert(b2.bound && b2.instance.casterGuid == 0x222);
    assert(mustMatch(0xB1, 2001, 32, t + 120).casterGuid == 0x211);
    assert(mustMatch(0xB1, 2001, 33, t + 120).casterGuid == 0x222);

    // Query never consumes Pending. It becomes visible only after ADD.
    observeCast(0xC1, 0x311, 3001, t + 200, SOURCE_NATIVE_HIT_TARGET);
    unsigned long pendingBefore = activePendingCount(t + 201);
    mustMiss(0xC1, 3001, 35, t + 202);
    assert(activePendingCount(t + 203) == pendingBefore);
    auto c = observeAura(0xC1, 3001, 35, AURA_ADD, t + 210);
    assert(c.bound && c.instance.casterGuid == 0x311);

    // One candidate cannot manufacture a sibling slot (usedSlotMask is gone).
    observeCast(0xD1, 0x411, 4001, t + 300, SOURCE_NATIVE_HIT_TARGET);
    auto d = observeAura(0xD1, 4001, 36, AURA_ADD, t + 310);
    assert(d.bound && d.instance.casterGuid == 0x411);
    auto noSibling = observeAura(0xD1, 4001, 37, AURA_ADD, t + 311);
    assert(!noSibling.bound);
    assert(mustMatch(0xD1, 4001, 36, t + 320).casterGuid == 0x411);
    mustMiss(0xD1, 4001, 37, t + 320);

    // STACK never lets another caster steal an already-bound slot.
    observeCast(0xE1, 0x511, 5001, t + 400, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xE1, 0x522, 5001, t + 401, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xE1, 5001, 38, AURA_ADD, t + 410); // 0x511
    observeAura(0xE1, 5001, 39, AURA_ADD, t + 411); // 0x522
    observeCast(0xE1, 0x522, 5001, t + 420, SOURCE_NATIVE_HIT_TARGET);
    auto stackOtherSlot = observeAura(0xE1, 5001, 38, AURA_STACK, t + 421);
    assert(stackOtherSlot.bound && stackOtherSlot.instance.casterGuid == 0x511);
    assert(mustMatch(0xE1, 5001, 39, t + 422).casterGuid == 0x522);
    auto stackOwner = observeAura(0xE1, 5001, 39, AURA_STACK, t + 423);
    assert(stackOwner.bound && stackOwner.instance.casterGuid == 0x522);

    // Genuine remove invalidates the instance; there is no OwnerMemory restore.
    auto removed = observeAura(0xA1, 1001, 34, AURA_REMOVE, t + 500);
    assert(removed.cleared == 1 && removed.resolution == RESOLUTION_REMOVED);
    mustMiss(0xA1, 1001, 34, t + 501);

    // All-empty descriptor is treated as a visibility teardown: preserve cache.
    unsigned long empty[48] = {};
    cleanupVisibleInstances(0xD1, empty);
    assert(mustMatch(0xD1, 4001, 36, t + 600).casterGuid == 0x411);

    // Populated contradictory descriptor is authoritative and clears stale aura.
    unsigned long contradictory[48] = {};
    contradictory[2] = 9999;
    cleanupVisibleInstances(0xD1, contradictory);
    mustMiss(0xD1, 4001, 36, t + 610);

    // Descriptor reshuffle with two same-spell casters becomes ambiguous rather
    // than guessing one caster onto the other slot.
    observeCast(0xD5, 0xA11, 4501, t + 650, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xD5, 0xA22, 4501, t + 651, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xD5, 4501, 42, AURA_ADD, t + 660);
    observeAura(0xD5, 4501, 43, AURA_ADD, t + 661);
    unsigned long shifted[48] = {};
    shifted[44] = 4501; shifted[45] = 4501;
    cleanupVisibleInstances(0xD5, shifted);
    mustMiss(0xD5, 4501, 44, t + 670);
    mustMiss(0xD5, 4501, 45, t + 670);

    // Duplicate observations for the same identity are consumed as one instance.
    observeCast(0xF1, 0x611, 6001, t + 700, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xF1, 0x611, 6001, t + 701, SOURCE_NATIVE_HIT_TARGET);
    auto f = observeAura(0xF1, 6001, 40, AURA_ADD, t + 710);
    assert(f.bound && f.instance.casterGuid == 0x611);
    auto fSibling = observeAura(0xF1, 6001, 41, AURA_ADD, t + 711);
    assert(!fSibling.bound);

    // Self/implicit-target source kind remains distinguishable.
    observeCast(0x701, 0x701, 7001, t + 800, SOURCE_NATIVE_CASTER_SELF);
    auto self = observeAura(0x701, 7001, 2, AURA_ADD, t + 810);
    assert(self.bound && self.instance.sourceKind == SOURCE_NATIVE_CASTER_SELF);


    // R3 timing evidence remains part of the same Aura instance.
    observeCast(0x801, 0x811, 8001, t + 900, SOURCE_NATIVE_HIT_TARGET,
                16000, TIME_SOURCE_LOCAL_COMBO_SCALED);
    auto timed = observeAura(0x801, 8001, 32, AURA_ADD, t + 910);
    assert(timed.bound && timed.instance.durationMs == 16000);
    assert(timed.instance.timeQuality == TIME_PREDICTED);
    assert(timed.instance.timeSource == TIME_SOURCE_LOCAL_COMBO_SCALED);

    // R4 refresh: a repeat cast opens one refresh-only FIFO candidate. The
    // same-slot ADD consumes it and refreshes timing without creating a sibling.
    unsigned long pendingTimed = activePendingCount(t + 911);
    observeCast(0x801, 0x811, 8001, t + 920, SOURCE_NATIVE_HIT_TARGET,
                12000, TIME_SOURCE_LOCAL_CAST_MODIFIED);
    assert(activePendingCount(t + 921) == pendingTimed + 1);
    auto refreshSame = observeAura(0x801, 8001, 32, AURA_ADD, t + 922);
    assert(refreshSame.bound && refreshSame.instance.casterGuid == 0x811);
    assert(refreshSame.instance.durationMs == 12000);
    assert(refreshSame.instance.expirationMs == t + 920 + 12000);
    assert(activePendingCount(t + 923) == pendingTimed);

    // R4 refresh-slot migration: observed caster evidence can move the same
    // identity to a new raw slot. A delayed REMOVE for the old slot must not
    // delete the newly rebound instance.
    observeCast(0x901, 0x911, 9001, t + 1000, SOURCE_NATIVE_HIT_TARGET,
                15000, TIME_SOURCE_LOCAL_CAST_MODIFIED);
    auto firstSeat = observeAura(0x901, 9001, 10, AURA_ADD, t + 1010);
    assert(firstSeat.bound && firstSeat.instance.rawSlot == 10);
    observeCast(0x901, 0x911, 9001, t + 1020, SOURCE_NATIVE_HIT_TARGET,
                15000, TIME_SOURCE_LOCAL_CAST_MODIFIED);
    auto movedSeat = observeAura(0x901, 9001, 12, AURA_ADD, t + 1021);
    assert(movedSeat.bound && movedSeat.instance.casterGuid == 0x911);
    assert(movedSeat.instance.rawSlot == 12);
    auto staleRemove = observeAura(0x901, 9001, 10, AURA_REMOVE, t + 1022);
    assert(staleRemove.cleared == 0);
    assert(mustMatch(0x901, 9001, 12, t + 1023).casterGuid == 0x911);

    // R4 multi-caster same SpellID refresh: refresh observations are FIFO too,
    // so two existing caster identities can move without crossing owners.
    observeCast(0xA01, 0xA11, 9101, t + 1100, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xA01, 0xA22, 9101, t + 1101, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xA01, 9101, 20, AURA_ADD, t + 1110);
    observeAura(0xA01, 9101, 21, AURA_ADD, t + 1111);
    observeCast(0xA01, 0xA11, 9101, t + 1120, SOURCE_NATIVE_HIT_TARGET);
    observeCast(0xA01, 0xA22, 9101, t + 1121, SOURCE_NATIVE_HIT_TARGET);
    auto moveA = observeAura(0xA01, 9101, 22, AURA_ADD, t + 1130);
    auto moveB = observeAura(0xA01, 9101, 23, AURA_ADD, t + 1131);
    assert(moveA.bound && moveA.instance.casterGuid == 0xA11);
    assert(moveB.bound && moveB.instance.casterGuid == 0xA22);
    observeAura(0xA01, 9101, 20, AURA_REMOVE, t + 1132);
    observeAura(0xA01, 9101, 21, AURA_REMOVE, t + 1133);
    assert(mustMatch(0xA01, 9101, 22, t + 1134).casterGuid == 0xA11);
    assert(mustMatch(0xA01, 9101, 23, t + 1135).casterGuid == 0xA22);

    // Target switching is pure GUID isolation: the same SpellID/rawSlot on two
    // targets never shares identity or caster attribution.
    observeCast(0xB01, 0xB11, 9201, t + 1200, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xB01, 9201, 30, AURA_ADD, t + 1201);
    observeCast(0xB02, 0xB22, 9201, t + 1202, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xB02, 9201, 30, AURA_ADD, t + 1203);
    assert(mustMatch(0xB01, 9201, 30, t + 1204).casterGuid == 0xB11);
    assert(mustMatch(0xB02, 9201, 30, t + 1205).casterGuid == 0xB22);

    // A refresh with no reliable duration evidence must invalidate the old
    // prediction rather than carrying stale time into the new application.
    observeCast(0xC01, 0xC11, 9301, t + 1300, SOURCE_NATIVE_HIT_TARGET,
                18000, TIME_SOURCE_LOCAL_CAST_MODIFIED);
    observeAura(0xC01, 9301, 31, AURA_ADD, t + 1301);
    observeCast(0xC01, 0xC11, 9301, t + 1310, SOURCE_NATIVE_HIT_TARGET,
                0, TIME_SOURCE_NONE);
    auto unknownRefresh = mustMatch(0xC01, 9301, 31, t + 1311);
    assert(unknownRefresh.timeQuality == TIME_UNKNOWN);
    assert(unknownRefresh.durationMs == 0 && unknownRefresh.expirationMs == 0);
    observeAura(0xC01, 9301, 31, AURA_STACK, t + 1312); // consumes refresh marker

    // World teardown is a hard transient boundary. It clears both Pending and
    // bound instances, but the core remains initialized and ready for the next map.
    observeCast(0xD01, 0xD11, 9401, t + 1400, SOURCE_NATIVE_HIT_TARGET);
    observeAura(0xD01, 9401, 40, AURA_ADD, t + 1401);
    observeCast(0xD02, 0xD22, 9402, t + 1402, SOURCE_NATIVE_HIT_TARGET);
    assert(activeInstanceCount() > 0);
    assert(activePendingCount(t + 1403) > 0);
    resetWorldState();
    assert(activeInstanceCount() == 0);
    assert(activePendingCount(t + 1404) == 0);
    mustMiss(0xD01, 9401, 40, t + 1405);
    observeCast(0xE01, 0xE11, 9501, t + 1410, SOURCE_NATIVE_HIT_TARGET);
    auto afterReset = observeAura(0xE01, 9501, 41, AURA_ADD, t + 1411);
    assert(afterReset.bound && afterReset.instance.casterGuid == 0xE11);

    Stats s = stats();
    assert(s.fifoBindings >= 16);
    assert(s.pendingCapacityDrops == 0);
    assert(s.instanceCapacityDrops == 0);
    assert(s.refreshPendingWrites >= 5);
    assert(s.refreshBindings >= 4);
    assert(s.staleRemoveIgnores >= 3);
    assert(s.worldResets >= 1);
    std::puts("AURA6_D4_R4_FINAL_LIFECYCLE_CORE=PASS");
    return 0;
}
