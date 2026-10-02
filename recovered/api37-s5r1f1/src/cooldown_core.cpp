#include <windows.h>
#include <cstdint>
#include "cooldown_core.h"
#include "cooldown_classifier.h"
#include "cooldown_transition.h"
#include "custom_event_bridge.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysCooldownCore {
namespace {

constexpr unsigned MAX_TRACKED=128;
constexpr std::uintptr_t UNIT_GUID_FN=0x00515970u;

struct Entry {
    unsigned long spellId;
    std::uint32_t startMs;
    std::uint32_t durationMs;
    std::uint32_t enable;
    std::uint32_t lastQueryMs;
    TysCooldownClassifier::Kind kind;
    TysCooldownClassifier::Source source;
    TysCooldownClassifier::Source pendingSource;
    bool used;
    bool valid;
    bool observed;
    bool active;
};

static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_engineQueries=0,g_queryFailures=0,g_parseFailures=0,g_spellGoPackets=0,g_spellCooldownPackets=0,g_clearCooldownPackets=0,g_cooldownCheatPackets=0,g_cooldownEventPackets=0,g_ignoredRemotePackets=0,g_deadlineWakes=0;
static volatile LONG g_dirtyEntries=0,g_deadlineRequeries=0,g_dirtyOverflow=0;
static volatile LONG g_startedEvents=0,g_changedEvents=0,g_readyEvents=0,g_eventFailures=0;
static volatile LONG g_clearMatchedActive=0,g_clearReady=0,g_clearToGcd=0,g_resetAffected=0,g_resetReady=0,g_resetToGcd=0;
static unsigned long g_lastPacketSpellId=0,g_lastChangedSpellId=0,g_lastChangeMs=0;
static TysCooldownClassifier::Source g_lastChangedSource=TysCooldownClassifier::SOURCE_EXPLICIT_ENGINE_QUERY;
static char g_status[96]="NOT_INITIALIZED";

// The 1.12 engine cooldown query writes duration, start and enable as unsigned
// 32-bit millisecond values. Keep the same unsigned clock domain used by
// GetTime()/the client helper; signed ticks break after 2^31 ms uptime.
using QueryFn = void (__fastcall *)(unsigned long spellId,
                                    unsigned long bookType,
                                    std::uint32_t* durationMs,
                                    std::uint32_t* startMs,
                                    std::uint32_t* enable);
using UnitGuidFn = unsigned long long(__fastcall*)(const char*);

static bool executable(std::uintptr_t a){
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
static std::uint32_t remaining32(const Entry&e,std::uint32_t now){if(!e.valid||e.durationMs==0)return 0;const std::uint32_t elapsed=elapsed32(now,e.startMs);return elapsed>=e.durationMs?0u:e.durationMs-elapsed;}
static bool activeNow(const Entry&e,std::uint32_t now){return e.valid&&e.enable!=0&&e.durationMs!=0&&remaining32(e,now)!=0;}
static unsigned activeCount(std::uint32_t now){unsigned n=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&activeNow(g_entries[i],now))++n;return n;}
static unsigned dirtyCount(){unsigned n=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&!g_entries[i].valid)++n;return n;}

static unsigned long long playerGuid(){
    if(!executable(UNIT_GUID_FN))return 0;
    return ((UnitGuidFn)UNIT_GUID_FN)("player");
}

static Entry* slot(unsigned long spell,bool create){
    if(!spell)return 0;unsigned freei=MAX_TRACKED;
    for(unsigned i=0;i<MAX_TRACKED;++i){if(g_entries[i].used&&g_entries[i].spellId==spell)return &g_entries[i];if(!g_entries[i].used&&freei==MAX_TRACKED)freei=i;}
    if(!create||freei==MAX_TRACKED)return 0;Entry&e=g_entries[freei];e=Entry{};e.used=true;e.spellId=spell;return &e;
}

static bool query(unsigned long spell,TysCooldownClassifier::Source source,Entry*out){
    if(!spell||!out||!executable(WoW112::COOLDOWN_QUERY_HELPER))return false;
    ++g_engineQueries;std::uint32_t start=0,duration=0,enable=0;
    ((QueryFn)WoW112::COOLDOWN_QUERY_HELPER)(spell,0,&duration,&start,&enable);
    out->spellId=spell;out->startMs=start;out->durationMs=duration;out->enable=enable;out->lastQueryMs=tickNow();out->valid=true;out->observed=true;out->source=source;out->pendingSource=source;
    out->active=out->enable!=0&&out->durationMs!=0&&remaining32(*out,out->lastQueryMs)!=0;
    const TysCooldownClassifier::SpellRecoveryFields fields={true,duration,0,start,start};
    out->kind=TysCooldownClassifier::classify(out->active,source,fields);return true;
}

