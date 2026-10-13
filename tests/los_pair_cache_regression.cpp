#include "../src/los_pair_cache.h"
#include <cassert>
#include <cstdint>

int main() {
    using namespace TysLosPairCache;
    reset();

    const Guid a = 0xF130000001234567ULL;
    const Guid b = 0xF130000008765432ULL;
    bool result = false;

    assert(!tryGet(a, b, 1000U, 50U, &result));
    put(a, b, 1000U, true);
    assert(tryGet(a, b, 1000U, 50U, &result) && result);
    assert(tryGet(b, a, 1049U, 50U, &result) && result); // symmetric key
    assert(!tryGet(a, b, 1050U, 50U, &result));         // exact TTL expires

    put(a, b, 2000U, false);
    result = true;
    assert(tryGet(a, b, 2049U, 50U, &result) && !result);

    // 32-bit GetTickCount wrap: 0xFFFFFFF0 -> 0x00000010 is 32 ms.
    reset();
    put(a, b, 0xFFFFFFF0U, true);
    assert(tryGet(a, b, 0x00000010U, 50U, &result) && result);
    assert(!tryGet(a, b, 0x00000030U, 50U, &result)); // 64 ms

    reset();
    assert(!tryGet(0, b, 1U, 50U, &result));
    assert(!tryGet(a, 0, 1U, 50U, &result));
    assert(!tryGet(a, b, 1U, 0U, &result));
    return 0;
}
