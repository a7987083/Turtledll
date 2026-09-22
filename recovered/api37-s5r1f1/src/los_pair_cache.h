#pragma once

namespace TysLosPairCache {
using Guid = unsigned long long;
using Tick32 = unsigned int;

constexpr unsigned int SLOT_COUNT = 128U;
constexpr Tick32 DEFAULT_TTL_MS = 50U;

struct Stats {
    unsigned long hits;
    unsigned long misses;
    unsigned long expired;
    unsigned long collisionMisses;
    unsigned long stores;
    unsigned long replacements;
    unsigned long recomputes;
};

void reset();
bool tryGet(Guid a, Guid b, Tick32 now, Tick32 ttlMs, bool* result);
void put(Guid a, Guid b, Tick32 now, bool result);
void noteRecompute();
Stats stats();
}
