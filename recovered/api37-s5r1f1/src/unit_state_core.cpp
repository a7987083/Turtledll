#include <windows.h>
#include <cstdint>
#include "unit_state_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"
#include "custom_event_bridge.h"

namespace TysUnitStateCore {
namespace {

constexpr unsigned MAX_TRACKED=128;
constexpr std::uintptr_t FAST_GUID_LOOKUP=WoW112::FAST_GUID_LOOKUP;
constexpr std::uintptr_t UNIT_TOKEN_RESOLVER=WoW112::RESOLVE_UNIT_TOKEN;
constexpr std::uint32_t OBJECT_TYPE_UNIT=3u;
constexpr std::uint32_t OBJECT_TYPE_PLAYER=4u;
constexpr std::uint32_t MAX_POWER_TYPE=4u;

// API36 US1-R2 snapshot layout recovered from 0x100450D6.
constexpr std::uint32_t OFF_OBJECT_DESCRIPTOR=0x08u;
constexpr std::uint32_t OFF_OBJECT_TYPE=0x14u;
constexpr std::uint32_t OFF_OBJECT_GUID=0x30u;
constexpr std::uint32_t OFF_HEALTH=0x58u;
constexpr std::uint32_t OFF_POWER1=0x5Cu;
constexpr std::uint32_t OFF_MAXHEALTH=0x70u;
constexpr std::uint32_t OFF_MAXPOWER1=0x74u;
constexpr std::uint32_t OFF_POWER_TYPE_BYTE=0x93u;
constexpr std::uint32_t OFF_UNIT_FLAGS=0xB8u;
constexpr std::uint32_t OFF_DYNAMIC_FLAGS=0x23Cu;
constexpr std::uint32_t UNIT_FLAG_IN_COMBAT=0x00080000u;
constexpr std::uint32_t UNIT_DYNFLAG_DEAD=0x20u;

struct Snapshot {
    std::uint8_t objectKnown;
    std::uint8_t descriptorKnown;
    std::uint8_t dead;
    std::uint8_t combat;
    std::uint8_t powerType;
    std::uint8_t reserved[3];
    std::uint32_t health;
    std::uint32_t maxHealth;
    std::uint32_t power[5];
    std::uint32_t maxPower[5];
    std::uint32_t unitFlags;
    std::uint32_t dynamicFlags;
};
static_assert(sizeof(Snapshot)==0x40,"API36 snapshot layout must remain 0x40 bytes");

struct Entry {
    std::uint8_t used;
    std::uint8_t snapshotKnown;
    std::uint8_t reserved[6];
    unsigned long long guid;
    Snapshot snapshot;
    std::uint32_t snapshotGeneration;
    std::uint32_t capturedAtMs;
};
static_assert(sizeof(Entry)==0x58,"API36 tracked entry layout must remain 0x58 bytes");

static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_updatePackets=0,g_compressedUpdatePackets=0,g_dirtySignals=0,g_coalescedSignals=0;
static volatile LONG g_dirtyPending=0;
static volatile LONG g_snapshotCalls=0,g_trackCalls=0,g_untrackCalls=0,g_capacityFailures=0,g_descriptorReconciles=0,g_descriptorFailures=0,g_descriptorClears=0,g_descriptorEmptyPreserves=0,g_descriptorUnbinds=0;
static volatile LONG g_healthEvents=0,g_powerEvents=0,g_combatEvents=0,g_objectUnavailable=0,g_recordsChecked=0,g_reconcilePasses=0;
static volatile LONG g_snapshotSuccess=0,g_snapshotKnown=0,g_snapshotUnknown=0,g_snapshotUnavailable=0,g_snapshotInstanceClears=0;
static std::uint32_t g_snapshotGeneration=0;
static std::uint32_t g_worldGeneration=0;
static unsigned long long g_lastChangedGuid=0;
static unsigned long g_lastChangedMask=0;
static char g_status[96]="LAZY_NOT_SUBSCRIBED";

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}

static bool readableProtection(DWORD protection){
    const DWORD p=protection&0xffu;
    return p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||
           p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool canRead(std::uintptr_t address,std::size_t bytes){
    if(!address||!bytes)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((const void*)address,&m,sizeof(m))!=sizeof(m))return false;
    if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!readableProtection(m.Protect))return false;
    const std::uintptr_t begin=(std::uintptr_t)m.BaseAddress;
    const std::uintptr_t end=begin+m.RegionSize;
    return address>=begin&&address<=end&&bytes<=end-address;
}

