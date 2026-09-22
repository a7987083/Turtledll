#include <windows.h>
#include <cstdint>
#include "cooldown_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysCooldownCore {
namespace {

constexpr unsigned MAX_TRACKED=128;

struct Entry {
    unsigned long spellId;
    std::uint32_t startMs;
    std::uint32_t durationMs;
    std::uint32_t enable;
    std::uint32_t lastQueryMs;
    bool used;
    bool valid;
};

static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_engineQueries=0,g_queryFailures=0,g_spellCooldownPackets=0,g_clearCooldownPackets=0,g_cooldownCheatPackets=0,g_cooldownEventPackets=0,g_ignoredRemotePackets=0,g_deadlineWakes=0;
static volatile LONG g_dirtyEntries=0,g_deadlineRequeries=0;
static unsigned long g_lastPacketSpellId=0,g_lastChangedSpellId=0,g_lastChangeMs=0;
static char g_status[96]="NOT_INITIALIZED";

// Historical ClassicAPI and the 1.12 client helper both use unsigned 32-bit
// millisecond ticks. The helper writes duration first, then start, then enable.
// Signed arithmetic here breaks after ~24.9 days uptime and across the 32-bit
// tick wrap, so all deadline arithmetic below is modulo uint32_t.
using QueryFn = void (__fastcall *)(unsigned long spellId,
                                    unsigned long bookType,
                                    std::uint32_t* durationMs,
                                    std::uint32_t* startMs,
                                    std::uint32_t* enable);

static bool executable(unsigned long a){
    MEMORY_BASIC_INFORMATION m={};
    if(!a||VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    DWORD p=m.Protect&0xff;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}

static std::uint32_t tickNow(){return static_cast<std::uint32_t>(GetTickCount());}
static std::uint32_t elapsed32(std::uint32_t now,std::uint32_t then){return now-then;}

static std::uint32_t remaining32(const Entry&e,std::uint32_t now){
    if(!e.valid||e.durationMs==0)return 0;
    const std::uint32_t elapsed=elapsed32(now,e.startMs);
    return elapsed>=e.durationMs?0u:e.durationMs-elapsed;
}

static bool activeNow(const Entry&e,std::uint32_t now){
    return e.valid&&e.enable!=0&&e.durationMs!=0&&remaining32(e,now)!=0;
}

static Entry* slot(unsigned long spell,bool create){
    if(!spell)return 0;
    unsigned freei=MAX_TRACKED;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        if(g_entries[i].used&&g_entries[i].spellId==spell)return &g_entries[i];
        if(!g_entries[i].used&&freei==MAX_TRACKED)freei=i;
    }
    if(!create||freei==MAX_TRACKED)return 0;
    Entry&e=g_entries[freei];
    e=Entry{};
    e.used=true;
    e.spellId=spell;
    return &e;
}

static bool query(unsigned long spell,Entry*out){
    if(!spell||!out||!executable(WoW112::COOLDOWN_QUERY_HELPER))return false;
    ++g_engineQueries;
    std::uint32_t start=0,duration=0,enable=0;
    ((QueryFn)WoW112::COOLDOWN_QUERY_HELPER)(spell,0,&duration,&start,&enable);
    out->spellId=spell;
    out->startMs=start;
    out->durationMs=duration;
    out->enable=enable;
    out->lastQueryMs=tickNow();
    out->valid=true;
    return true;
}

static bool stateChanged(const Entry&a,const Entry&b){
    return !a.valid||a.startMs!=b.startMs||a.durationMs!=b.durationMs||a.enable!=b.enable;
}

static void markDirty(Entry*e){
    if(!e)return;
    if(e->valid){e->valid=false;++g_dirtyEntries;}
}

static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*p){
    if(op==WoW112::SMSG_SPELL_COOLDOWN_OPCODE){++g_spellCooldownPackets;}
    else if(op==WoW112::SMSG_CLEAR_COOLDOWN_OPCODE){++g_clearCooldownPackets;}
    else if(op==WoW112::SMSG_COOLDOWN_CHEAT_OPCODE){++g_cooldownCheatPackets;}
    else if(op==WoW112::SMSG_COOLDOWN_EVENT_OPCODE){++g_cooldownEventPackets;}
    else return;

    // For packet forms that expose a leading spell id, invalidate just that
    // tracked record. Exact CLEAR/CHEAT multi-record reset semantics remain
    // intentionally unclaimed until their packet layouts are fully recovered.
    unsigned long s=0;
    if(p&&TysNativeBus::read(p,&s)&&s){
        g_lastPacketSpellId=s;
        Entry*e=slot(s,true);
        markDirty(e);
    }
}

