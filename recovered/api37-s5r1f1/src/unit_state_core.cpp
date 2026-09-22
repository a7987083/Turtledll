#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "unit_state_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysUnitStateCore {
namespace {

constexpr unsigned MAX_TRACKED=128;
constexpr std::uintptr_t FAST_GUID_LOOKUP=0x00464870u;
constexpr std::uintptr_t UNIT_GUID_FN=0x00515970u;
constexpr std::uint32_t OBJECT_TYPE_UNIT=3u;
constexpr std::uint32_t OBJECT_TYPE_PLAYER=4u;
constexpr std::uint32_t UNIT_FLAG_IN_COMBAT=0x00080000u;

struct Entry {
    unsigned long long guid;
    unsigned long health;
    unsigned long maxHealth;
    unsigned long power;
    unsigned long maxPower;
    unsigned long powerType;
    unsigned long combatFlag;
    unsigned long capturedAtMs;
    bool used;
    bool valid;
    bool dirty;
    bool powerValid;
};

static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_updatePackets=0,g_compressedUpdatePackets=0,g_dirtySignals=0,g_coalescedSignals=0;
static volatile LONG g_snapshotCalls=0,g_untrackCalls=0,g_descriptorReconciles=0,g_descriptorFailures=0,g_descriptorClears=0,g_descriptorEmptyPreserves=0,g_descriptorUnbinds=0;
static volatile LONG g_healthEvents=0,g_powerEvents=0,g_combatEvents=0,g_objectUnavailable=0,g_recordsChecked=0,g_reconcilePasses=0;
static char g_status[96]="NOT_INITIALIZED";

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static void setNil(Lua50::State L,const char*k){Lua50::PushString(L,k);Lua50::PushNil(L);Lua50::SetTable(L,-3);}

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

template<class T> static bool safeRead(std::uintptr_t a,T*out){if(!out||!canRead(a,sizeof(T)))return false;*out=*(const T*)a;return true;}

static bool executable(std::uintptr_t a){
    MEMORY_BASIC_INFORMATION m={};
    if(!a||VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&0xffu;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool hexNibble(char c,unsigned*o){if(c>='0'&&c<='9'){*o=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*o=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*o=(unsigned)(c-'A'+10);return true;}return false;}

static bool parseGuidText(const char*s,unsigned long long*out){
    if(!s||!out)return false;
    *out=0;
    while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')++s;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
    unsigned long long v=0;unsigned digits=0;
    for(;*s&&digits<16;++s,++digits){unsigned n=0;if(!hexNibble(*s,&n))return false;v=(v<<4)|n;}
    if(*s||digits==0||v==0)return false;
    *out=v;return true;
}

static bool resolveGuid(Lua50::State L,int idx,unsigned long long*out){
    if(!out)return false;
    *out=0;
    if(Lua50::IsNumber(L,idx)){
        const double n=Lua50::ToNumber(L,idx);
        if(n<=0)return false;
        *out=(unsigned long long)n;
        return *out!=0;
    }
    if(!Lua50::IsString(L,idx))return false;
    const char*s=Lua50::ToString(L,idx);
    if(!s||!*s)return false;
    if(executable(UNIT_GUID_FN)){
        using UnitGuidFn=unsigned long long(__fastcall*)(const char*);
        const unsigned long long g=((UnitGuidFn)UNIT_GUID_FN)(s);
        if(g){*out=g;return true;}
    }
    return parseGuidText(s,out);
}

static void pushGuid(Lua50::State L,const char*k,unsigned long long guid){
    char b[24]={};
#if defined(_MSC_VER)
    std::sprintf(b,"0x%016I64X",guid);
#else
    std::sprintf(b,"0x%016llX",guid);
#endif
    setStr(L,k,b);
}

static Entry* find(unsigned long long guid){for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].guid==guid)return &g_entries[i];return 0;}

static Entry* track(unsigned long long guid,bool*isNew){
    if(isNew)*isNew=false;
    Entry*e=find(guid);if(e)return e;
    for(unsigned i=0;i<MAX_TRACKED;++i)if(!g_entries[i].used){
        g_entries[i]=Entry{};g_entries[i].used=true;g_entries[i].dirty=true;g_entries[i].guid=guid;
        if(isNew)*isNew=true;return &g_entries[i];
    }
    return 0;
}

static void markDirtyAll(){
    for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used){
        if(g_entries[i].dirty)++g_coalescedSignals;
        else {g_entries[i].dirty=true;++g_dirtySignals;}
    }
}

static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*){
    if(op==WoW112::SMSG_UPDATE_OBJECT_OPCODE){++g_updatePackets;markDirtyAll();}
    else if(op==WoW112::SMSG_COMPRESSED_UPDATE_OBJECT_OPCODE){++g_compressedUpdatePackets;markDirtyAll();}
}