static bool executable(std::uintptr_t a){
    MEMORY_BASIC_INFORMATION m={};
    if(!a||VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&0xffu;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

constexpr std::uintptr_t SIGNAL_EVENT_PARAM=0x00703F50u;
using SignalEventParamFn=int (__cdecl *)(int eventCode,char* format,...);

static __forceinline bool emitHealthOriginalShape(unsigned long long guid,std::uint32_t oldHealth,std::uint32_t newHealth,std::uint32_t maxHealth,bool dead){
    if(!TysCustomEvents::ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    const int slot=TysCustomEvents::unitHealthSlot();if(slot<0)return false;
    static const char hex[]="0123456789ABCDEF";char g[19]={};g[0]='0';g[1]='x';
    for(unsigned i=0;i<16;++i)g[2+i]=hex[(unsigned)((guid>>((15u-i)*4u))&0xFu)];
    static char fmt[]="%s%d%d%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(slot,fmt,g,oldHealth,newHealth,maxHealth,dead?1u:0u);
    return true;
}
static __forceinline bool emitPowerOriginalShape(unsigned long long guid,std::uint32_t powerType,std::uint32_t oldPower,std::uint32_t newPower,std::uint32_t maxPower,std::uint32_t mask){
    if(!TysCustomEvents::ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    const int slot=TysCustomEvents::unitPowerSlot();if(slot<0)return false;
    static const char hex[]="0123456789ABCDEF";char g[19]={};g[0]='0';g[1]='x';
    for(unsigned i=0;i<16;++i)g[2+i]=hex[(unsigned)((guid>>((15u-i)*4u))&0xFu)];
    static char fmt[]="%s%d%d%d%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(slot,fmt,g,powerType,oldPower,newPower,maxPower,mask);
    return true;
}
static __forceinline bool emitCombatOriginalShape(unsigned long long guid,bool oldCombat,bool newCombat){
    if(!TysCustomEvents::ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    const int slot=TysCustomEvents::unitCombatSlot();if(slot<0)return false;
    static const char hex[]="0123456789ABCDEF";char g[19]={};g[0]='0';g[1]='x';
    for(unsigned i=0;i<16;++i)g[2+i]=hex[(unsigned)((guid>>((15u-i)*4u))&0xFu)];
    static char fmt[]="%s%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(slot,fmt,g,oldCombat?1u:0u,newCombat?1u:0u);
    return true;
}

static bool hexNibble(char c,unsigned*o){if(c>='0'&&c<='9'){*o=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*o=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*o=(unsigned)(c-'A'+10);return true;}return false;}

static char lowerAscii(char c){return c>='A'&&c<='Z'?(char)(c+('a'-'A')):c;}
static bool equalCi(const char*a,const char*b){
    if(!a||!b)return false;
    while(*a&&*b){if(lowerAscii(*a++)!=lowerAscii(*b++))return false;}
    return *a==0&&*b==0;
}
static bool prefixCi(const char*s,const char*p){
    if(!s||!p)return false;
    while(*p){if(!*s||lowerAscii(*s++)!=lowerAscii(*p++))return false;}
    return true;
}
static bool isUnitSelector(const char*s){
    if(!s)return false;
    if(equalCi(s,"player")||equalCi(s,"target")||equalCi(s,"mouseover")||equalCi(s,"pet"))return true;
    if(prefixCi(s,"party")){
        const char*d=s+5;
        return d[0]>='1'&&d[0]<='4'&&d[1]==0;
    }
    if(prefixCi(s,"raid")){
        const char*d=s+4;if(*d<'0'||*d>'9')return false;
        unsigned n=0;while(*d>='0'&&*d<='9'){n=n*10u+(unsigned)(*d-'0');++d;}
        return *d==0&&n>=1u&&n<=40u;
    }
    return false;
}
static bool parseGuidTextExact(const char*s,unsigned long long*out){
    if(!s||!out)return false;*out=0;
    while(*s==' '||*s=='\t')++s;
    if(s[0]=='0'&&lowerAscii(s[1])=='x')s+=2;
    unsigned long long v=0;unsigned digits=0;
    while(*s&&*s!=' '&&*s!='\t'){
        unsigned n=0;
        if(*s>='0'&&*s<='9')n=(unsigned)(*s-'0');
        else if(*s>='a'&&*s<='f')n=(unsigned)(*s-'a'+10);
        else if(*s>='A'&&*s<='F')n=(unsigned)(*s-'A'+10);
        else return false;
        if(digits>=16)return false;
        v=(v<<4)|n;++digits;++s;
    }
    while(*s==' '||*s=='\t')++s;
    if(*s||digits==0||v==0)return false;
    *out=v;return true;
}
static bool resolveSelector(const char*s,unsigned long long*out,const char**error){
    if(out)*out=0;if(error)*error="BAD_SELECTOR";
    if(!s||!out)return false;
    if(isUnitSelector(s)){
        if(!executable(UNIT_TOKEN_RESOLVER)){if(error)*error="RESOLVE_UNIT_UNAVAILABLE";return false;}
        using ResolveUnitFn=std::uint32_t(__fastcall*)(const char*);
        const std::uint32_t object=((ResolveUnitFn)UNIT_TOKEN_RESOLVER)(s);
        if(!object||(object&1u)||!canRead((std::uintptr_t)object+OFF_OBJECT_GUID,sizeof(unsigned long long))){
            if(error)*error="UNIT_NOT_FOUND";return false;
        }
        const unsigned long long g=*(const unsigned long long*)((std::uintptr_t)object+OFF_OBJECT_GUID);
        if(!g){if(error)*error="UNIT_NOT_FOUND";return false;}
        *out=g;if(error)*error="OK";return true;
    }
    if(!parseGuidTextExact(s,out)){if(error)*error="GUID_INVALID";return false;}
    if(error)*error="OK";return true;
}
static bool resolveGuid(Lua50::State L,int idx,unsigned long long*out,const char**error){
    if(!out)return false;*out=0;
    if(!Lua50::IsString(L,idx)){if(error)*error="BAD_SELECTOR";return false;}
    const char*s=Lua50::ToString(L,idx);
    return resolveSelector(s,out,error);
}

static void formatGuid(char*b,std::size_t n,unsigned long long guid){
    if(!b||n<2)return;
    static const char h[]="0123456789ABCDEF";
    if(n<19){b[0]=0;return;}b[0]='0';b[1]='x';
    for(unsigned i=0;i<16;++i)b[2+i]=h[(unsigned)((guid>>((15-i)*4))&15ULL)];
    b[18]=0;
}
static void pushGuid(Lua50::State L,const char*k,unsigned long long guid){char b[24]={};formatGuid(b,sizeof(b),guid);setStr(L,k,b);}

static __forceinline Entry* find(unsigned long long guid){
    for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].guid==guid)return &g_entries[i];
    return 0;
}
static __forceinline Entry* track(unsigned long long guid,bool*isNew){
    if(isNew)*isNew=false;Entry*e=find(guid);if(e)return e;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        if(g_entries[i].used)continue;
        g_entries[i]=Entry{};g_entries[i].used=1;g_entries[i].guid=guid;
        if(isNew)*isNew=true;return &g_entries[i];
    }
    return 0;
}

static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*){
    if(op!=WoW112::SMSG_UPDATE_OBJECT_OPCODE&&op!=WoW112::SMSG_COMPRESSED_UPDATE_OBJECT_OPCODE)return;
    if(op==WoW112::SMSG_UPDATE_OBJECT_OPCODE)++g_updatePackets;
    else ++g_compressedUpdatePackets;
    ++g_dirtySignals;
    if(InterlockedExchange(&g_dirtyPending,1)!=0)++g_coalescedSignals;
}

static bool resolveSnapshot(unsigned long long guid,Snapshot*out){
    if(!guid||!out)return false;*out=Snapshot{};
    if(!executable(FAST_GUID_LOOKUP))return false;
    using GetObjectFn=std::uint32_t(__fastcall*)(unsigned long long);
    const std::uint32_t object=((GetObjectFn)FAST_GUID_LOOKUP)(guid);

    // Object lookup failure is an observable state, not a descriptor failure.
    if(!object||(object&1u)||!canRead(object,0x38u)){out->objectKnown=0;return true;}
    const unsigned long long liveGuid=*(const unsigned long long*)((std::uintptr_t)object+OFF_OBJECT_GUID);
    const std::uint32_t type=*(const std::uint32_t*)((std::uintptr_t)object+OFF_OBJECT_TYPE);
    if(liveGuid!=guid||(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER)){out->objectKnown=0;return true;}

    out->objectKnown=1;
    const std::uint32_t descriptor=*(const std::uint32_t*)((std::uintptr_t)object+OFF_OBJECT_DESCRIPTOR);
    if(!descriptor)return false;
    // Original R2 validates one contiguous descriptor range: [descriptor+0x58, descriptor+0x240).
    if(!canRead((std::uintptr_t)descriptor+OFF_HEALTH,0x1E8u))return false;

    out->descriptorKnown=1;
    out->health=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_HEALTH);
    out->maxHealth=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_MAXHEALTH);
    for(unsigned i=0;i<5;++i){
        out->power[i]=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_POWER1+i*4u);
        out->maxPower[i]=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_MAXPOWER1+i*4u);
    }
    out->powerType=*(const std::uint8_t*)((std::uintptr_t)descriptor+OFF_POWER_TYPE_BYTE);
    out->unitFlags=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_UNIT_FLAGS);
    out->dynamicFlags=*(const std::uint32_t*)((std::uintptr_t)descriptor+OFF_DYNAMIC_FLAGS);
    out->dead=out->health==0?1u:((out->dynamicFlags&UNIT_DYNFLAG_DEAD)?1u:0u);
    out->combat=(out->unitFlags&UNIT_FLAG_IN_COMBAT)?1u:0u;
    return true;
}

