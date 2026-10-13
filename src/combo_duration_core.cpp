#include "combo_duration_core.h"

namespace TysComboDurationCore {
namespace {

constexpr unsigned long MAX_CAPTURES = 16UL;
constexpr unsigned long TTL_MS = 2000UL;

struct Capture {
    unsigned long spellId;
    unsigned long comboPoints;
    unsigned long tick;
    unsigned long long comboTargetGuid;
    bool valid;
};

static Capture g_captures[MAX_CAPTURES] = {};
static unsigned long g_cursor = 0;
static bool g_initialized = false;
static Stats g_stats = {};

static void zeroBytes(void* p, unsigned long n) {
    unsigned char* b = (unsigned char*)p;
    for (unsigned long i = 0; i < n; ++i) b[i] = 0;
}

static unsigned long ageMs(unsigned long then, unsigned long now) {
    return now - then;
}

static void expire(unsigned long now) {
    for (unsigned long i = 0; i < MAX_CAPTURES; ++i) {
        Capture& c = g_captures[i];
        if (!c.valid) continue;
        if (ageMs(c.tick, now) >= TTL_MS) {
            c.valid = false;
            ++g_stats.expired;
        }
    }
}

} // namespace

void initialize() {
    if (g_initialized) return;
    zeroBytes(g_captures, sizeof(g_captures));
    zeroBytes(&g_stats, sizeof(g_stats));
    g_cursor = 0;
    g_initialized = true;
}

void resetWorldState() {
    if (!g_initialized) initialize();
    zeroBytes(g_captures, sizeof(g_captures));
    g_cursor = 0;
}

void remember(unsigned long spellId, unsigned long comboPoints,
              unsigned long long comboTargetGuid, unsigned long now) {
    if (!g_initialized) initialize();
    if (!spellId || comboPoints < 1 || comboPoints > 5) return;
    expire(now);

    // Key by spellId: a second request of the same finisher before its first
    // response is newer evidence and replaces the stale request.
    for (unsigned long i = 0; i < MAX_CAPTURES; ++i) {
        Capture& c = g_captures[i];
        if (c.valid && c.spellId == spellId) {
            c.comboPoints = comboPoints;
            c.comboTargetGuid = comboTargetGuid;
            c.tick = now;
            ++g_stats.writes;
            ++g_stats.replacements;
            return;
        }
    }

    for (unsigned long pass = 0; pass < MAX_CAPTURES; ++pass) {
        unsigned long idx = (g_cursor + pass) % MAX_CAPTURES;
        if (g_captures[idx].valid) continue;
        g_captures[idx] = {spellId, comboPoints, now, comboTargetGuid, true};
        g_cursor = (idx + 1) % MAX_CAPTURES;
        ++g_stats.writes;
        return;
    }
    ++g_stats.capacityDrops;
}

bool consume(unsigned long spellId, unsigned long now, CaptureView* out) {
    if (!g_initialized) initialize();
    if (out) *out = {};
    if (!spellId) { ++g_stats.misses; return false; }
    expire(now);
    for (unsigned long i = 0; i < MAX_CAPTURES; ++i) {
        Capture& c = g_captures[i];
        if (!c.valid || c.spellId != spellId) continue;
        if (out) {
            out->spellId = c.spellId;
            out->comboPoints = c.comboPoints;
            out->tick = c.tick;
            out->comboTargetGuid = c.comboTargetGuid;
            out->valid = true;
        }
        c.valid = false;
        ++g_stats.consumes;
        return true;
    }
    ++g_stats.misses;
    return false;
}

Stats stats() { return g_stats; }

unsigned long active(unsigned long now) {
    if (!g_initialized) initialize();
    expire(now);
    unsigned long n = 0;
    for (unsigned long i = 0; i < MAX_CAPTURES; ++i)
        if (g_captures[i].valid) ++n;
    return n;
}

unsigned long scaleDuration(unsigned long baseMs, unsigned long maxMs,
                            unsigned long comboPoints,
                            long flatModMs, long pctMod) {
    if (comboPoints < 1 || comboPoints > 5 || baseMs == 0 || maxMs == 0 ||
        baseMs == maxMs || maxMs < baseMs)
        return 0;

    unsigned long raw = baseMs + (maxMs - baseMs) * comboPoints / 5UL;
    long adjusted = (long)raw + flatModMs;
    if (adjusted <= 0) return 0;
    long percent = 100L + pctMod;
    if (percent <= 0) return 0;
    // Aura durations and spell mods are small; guard the 32-bit multiply so
    // the no-CRT x86 build never needs the compiler's 64-bit division helper.
    if (adjusted > 0x7fffffffL / percent) return 0;
    adjusted = adjusted * percent / 100L;
    return adjusted > 0 ? (unsigned long)adjusted : 0UL;
}

} // namespace TysComboDurationCore
