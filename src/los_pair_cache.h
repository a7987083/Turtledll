#pragma once

namespace TysLosPairCache {

using Guid = unsigned long long;
using Tick32 = unsigned int;

constexpr unsigned int SLOT_COUNT = 128U;
constexpr Tick32 DEFAULT_TTL_MS = 50U;

// Fixed-capacity, allocation-free, direct-mapped cache for symmetric unit LOS.
// A collision only replaces an older cache line; it can reduce hit-rate but can
// never change LOS correctness because callers recompute on a miss.
void reset();
bool tryGet(Guid a, Guid b, Tick32 now, Tick32 ttlMs, bool* result);
void put(Guid a, Guid b, Tick32 now, bool result);

} // namespace TysLosPairCache