static bool resolveObject(unsigned long long guid,std::uint32_t*out){
    if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;
    using GetObjectFn=std::uint32_t(__fastcall*)(unsigned long long);
    const std::uint32_t object=((GetObjectFn)FAST_GUID_LOOKUP)(guid);
    if(!object||(object&1u)||!canRead(object,0x11cu))return false;
    std::uint32_t type=0;
    if(!safeRead((std::uintptr_t)object+0x14u,&type)||(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER))return false;
    unsigned long long liveGuid=0;
    if(!safeRead((std::uintptr_t)object+0x30u,&liveGuid)||liveGuid!=guid)return false;
    *out=object;return true;
}

// Recovered historical UnitXP descriptor layout:
// object + 0x110 -> UnitFields base
// health +0x40, maxHealth +0x58, UNIT_FIELD_FLAGS-like combat word +0xA0.
// Active-power offsets are deliberately left unrecovered here; we preserve the
// previous cached power values rather than inventing descriptor indices.
static bool reconcileEntry(Entry&e){
    ++g_recordsChecked;
    std::uint32_t object=0;
    if(!resolveObject(e.guid,&object)){
        ++g_objectUnavailable;
        e.valid=false;
        e.dirty=false;
        return false;
    }

    std::uint32_t attr=0;
    if(!safeRead((std::uintptr_t)object+0x110u,&attr)){
        ++g_descriptorFailures;e.dirty=false;return false;
    }
    if(!attr){
        // US1-R2 contract: empty descriptor preserves the previous cache.
        ++g_descriptorEmptyPreserves;e.dirty=false;return true;
    }
    if((attr&3u)!=0u||!canRead(attr,0xA4u)){
        ++g_descriptorFailures;e.dirty=false;return false;
    }

    std::uint32_t health=0,maxHealth=0,flags=0;
    if(!safeRead((std::uintptr_t)attr+0x40u,&health)||
       !safeRead((std::uintptr_t)attr+0x58u,&maxHealth)||
       !safeRead((std::uintptr_t)attr+0xA0u,&flags)){
        ++g_descriptorFailures;e.dirty=false;return false;
    }

    const bool had=e.valid;
    const unsigned long oldHealth=e.health;
    const unsigned long oldMax=e.maxHealth;
    const unsigned long oldCombat=e.combatFlag;

    e.health=health;
    e.maxHealth=maxHealth;
    e.combatFlag=(flags&UNIT_FLAG_IN_COMBAT)?1u:0u;
    e.capturedAtMs=GetTickCount();
    e.valid=true;
    e.dirty=false;
    ++g_descriptorReconciles;

    // Counters are recovered now; actual custom-event dispatch remains pending
    // until the historical event bridge/post-handler call site is recovered.
    if(had&&(oldHealth!=e.health||oldMax!=e.maxHealth))++g_healthEvents;
    if(had&&oldCombat!=e.combatFlag)++g_combatEvents;
    return true;
}

static void onTick(){
    bool any=false;
    for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].dirty){any=true;break;}
    if(!any)return;
    ++g_reconcilePasses;
    for(unsigned i=0;i<MAX_TRACKED;++i){Entry&e=g_entries[i];if(e.used&&e.dirty)reconcileEntry(e);}
}

static int pushEntry(Lua50::State L,const Entry&e){
    Lua50::NewTable(L);
    pushGuid(L,"guid",e.guid);
    setNum(L,"guidLow",(unsigned long)e.guid);
    setNum(L,"guidHigh",(unsigned long)(e.guid>>32));
    setBool(L,"tracked",e.used);
    setBool(L,"visible",e.valid);
    setBool(L,"valid",e.valid);
    setBool(L,"dirty",e.dirty);
    setNum(L,"health",e.health);
    setNum(L,"maxHealth",e.maxHealth);
    setBool(L,"dead",e.valid&&e.health==0);
    setBool(L,"combat",e.combatFlag!=0);
    setNum(L,"combatFlag",e.combatFlag);
    if(e.powerValid){setNum(L,"powerType",e.powerType);setNum(L,"power",e.power);setNum(L,"maxPower",e.maxPower);}
    else {setNil(L,"powerType");setNil(L,"power");setNil(L,"maxPower");}
    setNum(L,"capturedAtMs",e.capturedAtMs);
    return 1;
}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;
    bool a=TysNativeBus::subscribeIncoming(&onIncoming);
    bool b=TysNativeBus::subscribeWorldTick(&onTick);
    InterlockedExchange(&g_inSub,a?1:0);
    InterlockedExchange(&g_tickSub,b?1:0);
    const char*s=(a&&b)?"READY_TRACKED_UPDATEOBJECT_GATE_DESCRIPTOR_READ_PARTIAL":"PARTIAL_TRACKED_UPDATEOBJECT_GATE";
    unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;
    return a&&b;
}

const char* status(){return g_status;}

