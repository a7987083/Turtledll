#pragma once

#include <cstdint>

namespace TysUnitStateChangeMask {

// UnitState.Status::lastChangedMask category bits in the final target.
enum Aggregate : std::uint32_t {
    HEALTH = 0x01u,
    POWER  = 0x02u,
    COMBAT = 0x04u,
};

inline std::uint32_t aggregate(bool healthChanged,
                               std::uint32_t detailedPowerMask,
                               bool combatChanged) {
    std::uint32_t mask = healthChanged ? HEALTH : 0u;
    if (detailedPowerMask != 0u) mask |= POWER;
    if (combatChanged) mask |= COMBAT;
    return mask;
}

} // namespace TysUnitStateChangeMask
