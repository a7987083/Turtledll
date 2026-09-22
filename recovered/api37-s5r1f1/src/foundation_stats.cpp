#include "foundation_stats.h"

namespace TysFoundationStats {
namespace {
volatile unsigned long g_fastAttempts=0;
volatile unsigned long g_fastHits=0;
volatile unsigned long g_fastMisses=0;
volatile unsigned long g_fastRejected=0;
volatile unsigned long g_fallbackAttempts=0;
volatile unsigned long g_fallbackHits=0;
volatile unsigned long g_fallbackMisses=0;
}

GuidStats snapshotGuid(){
    GuidStats s={g_fastAttempts,g_fastHits,g_fastMisses,g_fastRejected,g_fallbackAttempts,g_fallbackHits,g_fallbackMisses};
    return s;
}
void resetGuid(){ g_fastAttempts=g_fastHits=g_fastMisses=g_fastRejected=g_fallbackAttempts=g_fallbackHits=g_fallbackMisses=0; }
void noteFastAttempt(){ ++g_fastAttempts; }
void noteFastHit(){ ++g_fastHits; }
void noteFastMiss(){ ++g_fastMisses; }
void noteFastRejected(){ ++g_fastRejected; }
void noteFallbackAttempt(){ ++g_fallbackAttempts; }
void noteFallbackHit(){ ++g_fallbackHits; }
void noteFallbackMiss(){ ++g_fallbackMisses; }
}
