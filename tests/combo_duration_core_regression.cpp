#include <cassert>
#include <cstdio>
#include "../src/combo_duration_core.h"
using namespace TysComboDurationCore;

int main() {
    initialize();
    unsigned long t = 1000;

    // 6s -> 16s row mirrors the vanilla combo-duration interpolation.
    assert(scaleDuration(6000, 16000, 1, 0, 0) == 8000);
    assert(scaleDuration(6000, 16000, 5, 0, 0) == 16000);
    assert(scaleDuration(6000, 16000, 3, 1000, 20) == 15600);
    assert(scaleDuration(6000, 6000, 5, 0, 0) == 0);

    remember(1943, 1, 0xA1, t);
    remember(408, 5, 0xA1, t + 1);
    CaptureView c = {};
    assert(consume(408, t + 20, &c));
    assert(c.comboPoints == 5 && c.comboTargetGuid == 0xA1);
    assert(consume(1943, t + 21, &c));
    assert(c.comboPoints == 1);

    remember(1943, 5, 0xA1, t + 100);
    assert(!consume(1943, t + 2201, &c));

    remember(1943, 5, 0xB1, t + 2300);
    assert(active(t + 2301) == 1);
    resetWorldState();
    assert(active(t + 2302) == 0);

    std::puts("AURA6_D4_R4_COMBO_DURATION_CORE=PASS");
    return 0;
}
