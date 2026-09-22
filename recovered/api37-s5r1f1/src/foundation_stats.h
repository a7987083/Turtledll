#pragma once

namespace TysFoundationStats {
struct GuidStats {
    unsigned long fastAttempts;
    unsigned long fastHits;
    unsigned long fastMisses;
    unsigned long fastRejected;
    unsigned long fallbackAttempts;
    unsigned long fallbackHits;
    unsigned long fallbackMisses;
};

GuidStats snapshotGuid();
void resetGuid();
void noteFastAttempt();
void noteFastHit();
void noteFastMiss();
void noteFastRejected();
void noteFallbackAttempt();
void noteFallbackHit();
void noteFallbackMiss();
}
