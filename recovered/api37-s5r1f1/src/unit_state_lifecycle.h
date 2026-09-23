#pragma once

#include <cstdint>

namespace TysUnitStateLifecycle {

// Final US1-R2 binary semantics.
// worldGeneration is advanced only on the PLAYER_LEAVING_WORLD reset path.
// It is intentionally kept non-zero across wrap.
inline std::uint32_t nextNonZeroGeneration(std::uint32_t generation) {
    ++generation;
    if (generation == 0u)
        generation = 1u;
    return generation;
}

// UnitState.Status.lastChangedMask is a category mask, not the detailed
// power payload mask.
enum ChangeCategoryMask : std::uint32_t {
    CHANGE_HEALTH = 0x01u,
    CHANGE_POWER  = 0x02u,
    CHANGE_COMBAT = 0x04u,
};

// Snapshot helper result categories reconstructed from the final DLL call site.
// HELPER_FAILURE is descriptor/snapshot failure; OBJECT_UNAVAILABLE is a
// successful snapshot call that explicitly reports no live object; SNAPSHOT_OK
// contains object + descriptor backed state.
enum class SnapshotOutcome : std::uint32_t {
    HELPER_FAILURE = 0,
    OBJECT_UNAVAILABLE = 1,
    SNAPSHOT_OK = 2,
};

} // namespace TysUnitStateLifecycle