int dispatchStatus(Lua50::State L){
    initialize();
    Lua50::NewTable(L);
    setStr(L,"stage","US1-R2");
    setStr(L,"status",g_status);
    setBool(L,"incomingSubscribed",g_inSub!=0);
    setBool(L,"worldTickSubscribed",g_tickSub!=0);
    setBool(L,"customEventsReady",false);
    setNum(L,"capacity",MAX_TRACKED);
    unsigned tracked=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used)++tracked;
    setNum(L,"tracked",tracked);
    setNum(L,"fastGuidLookupAddress",FAST_GUID_LOOKUP);
    setNum(L,"updatePackets",g_updatePackets);
    setNum(L,"compressedUpdatePackets",g_compressedUpdatePackets);
    setNum(L,"dirtySignals",g_dirtySignals);
    setNum(L,"coalescedSignals",g_coalescedSignals);
    setNum(L,"reconcilePasses",g_reconcilePasses);
    setNum(L,"recordsChecked",g_recordsChecked);
    setNum(L,"objectUnavailable",g_objectUnavailable);
    setNum(L,"snapshotCalls",g_snapshotCalls);
    setNum(L,"untrackCalls",g_untrackCalls);
    setNum(L,"descriptorReconciles",g_descriptorReconciles);
    setNum(L,"descriptorFailures",g_descriptorFailures);
    setNum(L,"descriptorClears",g_descriptorClears);
    setNum(L,"descriptorEmptyPreserves",g_descriptorEmptyPreserves);
    setNum(L,"descriptorUnbinds",g_descriptorUnbinds);
    setNum(L,"healthEvents",g_healthEvents);
    setNum(L,"powerEvents",g_powerEvents);
    setNum(L,"combatEvents",g_combatEvents);
    setStr(L,"powerSemantics","ACTIVE_POWER_ONLY_TYPE_VALUE_MAX_PENDING_DESCRIPTOR_OFFSETS");
    setStr(L,"powerMaskSemantics","BIT0_TYPE_BIT1_VALUE_BIT2_MAX");
    setStr(L,"descriptorReadPolicy","ONE_OBJECT_RANGE_PLUS_ONE_DESCRIPTOR_RANGE_PER_SNAPSHOT");
    setStr(L,"descriptorPolicy","UNITFIELDS_PRESENCE_AUTHORITY; EMPTY_DESCRIPTOR_PRESERVES_CACHE");
    setBool(L,"packetBodyParsing",false);
    setBool(L,"zlibDecompression",false);
    setBool(L,"compressedDecompression",false);
    setBool(L,"objectManagerPolling",false);
    setBool(L,"directHook",false);
    setBool(L,"timer",false);
    setBool(L,"backgroundThread",false);
    setBool(L,"idleTickPath",true);
    return 1;
}

int dispatchTrack(Lua50::State L){
    initialize();
    unsigned long long g=0;
    if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    bool n=false;Entry*e=track(g,&n);
    if(!e){Lua50::PushNil(L);Lua50::PushString(L,"TRACK_CAPACITY");return 2;}
    // Initial snapshot is demand-driven and uses the same reconcile path.
    reconcileEntry(*e);
    Lua50::PushBool(L,true);
    Lua50::PushString(L,n?"TRACKED_NEW":"TRACKED_EXISTING");
    char b[24]={};
#if defined(_MSC_VER)
    std::sprintf(b,"0x%016I64X",g);
#else
    std::sprintf(b,"0x%016llX",g);
#endif
    Lua50::PushString(L,b);
    return 3;
}

int dispatchUntrack(Lua50::State L){
    initialize();
    unsigned long long g=0;
    if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushBool(L,false);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    ++g_untrackCalls;Entry*e=find(g);
    if(!e){Lua50::PushBool(L,false);Lua50::PushString(L,"NOT_TRACKED");return 2;}
    *e=Entry{};
    Lua50::PushBool(L,true);Lua50::PushString(L,"UNTRACKED");
    char b[24]={};
#if defined(_MSC_VER)
    std::sprintf(b,"0x%016I64X",g);
#else
    std::sprintf(b,"0x%016llX",g);
#endif
    Lua50::PushString(L,b);
    return 3;
}

int dispatchGet(Lua50::State L){
    initialize();
    unsigned long long g=0;
    if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    ++g_snapshotCalls;
    Entry*e=find(g);
    if(!e){Lua50::PushNil(L);Lua50::PushString(L,"NOT_TRACKED");return 2;}
    if(e->dirty)reconcileEntry(*e);
    return pushEntry(L,*e);
}

int dispatchList(Lua50::State L){
    initialize();
    Lua50::NewTable(L);int idx=1;
    for(unsigned i=0;i<MAX_TRACKED;++i){if(!g_entries[i].used)continue;Lua50::PushNumber(L,idx++);pushEntry(L,g_entries[i]);Lua50::SetTable(L,-3);}
    return 1;
}

int dispatchClear(Lua50::State L){
    initialize();
    unsigned n=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used){g_entries[i]=Entry{};++n;}
    Lua50::PushNumber(L,n);return 1;
}

} // namespace TysUnitStateCore
