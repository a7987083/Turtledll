#include <windows.h>
#include "foundation_f1.h"
#include "wow112_offsets.h"
#include "los_pair_cache.h"

namespace TysFoundationF1 {
namespace {
static bool g_ready=false;
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

bool initialize(){ g_ready=true; return true; }
const char* status(){ return g_ready?"READY_FOUNDATION_F1":"NOT_INITIALIZED"; }

int dispatchStatus(Lua50::State L){
    Lua50::NewTable(L);
    setStr(L,"stage","F1");
    setStr(L,"status",status());
    setNum(L,"guidFastAddress",0x00464870UL);
    setBool(L,"guidFastReady",executable(0x00464870UL));
    const char* zeroFields[]={
        "guidFastAttempts","guidFastHits","guidFastMisses","guidFastRejected",
        "guidFallbackAttempts","guidFallbackHits","guidFallbackMisses",
        "losCacheHits","losCacheMisses","losCacheExpired","losCacheCollisionMisses",
        "losCacheStores","losCacheReplacements","losRecomputes"
    };
    for(unsigned i=0;i<sizeof(zeroFields)/sizeof(zeroFields[0]);++i) setNum(L,zeroFields[i],0);
    setBool(L,"guidFallbackScannerPersistent",false);
    setNum(L,"losCacheSlots",TysLosPairCache::SLOT_COUNT);
    setNum(L,"losCacheTtlMs",TysLosPairCache::DEFAULT_TTL_MS);
    setBool(L,"clockWrapSafe",true);
    setStr(L,"clock","TysClock32");
    setBool(L,"backgroundWorker",false);
    return 1;
}
}