static bool stateChanged(const Entry&a,const Entry&b){return !a.observed||a.active!=b.active||a.startMs!=b.startMs||a.durationMs!=b.durationMs||a.enable!=b.enable||a.kind!=b.kind;}
static void markDirty(Entry*e,TysCooldownClassifier::Source source){if(e){e->pendingSource=source;if(e->valid){e->valid=false;++g_dirtyEntries;}}}
static void markDirtyAllTracked(TysCooldownClassifier::Source source){for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used)markDirty(&g_entries[i],source);}
static bool isLocalGuid(unsigned long long guid){const unsigned long long me=playerGuid();return guid!=0&&me!=0&&guid==me;}

static TysCooldownTransition::Snapshot snapshot(const Entry&e,std::uint32_t now){
    TysCooldownTransition::Snapshot s={};s.queryOk=e.valid;s.active=e.active;s.spellId=e.spellId;s.startMs=e.startMs;s.durationMs=e.durationMs;s.endMs=e.startMs+e.durationMs;s.remainingMs=e.active?remaining32(e,now):0;s.enable=e.enable;s.kind=e.kind;s.source=e.source;return s;
}
static void emitTransition(const Entry&oldEntry,const Entry&newEntry,std::uint32_t now){
    const TysCooldownTransition::Snapshot oldState=snapshot(oldEntry,now),newState=snapshot(newEntry,now);
    const TysCooldownTransition::Event event=TysCooldownTransition::decide(oldEntry.observed,oldState,newState);
    bool ok=true;
    if(event==TysCooldownTransition::EVENT_STARTED){ok=TysCustomEvents::emitCooldownStarted(newState.spellId,newState.startMs,newState.durationMs,newState.endMs,newState.remainingMs,newState.enable,newState.kind,newState.source);if(ok)++g_startedEvents;}
    else if(event==TysCooldownTransition::EVENT_CHANGED){ok=TysCustomEvents::emitCooldownChanged(newState.spellId,newState.startMs,newState.durationMs,newState.endMs,newState.remainingMs,newState.enable,newState.kind,newState.source);if(ok)++g_changedEvents;}
    else if(event==TysCooldownTransition::EVENT_READY){ok=TysCustomEvents::emitCooldownReady(newState.spellId,newState.startMs,newState.durationMs,newState.endMs,newState.remainingMs,newState.enable,newState.kind,newState.source);if(ok)++g_readyEvents;}
    if(event!=TysCooldownTransition::EVENT_NONE){if(!ok)++g_eventFailures;g_lastChangedSpellId=newEntry.spellId;g_lastChangeMs=now;g_lastChangedSource=newEntry.source;}
}
static bool reconcile(Entry&e,TysCooldownClassifier::Source source){
    const Entry old=e;Entry next=e;if(!query(e.spellId,source,&next))return false;
    const std::uint32_t now=next.lastQueryMs;
    const TysCooldownTransition::Snapshot oldState=snapshot(old,now),newState=snapshot(next,now);
    if(source==TysCooldownClassifier::SOURCE_CLEAR_COOLDOWN){
        if(old.active)++g_clearMatchedActive;
        if(old.active&&!newState.active)++g_clearReady;
        if(TysCooldownTransition::isSpellToGcd(oldState,newState))++g_clearToGcd;
    }else if(source==TysCooldownClassifier::SOURCE_COOLDOWN_CHEAT){
        if(old.active)++g_resetAffected;
        if(old.active&&!newState.active)++g_resetReady;
        if(TysCooldownTransition::isSpellToGcd(oldState,newState))++g_resetToGcd;
    }
    emitTransition(old,next,now);e=next;return true;
}

