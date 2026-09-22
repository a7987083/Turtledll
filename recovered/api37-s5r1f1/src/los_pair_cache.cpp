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
static Stats g_stats = {};

static void canonicalize(Guid* a, Guid* b) {
    if (!a || !b) return;
    if (*a > *b) { Guid t=*a; *a=*b; *b=t; }
}

static unsigned int slotFor(Guid lo, Guid hi) {
    unsigned int x=static_cast<unsigned int>(lo)^static_cast<unsigned int>(lo>>32);
    unsigned int y=static_cast<unsigned int>(hi)^static_cast<unsigned int>(hi>>32);
    unsigned int h=x*0x9E3779B1U;
    h^=y+0x85EBCA6BU+(h<<6)+(h>>2);
    return h&(SLOT_COUNT-1U);
}

void reset() {
    g_stats=Stats{};
    for(unsigned int i=0;i<SLOT_COUNT;++i){
        g_entries[i].lo=0; g_entries[i].hi=0; g_entries[i].tick=0;
        g_entries[i].value=0; g_entries[i].valid=0;
    }
}

bool tryGet(Guid a, Guid b, Tick32 now, Tick32 ttlMs, bool* result) {
    if(!a||!b||!result||ttlMs==0U) return false;
    canonicalize(&a,&b);
    Entry& e=g_entries[slotFor(a,b)];
    if(!e.valid){ ++g_stats.misses; return false; }
    if(e.lo!=a||e.hi!=b){ ++g_stats.misses; ++g_stats.collisionMisses; return false; }
    const Tick32 age=now-e.tick;
    if(age>=ttlMs){ ++g_stats.misses; ++g_stats.expired; return false; }
    *result=e.value!=0;
    ++g_stats.hits;
    return true;
}

void put(Guid a, Guid b, Tick32 now, bool result) {
    if(!a||!b) return;
    canonicalize(&a,&b);
    Entry& e=g_entries[slotFor(a,b)];
    if(e.valid&&(e.lo!=a||e.hi!=b)) ++g_stats.replacements;
    e.lo=a; e.hi=b; e.tick=now; e.value=result?1U:0U; e.valid=1U;
    ++g_stats.stores;
}

void noteRecompute(){ ++g_stats.recomputes; }
Stats stats(){ return g_stats; }
}
