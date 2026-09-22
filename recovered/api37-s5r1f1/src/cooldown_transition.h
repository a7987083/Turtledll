#pragma once

#include <cstdint>
#include "cooldown_classifier.h"

namespace TysCooldownTransition {

enum Event : std::uint8_t {
    EVENT_NONE = 0,
    EVENT_STARTED = 1,
    EVENT_CHANGED = 2,
    EVENT_READY = 3,
};

struct Snapshot {
    bool queryOk;
    bool active;
    std::uint32_t spellId;
    std::uint32_t startMs;
    std::uint32_t durationMs;
    std::uint32_t endMs;
    std::uint32_t remainingMs;
    std::uint32_t enable;
    TysCooldownClassifier::Kind kind;
    TysCooldownClassifier::Source source;
};

inline bool externallyEqual(const Snapshot& a, const Snapshot& b) {
    return a.active == b.active &&
           a.startMs == b.startMs &&
           a.durationMs == b.durationMs &&
           a.enable == b.enable &&
           a.kind == b.kind;
}

// Behavior-equivalent to the event decision recovered from 0x10044998.
inline Event decide(bool haveOld, const Snapshot& oldState, const Snapshot& newState) {
    if (!newState.queryOk)
        return EVENT_NONE;

    if (!haveOld)
        return newState.active ? EVENT_STARTED : EVENT_NONE;

    if (!newState.active)
        return oldState.active ? EVENT_READY : EVENT_NONE;

    if (externallyEqual(oldState, newState))
        return EVENT_NONE;

    return EVENT_CHANGED;
}

inline bool isSpellToGcd(const Snapshot& oldState, const Snapshot& newState) {
    return oldState.kind == TysCooldownClassifier::KIND_SPELL &&
           newState.kind == TysCooldownClassifier::KIND_GCD;
}

} // namespace TysCooldownTransition