static void onTick(){
    const std::uint32_t now=tickNow();
    for(unsigned i=0;i<MAX_TRACKED;++i){
        Entry&e=g_entries[i];
        if(!e.used)continue;

        bool needQuery=!e.valid;
        if(!needQuery&&e.durationMs>0&&remaining32(e,now)==0){
            // Deadline wake: query exactly at/after expiry instead of polling the
            // engine every 50 ms. Subtraction is wrap-safe modulo uint32_t.
            ++g_deadlineWakes;
            ++g_deadlineRequeries;
            needQuery=true;
        }
        if(!needQuery)continue;

        Entry n=e;
        if(query(e.spellId,&n)){
            const bool changed=stateChanged(e,n);
            e=n;
            if(changed){
                g_lastChangedSpellId=e.spellId;
                g_lastChangeMs=now;
            }
        }else{
            ++g_queryFailures;
        }
    }
}

static int pushEntry(Lua50::State L,const Entry&e){
    Lua50::NewTable(L);
    const std::uint32_t now=tickNow();
    const std::uint32_t remain=remaining32(e,now);
    setNum(L,"spellId",e.spellId);
    setBool(L,"valid",e.valid);
    setBool(L,"active",activeNow(e,now));
    setNum(L,"startMs",e.startMs);
    setNum(L,"durationMs",e.durationMs);
    setNum(L,"recoveryTimeMs",e.durationMs);
    setNum(L,"startRecoveryTimeMs",e.startMs);
    setNum(L,"enable",e.enable);
    setNum(L,"remainingMs",remain);
    setBool(L,"ready",e.valid&&remain==0);
    setStr(L,"querySource","CLIENT_ENGINE_COOLDOWN_MANAGER");
    return 1;
}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;
    bool a=TysNativeBus::subscribeIncoming(&onIncoming);
    bool b=TysNativeBus::subscribeWorldTick(&onTick);
    InterlockedExchange(&g_inSub,a?1:0);
    InterlockedExchange(&g_tickSub,b?1:0);
    const char*s=(a&&b&&executable(WoW112::COOLDOWN_QUERY_HELPER))?"READY_COOLDOWN_CORE_C1R2":"PARTIAL_COOLDOWN_CORE_C1R2";
    unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;
    return a&&b;
}

const char* status(){return g_status;}

int dispatchStatus(Lua50::State L){
    initialize();
    Lua50::NewTable(L);
    setStr(L,"stage","CD1-R2");
    setStr(L,"status",g_status);
    setBool(L,"incomingSubscribed",g_inSub!=0);
    setBool(L,"worldTickSubscribed",g_tickSub!=0);
    setNum(L,"queryHelperAddress",WoW112::COOLDOWN_QUERY_HELPER);
    setBool(L,"queryHelperReady",executable(WoW112::COOLDOWN_QUERY_HELPER));
    setNum(L,"engineQueries",g_engineQueries);
    setNum(L,"queryFailures",g_queryFailures);
    setNum(L,"spellCooldownPackets",g_spellCooldownPackets);
    setNum(L,"clearCooldownPackets",g_clearCooldownPackets);
    setNum(L,"cooldownCheatPackets",g_cooldownCheatPackets);
    setNum(L,"cooldownEventPackets",g_cooldownEventPackets);
    setNum(L,"deadlineWakes",g_deadlineWakes);
    setNum(L,"deadlineRequeries",g_deadlineRequeries);
    setNum(L,"dirty",g_dirtyEntries);
    setNum(L,"lastPacketSpellId",g_lastPacketSpellId);
    setNum(L,"lastChangedSpellId",g_lastChangedSpellId);
    setNum(L,"lastChangeMs",g_lastChangeMs);
    setStr(L,"clockSemantics","UINT32_WRAP_SAFE_MILLISECONDS");
    setStr(L,"readySemantics","READY_ONLY_WHEN_ENGINE_REPORTS_NO_COOLDOWN");
    setStr(L,"resetSemantics","CLEAR_CHEAT_EXACT_MULTI_RECORD_RECONCILE_PENDING_DISASSEMBLY");
    setBool(L,"spellbookPolling",false);
    setBool(L,"backgroundThread",false);
    setBool(L,"directHook",false);
    setBool(L,"idleTickPath",true);
    return 1;
}

int dispatchGet(Lua50::State L){
    initialize();
    if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){
        Lua50::PushNil(L);Lua50::PushString(L,"BAD_SPELL_ID");return 2;
    }
    unsigned long s=(unsigned long)Lua50::ToNumber(L,2);
    Entry*e=slot(s,true);
    if(!e){Lua50::PushNil(L);Lua50::PushString(L,"CAPACITY");return 2;}
    Entry n=*e;
    if(!query(s,&n)){++g_queryFailures;Lua50::PushNil(L);Lua50::PushString(L,"QUERY_FAILED");return 2;}
    *e=n;
    return pushEntry(L,*e);
}

int dispatchList(Lua50::State L){
    initialize();
    Lua50::NewTable(L);
    int idx=1;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        if(!g_entries[i].used)continue;
        Lua50::PushNumber(L,idx++);
        pushEntry(L,g_entries[i]);
        Lua50::SetTable(L,-3);
    }
    return 1;
}

} // namespace TysCooldownCore