static std::uint32_t activePower(const Snapshot&s){
    return s.powerType<=MAX_POWER_TYPE?s.power[s.powerType]:0u;
}
static std::uint32_t activeMaxPower(const Snapshot&s){
    return s.powerType<=MAX_POWER_TYPE?s.maxPower[s.powerType]:0u;
}

static bool reconcileEntry(Entry&e){
    ++g_recordsChecked;
    Snapshot next={};
    if(!resolveSnapshot(e.guid,&next)){
        ++g_descriptorFailures;
        if(e.snapshotKnown)++g_descriptorEmptyPreserves;
        return false;
    }
    if(!next.objectKnown){
        ++g_objectUnavailable;
        if(e.snapshotKnown&&e.snapshot.objectKnown)++g_descriptorUnbinds;
        e.snapshot.objectKnown=0;
        return false;
    }

    const bool had=e.snapshotKnown!=0;
    const Snapshot old=e.snapshot;
    e.snapshot=next;
    e.snapshotKnown=1;
    ++g_descriptorReconciles;

    if(!had){
        e.capturedAtMs=GetTickCount();
        ++g_snapshotKnown;
        return true;
    }

    unsigned long changedMask=0;
    const bool healthChanged=old.health!=next.health||old.maxHealth!=next.maxHealth||old.dead!=next.dead;
    if(healthChanged){
        if(emitHealthOriginalShape(e.guid,old.health,next.health,next.maxHealth,next.dead!=0))++g_healthEvents;
        changedMask|=0x01u;
    }

    unsigned long powerMask=0;
    const std::uint32_t oldPower=activePower(old),newPower=activePower(next);
    const std::uint32_t oldMax=activeMaxPower(old),newMax=activeMaxPower(next);
    if(old.powerType!=next.powerType)powerMask|=0x01u;
    if(oldPower!=newPower)powerMask|=0x02u;
    if(oldMax!=newMax)powerMask|=0x04u;
    if(powerMask){
        if(emitPowerOriginalShape(e.guid,next.powerType,oldPower,newPower,newMax,powerMask))++g_powerEvents;
        changedMask|=0x02u;
    }

    if(old.combat!=next.combat){
        if(emitCombatOriginalShape(e.guid,old.combat!=0,next.combat!=0))++g_combatEvents;
        changedMask|=0x04u;
    }

    if(changedMask){
        e.snapshotGeneration=++g_snapshotGeneration;
        e.capturedAtMs=GetTickCount();
        g_lastChangedGuid=e.guid;g_lastChangedMask=changedMask;
    }
    return true;
}

