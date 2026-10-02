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
constexpr std::uintptr_t ACTIVE_PLAYER_GUID_FN=WoW112::GET_ACTIVE_PLAYER_GUID;
constexpr std::uintptr_t SPELL_DB=WoW112::SPELL_DB;
constexpr std::uintptr_t SPELL_DB_RECORDS=SPELL_DB+0x08u;
constexpr std::uintptr_t SPELL_DB_MAX_ID=SPELL_DB+0x0Cu;
constexpr std::uintptr_t SPELL_RECOVERY_TIME=0x4Cu;
constexpr std::uintptr_t SPELL_CATEGORY_RECOVERY_TIME=0x50u;
constexpr std::uintptr_t SPELL_START_RECOVERY_CATEGORY=0x274u;
constexpr std::uintptr_t SPELL_START_RECOVERY_TIME=0x278u;
constexpr std::size_t SPELL_RECORD_MIN_SIZE=0x27Cu;

struct Entry {
    bool used;                              // +0x00
    bool active;                            // +0x01
    TysCooldownClassifier::Kind kind;       // +0x02
    TysCooldownClassifier::Source source;   // +0x03
    std::uint32_t spellId;                  // +0x04
    std::uint32_t startMs;                  // +0x08
    std::uint32_t durationMs;               // +0x0c
    std::uint32_t endMs;                    // +0x10
    std::uint32_t enable;                   // +0x14
    std::uint32_t lastQueryMs;              // +0x18
    std::uint32_t generation;               // +0x1c
    std::uint32_t engineGeneration;         // +0x20
};
static_assert(sizeof(Entry)==0x24,"API35 cooldown entry layout must remain 0x24 bytes");

struct DirtyEntry {
    std::uint32_t spellId;
    TysCooldownClassifier::Source source;
    unsigned char reserved[3];
};
static_assert(sizeof(DirtyEntry)==8,"API35 dirty entry layout must remain 8 bytes");

static Entry g_entries[MAX_TRACKED]={};
static DirtyEntry g_dirtyQueue[MAX_TRACKED]={};
static unsigned g_dirtyCount=0;
static unsigned g_activeCount=0;
static std::uint32_t g_nextDeadline=0;
static bool g_deadlineValid=false;
static std::uint32_t g_generation=0;
static std::uint32_t g_engineEpoch=1;
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
using ActivePlayerGuidFn = unsigned long long (__cdecl*)();

