#pragma once

namespace TysComboDurationCore {

struct CaptureView {
    unsigned long spellId;
    unsigned long comboPoints;
    unsigned long tick;
    unsigned long long comboTargetGuid;
    bool valid;
};

struct Stats {
    unsigned long writes;
    unsigned long consumes;
    unsigned long misses;
    unsigned long expired;
    unsigned long replacements;
    unsigned long capacityDrops;
};

void initialize();
void resetWorldState();
void remember(unsigned long spellId, unsigned long comboPoints,
              unsigned long long comboTargetGuid, unsigned long now);
bool consume(unsigned long spellId, unsigned long now, CaptureView* out);
Stats stats();
unsigned long active(unsigned long now);

// Pure server-mirroring formula for combo-scaled duration rows.
// Returns 0 when the row is not combo-scaled or the inputs are invalid.
unsigned long scaleDuration(unsigned long baseMs, unsigned long maxMs,
                            unsigned long comboPoints,
                            long flatModMs, long pctMod);

constexpr unsigned long ttlMs() { return 2000UL; }
constexpr unsigned long capacity() { return 16UL; }

} // namespace TysComboDurationCore