static void onTick(){
    if(InterlockedExchange(&g_dirtyPending,0)==0)return;
    ++g_reconcilePasses;
    for(unsigned i=0;i<MAX_TRACKED;++i){
        Entry&e=g_entries[i];if(e.used)reconcileEntry(e);
    }
    ++g_worldGeneration;if(g_worldGeneration==0)++g_worldGeneration;
}

static int pushEntry(Lua50::State L,const Entry&e){
    Lua50::NewTable(L);
    pushGuid(L,"guid",e.guid);
    const bool visible=e.snapshotKnown&&e.snapshot.objectKnown;
    const bool fieldsValid=visible&&e.snapshot.descriptorKnown;
    setBool(L,"visible",visible);
    setBool(L,"fieldsValid",fieldsValid);
    setBool(L,"tracked",e.used!=0);
    setBool(L,"initialized",e.snapshotKnown!=0);

    const char* state="GUID_NOT_VISIBLE";
    if(visible){
        state="UNIT_FIELDS_UNAVAILABLE";
        if(fieldsValid){
            const Snapshot&s=e.snapshot;
            setNum(L,"health",s.health);
            setNum(L,"maxHealth",s.maxHealth);
            setBool(L,"dead",s.dead!=0);
            setBool(L,"combat",s.combat!=0);
            setNum(L,"unitFlags",s.unitFlags);
            setNum(L,"dynamicFlags",s.dynamicFlags);
            setNum(L,"powerType",s.powerType);
            if(s.powerType<=MAX_POWER_TYPE){
                setNum(L,"power",activePower(s));
                setNum(L,"maxPower",activeMaxPower(s));
            }else{
                Lua50::PushString(L,"power");Lua50::PushNil(L);Lua50::SetTable(L,-3);
                Lua50::PushString(L,"maxPower");Lua50::PushNil(L);Lua50::SetTable(L,-3);
            }

            char keyPower[7]={'p','o','w','e','r','1',0};
            char keyMax[10]={'m','a','x','P','o','w','e','r','1',0};
            for(unsigned i=0;i<5;++i){
                keyPower[5]=(char)('1'+i);
                keyMax[8]=(char)('1'+i);
                setNum(L,keyPower,s.power[i]);
                setNum(L,keyMax,s.maxPower[i]);
            }
            state="OK";
        }
    }
    setStr(L,"status",state);
    return 1;
}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;
    bool a=TysNativeBus::subscribeIncoming(&onIncoming);bool b=TysNativeBus::subscribeWorldTick(&onTick);bool c=TysCustomEvents::ensureUnitStateEvents();
    InterlockedExchange(&g_inSub,a?1:0);InterlockedExchange(&g_tickSub,b?1:0);
    (void)c;
    const char*s=(a&&b)?"READY_UNITSTATE_US1R2":"LAZY_NOT_SUBSCRIBED";
    unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;return a&&b;
}
const char* status(){return g_status;}