static bool executable(std::uintptr_t a){
    MEMORY_BASIC_INFORMATION m={};
    if(!a||VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    DWORD p=m.Protect&0xff;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool readable(std::uintptr_t a,std::size_t n){
    if(!a||!n)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&0xff;
    if(!(p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY))return false;
    const std::uintptr_t end=a+n;
    return end>=a&&end<=(std::uintptr_t)m.BaseAddress+m.RegionSize;
}

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}

static std::uint32_t tickNow(){return static_cast<std::uint32_t>(GetTickCount());}
static std::uint32_t elapsed32(std::uint32_t now,std::uint32_t then){return now-then;}
static std::uint32_t remaining32(const Entry&e,std::uint32_t now){
    if(!e.active)return 0;
    const std::int32_t left=(std::int32_t)(e.endMs-now);
    return left>0?(std::uint32_t)left:0u;
}
static bool activeNow(const Entry&e,std::uint32_t now){return e.used&&e.active&&e.enable!=0&&remaining32(e,now)!=0;}
static unsigned activeCount(std::uint32_t now){unsigned n=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(activeNow(g_entries[i],now))++n;return n;}
static unsigned dirtyCount(){return g_dirtyCount;}

static unsigned long long playerGuid(){
    if(!executable(ACTIVE_PLAYER_GUID_FN))return 0;
    return ((ActivePlayerGuidFn)ACTIVE_PLAYER_GUID_FN)();
}

static TysCooldownClassifier::SpellRecoveryFields recoveryFields(unsigned long spell){
    TysCooldownClassifier::SpellRecoveryFields f={};
    if(!spell||!readable(SPELL_DB,0x14u)||!readable(SPELL_DB_RECORDS,sizeof(std::uintptr_t))||!readable(SPELL_DB_MAX_ID,sizeof(std::uint32_t)))return f;
    const std::uintptr_t records=*(const std::uintptr_t*)SPELL_DB_RECORDS;
    const std::uint32_t maxId=*(const std::uint32_t*)SPELL_DB_MAX_ID;
    if(!records||spell>maxId)return f;
    const std::uintptr_t slotAddr=records+(std::uintptr_t)spell*sizeof(std::uintptr_t);
    if(!readable(slotAddr,sizeof(std::uintptr_t)))return f;
    const std::uintptr_t rec=*(const std::uintptr_t*)slotAddr;
    if(!rec||!readable(rec,SPELL_RECORD_MIN_SIZE))return f;
    f.available=true;
    f.recoveryTime=*(const std::uint32_t*)(rec+SPELL_RECOVERY_TIME);
    f.categoryRecoveryTime=*(const std::uint32_t*)(rec+SPELL_CATEGORY_RECOVERY_TIME);
    f.startRecoveryCategory=*(const std::uint32_t*)(rec+SPELL_START_RECOVERY_CATEGORY);
    f.startRecoveryTime=*(const std::uint32_t*)(rec+SPELL_START_RECOVERY_TIME);
    return f;
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
    const std::uint32_t now=tickNow();
    out->used=true;out->source=source;out->spellId=spell;out->startMs=start;out->durationMs=duration;out->endMs=start+duration;out->enable=enable;out->lastQueryMs=now;
    out->active=enable!=0&&duration!=0&&(std::int32_t)(out->endMs-now)>0;
    const TysCooldownClassifier::SpellRecoveryFields fields=recoveryFields(spell);
    out->kind=TysCooldownClassifier::classify(out->active,source,fields);
    out->generation=++g_generation;
    out->engineGeneration=g_engineEpoch;
    return true;
}

static bool stateChanged(const Entry&a,const Entry&b){return a.generation==0||a.active!=b.active||a.startMs!=b.startMs||a.durationMs!=b.durationMs||a.enable!=b.enable||a.kind!=b.kind;}
static bool sourceShouldReplace(TysCooldownClassifier::Source oldSource,TysCooldownClassifier::Source newSource){
    const unsigned oldValue=(unsigned)oldSource,newValue=(unsigned)newSource;
    if(oldValue>=6u)return true;
    if(newValue>=6u)return false;
    return newValue>=oldValue;
}
static void markDirtySpell(std::uint32_t spell,TysCooldownClassifier::Source source){
    if(!spell)return;
    for(unsigned i=0;i<g_dirtyCount;++i){
        if(g_dirtyQueue[i].spellId!=spell)continue;
        if(sourceShouldReplace(g_dirtyQueue[i].source,source))g_dirtyQueue[i].source=source;
        return;
    }
    if(g_dirtyCount>=MAX_TRACKED){++g_dirtyOverflow;return;}
    DirtyEntry&d=g_dirtyQueue[g_dirtyCount++];d.spellId=spell;d.source=source;d.reserved[0]=d.reserved[1]=d.reserved[2]=0;
    ++g_dirtyEntries;
}
static void markDirtyAllActive(TysCooldownClassifier::Source source){for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].active)markDirtySpell(g_entries[i].spellId,source);}
static bool isLocalGuid(unsigned long long guid){const unsigned long long me=playerGuid();return guid!=0&&me!=0&&guid==me;}

static TysCooldownTransition::Snapshot snapshot(const Entry&e,std::uint32_t now){
    TysCooldownTransition::Snapshot s={};s.queryOk=e.generation!=0;s.active=e.active;s.spellId=e.spellId;s.startMs=e.startMs;s.durationMs=e.durationMs;s.endMs=e.endMs;s.remainingMs=e.active?remaining32(e,now):0;s.enable=e.enable;s.kind=e.kind;s.source=e.source;return s;
}
static void emitTransition(const Entry&oldEntry,const Entry&newEntry,std::uint32_t now){
    const TysCooldownTransition::Snapshot oldState=snapshot(oldEntry,now),newState=snapshot(newEntry,now);
    const TysCooldownTransition::Event event=TysCooldownTransition::decide(oldEntry.generation!=0,oldState,newState);
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
        if(old.active&&!newState.active)++g_clearReady;
        if(TysCooldownTransition::isSpellToGcd(oldState,newState))++g_clearToGcd;
    }else if(source==TysCooldownClassifier::SOURCE_COOLDOWN_CHEAT){
        if(old.active&&!newState.active)++g_resetReady;
        if(TysCooldownTransition::isSpellToGcd(oldState,newState))++g_resetToGcd;
    }
    emitTransition(old,next,now);e=next;return true;
}

