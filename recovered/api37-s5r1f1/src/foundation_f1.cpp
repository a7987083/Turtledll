#include <windows.h>
#include "foundation_f1.h"
#include "foundation_stats.h"
#include "los_pair_cache.h"

namespace TysFoundationF1 {
namespace {
static bool executable(unsigned long address){
    MEMORY_BASIC_INFORMATION m={};
    if(!address || VirtualQuery((void*)address,&m,sizeof(m))!=sizeof(m) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    DWORD p=m.Protect&0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
static void setNum(Lua50::State L,const char* k,double v){ Lua50::PushString(L,k); Lua50::PushNumber(L,v); Lua50::SetTable(L,-3); }
static void setBool(Lua50::State L,const char* k,bool v){ Lua50::PushString(L,k); Lua50::PushBool(L,v); Lua50::SetTable(L,-3); }
static void setStr(Lua50::State L,const char* k,const char* v){ Lua50::PushString(L,k); Lua50::PushString(L,v); Lua50::SetTable(L,-3); }
}

int dispatchStatus(Lua50::State L){
    const TysFoundationStats::GuidStats g=TysFoundationStats::snapshotGuid();
    const TysLosPairCache::Stats los=TysLosPairCache::stats();
    Lua50::NewTable(L);
    setStr(L,"stage","F1");
    setStr(L,"clock","TysClock32");
    setBool(L,"clockWrapSafe",true);
    setNum(L,"guidFastAddress",0x00464870UL);
    setBool(L,"guidFastReady",executable(0x00464870UL));
    setNum(L,"guidFastAttempts",g.fastAttempts);
    setNum(L,"guidFastHits",g.fastHits);
    setNum(L,"guidFastMisses",g.fastMisses);
    setNum(L,"guidFastRejected",g.fastRejected);
    setNum(L,"guidFallbackAttempts",g.fallbackAttempts);
    setNum(L,"guidFallbackHits",g.fallbackHits);
    setNum(L,"guidFallbackMisses",g.fallbackMisses);
    setNum(L,"losCacheSlots",TysLosPairCache::SLOT_COUNT);
    setNum(L,"losCacheTtlMs",TysLosPairCache::DEFAULT_TTL_MS);
    setNum(L,"losCacheHits",los.hits);
    setNum(L,"losCacheMisses",los.misses);
    setNum(L,"losCacheExpired",los.expired);
    setNum(L,"losCacheCollisionMisses",los.collisionMisses);
    setNum(L,"losCacheStores",los.stores);
    setNum(L,"losCacheReplacements",los.replacements);
    setNum(L,"losRecomputes",los.recomputes);
    setBool(L,"guidFallbackScannerPersistent",false);
    setBool(L,"backgroundWorker",false);
    return 1;
}
}