int dispatchStatus(Lua50::State L){
    initialize();Lua50::NewTable(L);
    setStr(L,"status",g_status);
    setStr(L,"architecture","LAZY_TRACKED_GUIDS_UPDATEOBJECT_DIRTY_RECONCILE");
    setStr(L,"authority","CLIENT_UNIT_DESCRIPTOR_AFTER_STOCK_HANDLER");
    setNum(L,"capacity",MAX_TRACKED);
    unsigned tracked=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used)++tracked;setNum(L,"tracked",tracked);
    setBool(L,"incomingSubscribed",g_inSub!=0);
    setBool(L,"worldTickSubscribed",g_tickSub!=0);
    setBool(L,"customEventsReady",TysCustomEvents::ensureUnitStateEvents());
    setBool(L,"dynamicEventSlots",true);
    setNum(L,"eventHealthSlot",TysCustomEvents::unitHealthSlot());
    setNum(L,"eventPowerSlot",TysCustomEvents::unitPowerSlot());
    setNum(L,"eventCombatSlot",TysCustomEvents::unitCombatSlot());
    setNum(L,"updateObjectOpcode",WoW112::SMSG_UPDATE_OBJECT_OPCODE);
    setNum(L,"compressedUpdateObjectOpcode",WoW112::SMSG_COMPRESSED_UPDATE_OBJECT_OPCODE);
    setNum(L,"fastGuidLookupAddress",FAST_GUID_LOOKUP);
    setNum(L,"updatePackets",g_updatePackets);
    setNum(L,"compressedUpdatePackets",g_compressedUpdatePackets);
    setNum(L,"dirtySignals",g_dirtySignals);
    setNum(L,"coalescedSignals",g_coalescedSignals);
    setNum(L,"reconcilePasses",g_reconcilePasses);
    setNum(L,"recordsChecked",g_recordsChecked);
    setNum(L,"objectUnavailable",g_objectUnavailable);
    setNum(L,"descriptorFailures",g_descriptorFailures);
    setNum(L,"healthEvents",g_healthEvents);
    setNum(L,"powerEvents",g_powerEvents);
    setNum(L,"combatEvents",g_combatEvents);
    setNum(L,"trackCalls",g_trackCalls);
    setNum(L,"untrackCalls",g_untrackCalls);
    setNum(L,"capacityFailures",g_capacityFailures);
    if(g_lastChangedGuid)pushGuid(L,"lastChangedGuid",g_lastChangedGuid);else{Lua50::PushString(L,"lastChangedGuid");Lua50::PushNil(L);Lua50::SetTable(L,-3);}
    setNum(L,"lastChangedMask",g_lastChangedMask);
    setNum(L,"worldGeneration",g_worldGeneration);
    setBool(L,"reconcilePending",g_dirtyPending!=0);
    setBool(L,"directHook",false);
    setBool(L,"objectManagerPolling",false);
    setBool(L,"packetBodyParsing",false);
    setBool(L,"zlibDecompression",false);
    setBool(L,"backgroundThread",false);
    setBool(L,"timer",false);
    setStr(L,"idleTickPath","TRACKED_ZERO_OR_DIRTY_ZERO_READ_ONLY_RETURN");
    setStr(L,"powerSemantics","ACTIVE_POWER_ONLY_TYPE_VALUE_MAX");
    setStr(L,"powerMaskSemantics","BIT0_TYPE_BIT1_VALUE_BIT2_MAX");
    setStr(L,"descriptorReadPolicy","ONE_OBJECT_RANGE_PLUS_ONE_DESCRIPTOR_RANGE_PER_SNAPSHOT");
    setNum(L,"combatFlag",UNIT_FLAG_IN_COMBAT);
    setStr(L,"compressedPolicy","COMPRESSED_UPDATE_IS_DIRTY_SIGNAL_NO_DECOMPRESSION");
    return 1;
}