static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*p){
    if(!p)return;

    if(op==WoW112::SMSG_SPELL_GO_OPCODE){
        ++g_spellGoPackets;
        unsigned long long firstGuid=0,localGuid=0;std::uint32_t spell=0;
        if(!TysNativeBus::readPackedGuid(p,&firstGuid)||!TysNativeBus::readPackedGuid(p,&localGuid)||!TysNativeBus::read(p,&spell)){++g_parseFailures;return;}
        if(!isLocalGuid(localGuid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=spell;markDirtySpell(spell,TysCooldownClassifier::SOURCE_SPELL_GO);return;
    }

    if(op==WoW112::SMSG_SPELL_COOLDOWN_OPCODE){
        ++g_spellCooldownPackets;
        unsigned long long guid=0;
        if(!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        while(p->read<=p->size&&p->size-p->read>=8u){
            std::uint32_t spell=0,packetMs=0;
            if(!TysNativeBus::read(p,&spell)||!TysNativeBus::read(p,&packetMs)){++g_parseFailures;return;}
            if(spell){g_lastPacketSpellId=spell;markDirtySpell(spell,TysCooldownClassifier::SOURCE_SPELL_COOLDOWN);}
        }
        return;
    }

    if(op==WoW112::SMSG_COOLDOWN_EVENT_OPCODE){
        ++g_cooldownEventPackets;
        std::uint32_t spell=0;unsigned long long guid=0;
        if(!TysNativeBus::read(p,&spell)||!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=spell;markDirtySpell(spell,TysCooldownClassifier::SOURCE_COOLDOWN_EVENT);return;
    }

    if(op==WoW112::SMSG_CLEAR_COOLDOWN_OPCODE){
        ++g_clearCooldownPackets;
        std::uint32_t spell=0;unsigned long long guid=0;
        if(!TysNativeBus::read(p,&spell)||!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        Entry*e=slot(spell,false);if(e&&e->active)++g_clearMatchedActive;
        g_lastPacketSpellId=spell;markDirtySpell(spell,TysCooldownClassifier::SOURCE_CLEAR_COOLDOWN);return;
    }

    if(op==WoW112::SMSG_COOLDOWN_CHEAT_OPCODE){
        ++g_cooldownCheatPackets;
        unsigned long long guid=0;
        if(!TysNativeBus::read(p,&guid)){++g_parseFailures;return;}
        if(!isLocalGuid(guid)){++g_ignoredRemotePackets;return;}
        g_lastPacketSpellId=0;
        unsigned affected=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].active)++affected;
        g_resetAffected+=affected;markDirtyAllActive(TysCooldownClassifier::SOURCE_COOLDOWN_CHEAT);return;
    }
}

static void recomputeDeadline(std::uint32_t now){
    g_activeCount=0;g_deadlineValid=false;g_nextDeadline=0;
    std::uint32_t bestDelta=0xffffffffu;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        const Entry&e=g_entries[i];if(!e.used||!e.active)continue;
        ++g_activeCount;
        std::int32_t signedDelta=(std::int32_t)(e.endMs-now);
        std::uint32_t delta=signedDelta>0?(std::uint32_t)signedDelta:0u;
        if(delta<bestDelta){bestDelta=delta;g_nextDeadline=now+(delta?delta:25u);g_deadlineValid=true;}
    }
}

static void onTick(){
    const std::uint32_t now=tickNow();
    if(g_dirtyCount){
        DirtyEntry local[MAX_TRACKED]={};const unsigned count=g_dirtyCount;
        for(unsigned i=0;i<count;++i)local[i]=g_dirtyQueue[i];
        g_dirtyCount=0;
        for(unsigned i=0;i<count;++i){
            Entry*e=slot(local[i].spellId,true);if(!e)continue;
            const Entry old=*e;
            if(reconcile(*e,local[i].source)){if(stateChanged(old,*e)){g_lastChangedSpellId=e->spellId;g_lastChangeMs=now;g_lastChangedSource=local[i].source;}}
            else ++g_queryFailures;
        }
        recomputeDeadline(now);return;
    }

    if(!g_activeCount||!g_deadlineValid||(std::int32_t)(now-g_nextDeadline)<0)return;
    ++g_deadlineWakes;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        Entry&e=g_entries[i];if(!e.used||!e.active||(std::int32_t)(now-e.endMs)<0)continue;
        ++g_deadlineRequeries;const Entry old=e;
        if(reconcile(e,TysCooldownClassifier::SOURCE_DEADLINE_RECHECK)){if(stateChanged(old,e)){g_lastChangedSpellId=e.spellId;g_lastChangeMs=now;g_lastChangedSource=TysCooldownClassifier::SOURCE_DEADLINE_RECHECK;}}
        else ++g_queryFailures;
    }
    recomputeDeadline(now);
}

static int pushEntry(Lua50::State L,const Entry&e){
    Lua50::NewTable(L);const std::uint32_t now=tickNow();const std::uint32_t remain=remaining32(e,now);
    setNum(L,"spellId",e.spellId);setBool(L,"valid",e.generation!=0);setBool(L,"active",e.active);setNum(L,"startMs",e.startMs);setNum(L,"durationMs",e.durationMs);setNum(L,"recoveryTimeMs",e.durationMs);setNum(L,"startRecoveryTimeMs",e.startMs);setNum(L,"enable",e.enable);setNum(L,"remainingMs",remain);setBool(L,"ready",e.generation!=0&&!e.active);setNum(L,"kind",e.kind);setStr(L,"kindName",TysCooldownClassifier::kindName(e.kind));setNum(L,"source",e.source);setStr(L,"sourceName",TysCooldownClassifier::sourceName(e.source));setStr(L,"querySource","CLIENT_ENGINE_COOLDOWN_MANAGER");return 1;
}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;
    ++g_engineEpoch;if(g_engineEpoch==0)++g_engineEpoch;
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
