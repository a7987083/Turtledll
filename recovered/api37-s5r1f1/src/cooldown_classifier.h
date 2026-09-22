#pragma once

#include <cstdint>

namespace TysCooldownClassifier {

enum Kind : std::uint8_t {
    KIND_NONE = 0,
    KIND_GCD = 1,
    KIND_SPELL = 2,
    KIND_UNKNOWN = 3,
};

enum Source : std::uint8_t {
    SOURCE_SPELL_GO = 1,
    SOURCE_SPELL_COOLDOWN = 2,
    SOURCE_COOLDOWN_EVENT = 3,
    SOURCE_CLEAR_COOLDOWN = 4,
    SOURCE_COOLDOWN_CHEAT = 5,
    SOURCE_DEADLINE_RECHECK = 6,
    SOURCE_EXPLICIT_ENGINE_QUERY = 7,
};

struct SpellRecoveryFields {
    bool available;
    std::uint32_t recoveryTime;
    std::uint32_t categoryRecoveryTime;
    std::uint32_t startRecoveryCategory;
    std::uint32_t startRecoveryTime;
};

// Recovered from final CD1-R2 disassembly (0x1004428F..0x100442DC).
inline Kind classify(bool active, Source source, const SpellRecoveryFields& f) {
    if (!active)
        return KIND_NONE;

    if (source == SOURCE_SPELL_COOLDOWN || source == SOURCE_COOLDOWN_EVENT)
        return KIND_SPELL;

    if (f.available && (f.recoveryTime != 0 || f.categoryRecoveryTime != 0))
        return KIND_SPELL;

    if (f.available && f.startRecoveryTime != 0)
        return KIND_GCD;

    return KIND_UNKNOWN;
}

inline const char* kindName(Kind k) {
    switch (k) {
        case KIND_GCD: return "GCD";
        case KIND_SPELL: return "SPELL";
        case KIND_UNKNOWN: return "UNKNOWN";
        default: return "NONE";
    }
}

inline const char* sourceName(Source s) {
    switch (s) {
        case SOURCE_SPELL_GO: return "SMSG_SPELL_GO";
        case SOURCE_SPELL_COOLDOWN: return "SMSG_SPELL_COOLDOWN";
        case SOURCE_COOLDOWN_EVENT: return "SMSG_COOLDOWN_EVENT";
        case SOURCE_CLEAR_COOLDOWN: return "SMSG_CLEAR_COOLDOWN";
        case SOURCE_COOLDOWN_CHEAT: return "SMSG_COOLDOWN_CHEAT";
        case SOURCE_DEADLINE_RECHECK: return "DEADLINE_RECHECK";
        case SOURCE_EXPLICIT_ENGINE_QUERY: return "EXPLICIT_ENGINE_QUERY";
        default: return "UNKNOWN";
    }
}

} // namespace TysCooldownClassifier