int dispatchTrack(Lua50::State L){
    ++g_trackCalls;
    if(!initialize()){Lua50::PushNil(L);Lua50::PushString(L,g_status);return 2;}
    unsigned long long g=0;const char*err="BAD_SELECTOR";if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g,&err)){Lua50::PushNil(L);Lua50::PushString(L,err);return 2;}
    bool n=false;Entry*e=track(g,&n);if(!e){++g_capacityFailures;Lua50::PushNil(L);Lua50::PushString(L,"TRACK_CAPACITY");return 2;}
    Snapshot snap={};
    if(resolveSnapshot(g,&snap)){
        e->snapshot=snap;
        e->snapshotKnown=(snap.objectKnown!=0&&snap.descriptorKnown!=0)?1u:0u;
        e->capturedAtMs=GetTickCount();
    }else{
        ++g_descriptorFailures;
    }
    Lua50::PushBool(L,true);Lua50::PushString(L,n?"TRACKED_NEW":"TRACKED_EXISTING");char b[24]={};formatGuid(b,sizeof(b),g);Lua50::PushString(L,b);return 3;
}
int dispatchUntrack(Lua50::State L){
    initialize();unsigned long long g=0;const char*err="BAD_SELECTOR";if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g,&err)){Lua50::PushBool(L,false);Lua50::PushString(L,err);return 2;}
    ++g_untrackCalls;Entry*e=find(g);if(!e){Lua50::PushBool(L,false);Lua50::PushString(L,"NOT_TRACKED");return 2;}*e=Entry{};Lua50::PushBool(L,true);Lua50::PushString(L,"UNTRACKED");return 2;
}
int dispatchGet(Lua50::State L){
    initialize();unsigned long long g=0;const char*err="BAD_SELECTOR";if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g,&err)){Lua50::PushNil(L);Lua50::PushString(L,err);return 2;}++g_snapshotCalls;
    Entry*e=find(g);if(!e){++g_snapshotUnknown;Lua50::PushNil(L);Lua50::PushString(L,"NOT_TRACKED");return 2;}const bool ok=reconcileEntry(*e);if(ok)++g_snapshotSuccess;else ++g_snapshotUnavailable;if(e->snapshotKnown)++g_snapshotKnown;return pushEntry(L,*e);
}
int dispatchList(Lua50::State L){initialize();Lua50::NewTable(L);int idx=1;for(unsigned i=0;i<MAX_TRACKED;++i){if(!g_entries[i].used)continue;Lua50::PushNumber(L,idx++);pushEntry(L,g_entries[i]);Lua50::SetTable(L,-3);}return 1;}
int dispatchClear(Lua50::State L){initialize();for(unsigned i=0;i<MAX_TRACKED;++i)g_entries[i]=Entry{};InterlockedExchange(&g_dirtyPending,0);Lua50::PushBool(L,true);Lua50::PushString(L,"CLEARED");return 2;}

} // namespace TysUnitStateCore
