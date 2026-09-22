#pragma once

#include <cstdint>

namespace TysUnitStateLayout {

// Final API37 / US1-R2 binary-confirmed descriptor path.
constexpr std::uint32_t OBJECT_DESCRIPTOR_PTR = 0x08u;
constexpr std::uint32_t OBJECT_TYPE            = 0x14u;
constexpr std::uint32_t OBJECT_GUID_LOW        = 0x30u;
constexpr std::uint32_t OBJECT_GUID_HIGH       = 0x34u;
constexpr std::uint32_t OBJECT_VALIDATE_BYTES  = 0x38u;

constexpr std::uint32_t DESC_HEALTH            = 0x58u;
constexpr std::uint32_t DESC_POWER1            = 0x5Cu;
constexpr std::uint32_t DESC_MAX_HEALTH        = 0x70u;
constexpr std::uint32_t DESC_MAX_POWER1        = 0x74u;
constexpr std::uint32_t DESC_POWER_TYPE_PACKED = 0x90u;
constexpr std::uint32_t DESC_UNIT_FLAGS        = 0xB8u;
constexpr std::uint32_t DESC_STATE_23C         = 0x23Cu;

constexpr std::uint32_t DESC_RANGE_START       = 0x58u;
constexpr std::uint32_t DESC_RANGE_BYTES       = 0x1E8u;

constexpr std::uint32_t POWER_LANES            = 5u;
constexpr std::uint32_t UNIT_FLAG_IN_COMBAT    = 0x00080000u;

inline std::uint32_t powerOffset(std::uint32_t powerType) {
    return DESC_POWER1 + powerType * 4u;
}

inline std::uint32_t maxPowerOffset(std::uint32_t powerType) {
    return DESC_MAX_POWER1 + powerType * 4u;
}

inline std::uint32_t powerTypeFromPacked(std::uint32_t packed) {
    return packed >> 24;
}

inline bool inCombat(std::uint32_t flags) {
    return (flags & UNIT_FLAG_IN_COMBAT) != 0;
}

// Public power-change contract frozen in US1-R2.
enum PowerChangeMask : std::uint32_t {
    POWER_TYPE_CHANGED = 0x01u,
    POWER_VALUE_CHANGED = 0x02u,
    POWER_MAX_CHANGED = 0x04u,
};

} // namespace TysUnitStateLayout
