#include "los_pair_cache.h"

namespace TysLosPairCache {

struct Entry {
    Guid lo;
    Guid hi;
    Tick32 tick;
    unsigned char value;
    unsigned char valid;
};

static Entry g_entries[SLOT_COUNT] = {};

static void canonicalize(Guid* a, Guid* b) {
    if (!a || !b) return;
    if (*a > *b) {
        Guid t = *a;
        *a = *b;
        *b = t;
    }
}

static unsigned int slotFor(Guid lo, Guid hi) {
    // Small integer-only mix. SLOT_COUNT is a power of two.
    unsigned int x = static_cast<unsigned int>(lo) ^ static_cast<unsigned int>(lo >> 32);
    unsigned int y = static_cast<unsigned int>(hi) ^ static_cast<unsigned int>(hi >> 32);
    unsigned int h = x * 0x9E3779B1U;
    h ^= y + 0x85EBCA6BU + (h << 6) + (h >> 2);
    return h & (SLOT_COUNT - 1U);
}

void reset() {
    for (unsigned int i = 0; i < SLOT_COUNT; ++i) {
        g_entries[i].lo = 0;
        g_entries[i].hi = 0;
        g_entries[i].tick = 0;
        g_entries[i].value = 0;
        g_entries[i].valid = 0;
    }
}

bool tryGet(Guid a, Guid b, Tick32 now, Tick32 ttlMs, bool* result) {
    if (!a || !b || !result || ttlMs == 0U) return false;
    canonicalize(&a, &b);
    Entry& e = g_entries[slotFor(a, b)];
    if (!e.valid || e.lo != a || e.hi != b) return false;

    // Unsigned subtraction intentionally preserves GetTickCount wrap behavior.
    const Tick32 age = now - e.tick;
    if (age >= ttlMs) return false;

    *result = e.value != 0;
    return true;
}

void put(Guid a, Guid b, Tick32 now, bool result) {
    if (!a || !b) return;
    canonicalize(&a, &b);
    Entry& e = g_entries[slotFor(a, b)];
    e.lo = a;
    e.hi = b;
    e.tick = now;
    e.value = result ? 1U : 0U;
    e.valid = 1U;
}

} // namespace TysLosPairCache