static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*p){
    if(!p)return;

    // Vanilla 1.12 packet layouts are confirmed by the classic protocol:
    // SPELL_COOLDOWN : uint64 casterGuid, repeated {uint32 spellId,uint32 ms}
    // CLEAR_COOLDOWN : uint32 spellId, uint64 targetGuid
    // COOLDOWN_CHEAT : uint64 targetGuid (clear all cooldowns for that unit)
    // COOLDOWN_EVENT : uint32 spellId, uint64 casterGuid
    if(op==WoW112::SMSG_SPELL_GO_OPCODE){
        ++g_spellGoPackets;
        return;
    }

    if(op==WoW112::SMSG_SPELL_COOLDOWN_OPCODE){
        ++g_spellCooldownPackets;
        unsigned long long guid=0;
        if(!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        // The packet may contain multiple spell/cooldown pairs. Existing tracked
        // records are invalidated in one pass and reconciled from the engine
        // after the stock handler consumes the packet; no packet-body cooldown
        // value is treated as authoritative by this module.
        markDirtyAllTracked(TysCooldownClassifier::SOURCE_SPELL_COOLDOWN);
        return;
    }

    if(op==WoW112::SMSG_CLEAR_COOLDOWN_OPCODE){
        ++g_clearCooldownPackets;
        unsigned long s=0;unsigned long long guid=0;
        if(!TysNativeBus::read(p,&s)||!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=s;markDirty(slot(s,false),TysCooldownClassifier::SOURCE_CLEAR_COOLDOWN);
        return;
    }

    if(op==WoW112::SMSG_COOLDOWN_CHEAT_OPCODE){
        ++g_cooldownCheatPackets;
        unsigned long long guid=0;
        if(!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=0;
        // Server semantics: RemoveAllSpellCooldown() then SMSG_COOLDOWN_CHEAT
        // carrying only the target GUID. Every tracked cooldown must requery.
        markDirtyAllTracked(TysCooldownClassifier::SOURCE_COOLDOWN_CHEAT);
        return;
    }

    if(op==WoW112::SMSG_COOLDOWN_EVENT_OPCODE){
        ++g_cooldownEventPackets;
        unsigned long s=0;unsigned long long guid=0;
        if(!TysNativeBus::read(p,&s)||!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=s;markDirty(slot(s,false),TysCooldownClassifier::SOURCE_COOLDOWN_EVENT);
    }
}

static void onTick(){
    const std::uint32_t now=tickNow();
    for(unsigned i=0;i<MAX_TRACKED;++i){
        Entry&e=g_entries[i];if(!e.used)continue;
        bool needQuery=!e.valid;TysCooldownClassifier::Source source=e.pendingSource;
        if(!needQuery&&e.active&&e.durationMs>0&&remaining32(e,now)==0){++g_deadlineWakes;++g_deadlineRequeries;needQuery=true;source=TysCooldownClassifier::SOURCE_DEADLINE_RECHECK;}
        if(!needQuery)continue;
        const Entry old=e;
        if(reconcile(e,source)){if(stateChanged(old,e)){g_lastChangedSpellId=e.spellId;g_lastChangeMs=now;g_lastChangedSource=source;}}
        else ++g_queryFailures;
    }
}

static int pushEntry(Lua50::State L,const Entry&e){
    Lua50::NewTable(L);const std::uint32_t now=tickNow();const std::uint32_t remain=remaining32(e,now);
    setNum(L,"spellId",e.spellId);setBool(L,"valid",e.valid);setBool(L,"active",e.active);setNum(L,"startMs",e.startMs);setNum(L,"durationMs",e.durationMs);setNum(L,"recoveryTimeMs",e.durationMs);setNum(L,"startRecoveryTimeMs",e.startMs);setNum(L,"enable",e.enable);setNum(L,"remainingMs",remain);setBool(L,"ready",e.valid&&!e.active);setNum(L,"kind",e.kind);setStr(L,"kindName",TysCooldownClassifier::kindName(e.kind));setNum(L,"source",e.source);setStr(L,"sourceName",TysCooldownClassifier::sourceName(e.source));setStr(L,"querySource","CLIENT_ENGINE_COOLDOWN_MANAGER");return 1;
}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;
    bool a=TysNativeBus::subscribeIncoming(&onIncoming);bool b=TysNativeBus::subscribeWorldTick(&onTick);
    InterlockedExchange(&g_inSub,a?1:0);InterlockedExchange(&g_tickSub,b?1:0);
    const bool c=TysCustomEvents::ensureCooldownEvents();
    const char*s=(a&&b&&c&&executable(WoW112::COOLDOWN_QUERY_HELPER))?"READY_COOLDOWN_CORE_C1R2":"PARTIAL_COOLDOWN_CORE_C1R2";
    unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;return a&&b;
}
const char* status(){return g_status;}

int dispatchStatus(Lua50::State L){
    initialize();const std::uint32_t now=tickNow();Lua50::NewTable(L);setStr(L,"stage","CD1-R2");setStr(L,"status",g_status);setBool(L,"incomingSubscribed",g_inSub!=0);setBool(L,"worldTickSubscribed",g_tickSub!=0);setNum(L,"queryHelperAddress",WoW112::COOLDOWN_QUERY_HELPER);setBool(L,"queryHelperReady",executable(WoW112::COOLDOWN_QUERY_HELPER));setNum(L,"active",activeCount(now));setNum(L,"dirty",dirtyCount());setNum(L,"engineQueries",g_engineQueries);setNum(L,"queryFailures",g_queryFailures);setNum(L,"parseFailure",g_parseFailures);setNum(L,"dirtyOverflow",g_dirtyOverflow);setNum(L,"spellGoPackets",g_spellGoPackets);setNum(L,"spellCooldownPackets",g_spellCooldownPackets);setNum(L,"clearCooldownPackets",g_clearCooldownPackets);setNum(L,"cooldownCheatPackets",g_cooldownCheatPackets);setNum(L,"cooldownEventPackets",g_cooldownEventPackets);setNum(L,"ignoredRemotePackets",g_ignoredRemotePackets);setNum(L,"deadlineWakes",g_deadlineWakes);setNum(L,"deadlineRequeries",g_deadlineRequeries);setNum(L,"started",g_startedEvents);setNum(L,"changed",g_changedEvents);setNum(L,"ready",g_readyEvents);setNum(L,"startedEvents",g_startedEvents);setNum(L,"changedEvents",g_changedEvents);setNum(L,"readyEvents",g_readyEvents);setNum(L,"eventFailures",g_eventFailures);setNum(L,"clearMatchedActive",g_clearMatchedActive);setNum(L,"clearReady",g_clearReady);setNum(L,"clearToGcd",g_clearToGcd);setNum(L,"resetAffected",g_resetAffected);setNum(L,"resetReady",g_resetReady);setNum(L,"resetToGcd",g_resetToGcd);setNum(L,"eventStartedSlot",TysCustomEvents::cooldownStartedSlot());setNum(L,"eventChangedSlot",TysCustomEvents::cooldownChangedSlot());setNum(L,"eventReadySlot",TysCustomEvents::cooldownReadySlot());setNum(L,"lastPacketSpellId",g_lastPacketSpellId);setNum(L,"lastChangedSpellId",g_lastChangedSpellId);setNum(L,"lastChangeMs",g_lastChangeMs);setNum(L,"lastChangedSource",g_lastChangedSource);setStr(L,"lastChangedSourceName",TysCooldownClassifier::sourceName(g_lastChangedSource));setStr(L,"clockSemantics","UINT32_WRAP_SAFE_MILLISECONDS");setStr(L,"readySemantics","READY_ONLY_WHEN_ENGINE_REPORTS_NO_COOLDOWN");setStr(L,"resetSemantics","CLEAR_OR_CHEAT_POST_HANDLER_ENGINE_REQUERY");setStr(L,"packetLayoutSemantics","VANILLA1121_GUID_FILTERED");setStr(L,"scope","LOCAL_PLAYER_SPELLS");setStr(L,"tracking","NATIVEBUS_DIRTY_IDS_ENGINE_QUERY_ACTIVE_DEADLINE");setStr(L,"queryMode","EXPLICIT_QUERY_NO_BACKGROUND_WORK");setBool(L,"spellbookPolling",false);setBool(L,"objectManagerPolling",false);setBool(L,"backgroundThread",false);setBool(L,"directHook",false);setBool(L,"idleTickPath",true);return 1;
}

int dispatchGet(Lua50::State L){
    initialize();if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_SPELL_ID");return 2;}
    unsigned long s=(unsigned long)Lua50::ToNumber(L,2);Entry*e=slot(s,true);if(!e){Lua50::PushNil(L);Lua50::PushString(L,"CAPACITY");return 2;}Entry n=*e;
    if(!query(s,TysCooldownClassifier::SOURCE_EXPLICIT_ENGINE_QUERY,&n)){++g_queryFailures;Lua50::PushNil(L);Lua50::PushString(L,"QUERY_HELPER_UNAVAILABLE");return 2;}emitTransition(*e,n,n.lastQueryMs);*e=n;return pushEntry(L,*e);
}
int dispatchList(Lua50::State L){initialize();Lua50::NewTable(L);int idx=1;for(unsigned i=0;i<MAX_TRACKED;++i){if(!g_entries[i].used)continue;Lua50::PushNumber(L,idx++);pushEntry(L,g_entries[i]);Lua50::SetTable(L,-3);}return 1;}

} // namespace TysCooldownCore
