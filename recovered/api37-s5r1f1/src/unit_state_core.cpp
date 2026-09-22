#include <windows.h>
#include "unit_state_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysUnitStateCore {
namespace {
constexpr unsigned MAX_TRACKED=128;
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
};
static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_updatePackets=0,g_compressedUpdatePackets=0,g_dirtySignals=0,g_coalescedSignals=0;
static volatile LONG g_snapshotCalls=0,g_untrackCalls=0,g_descriptorReconciles=0,g_descriptorFailures=0,g_descriptorClears=0,g_descriptorEmptyPreserves=0,g_descriptorUnbinds=0;
static volatile LONG g_healthEvents=0,g_powerEvents=0,g_combatEvents=0;
static char g_status[96]="NOT_INITIALIZED";

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static bool hexNibble(char c,unsigned*o){if(c>='0'&&c<='9'){*o=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*o=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*o=(unsigned)(c-'A'+10);return true;}return false;}
static bool parseGuid(Lua50::State L,int idx,unsigned long long*out){if(!out)return false;*out=0;if(Lua50::IsNumber(L,idx)){double n=Lua50::ToNumber(L,idx);if(n<=0)return false;*out=(unsigned long long)n;return *out!=0;}if(!Lua50::IsString(L,idx))return false;const char*s=Lua50::ToString(L,idx);if(!s||!*s)return false;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;unsigned long long v=0;unsigned digits=0;for(;*s&&digits<16;++s,++digits){unsigned n=0;if(!hexNibble(*s,&n))return false;v=(v<<4)|n;}if(*s||digits==0||v==0)return false;*out=v;return true;}
static Entry* find(unsigned long long guid){for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used&&g_entries[i].guid==guid)return &g_entries[i];return 0;}
static Entry* track(unsigned long long guid,bool*isNew){if(isNew)*isNew=false;Entry*e=find(guid);if(e)return e;for(unsigned i=0;i<MAX_TRACKED;++i)if(!g_entries[i].used){g_entries[i]=Entry{};g_entries[i].used=true;g_entries[i].dirty=true;g_entries[i].guid=guid;if(isNew)*isNew=true;return &g_entries[i];}return 0;}
static void markDirtyAll(){for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used){if(g_entries[i].dirty)++g_coalescedSignals;else {g_entries[i].dirty=true;++g_dirtySignals;}}}
static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*){if(op==WoW112::SMSG_UPDATE_OBJECT_OPCODE){++g_updatePackets;markDirtyAll();}else if(op==WoW112::SMSG_COMPRESSED_UPDATE_OBJECT_OPCODE){++g_compressedUpdatePackets;markDirtyAll();}}
// Compile-stage recovery only. Exact post-handler descriptor reconcile remains
// under disassembly recovery; do not fabricate packet parsing or decompression.
static void onTick(){}
static int pushEntry(Lua50::State L,const Entry&e){Lua50::NewTable(L);setNum(L,"guidLow",(unsigned long)e.guid);setNum(L,"guidHigh",(unsigned long)(e.guid>>32));setBool(L,"tracked",e.used);setBool(L,"valid",e.valid);setBool(L,"dirty",e.dirty);setNum(L,"health",e.health);setNum(L,"maxHealth",e.maxHealth);setNum(L,"power",e.power);setNum(L,"maxPower",e.maxPower);setNum(L,"powerType",e.powerType);setNum(L,"combatFlag",e.combatFlag);setNum(L,"capturedAtMs",e.capturedAtMs);return 1;}
}

bool initialize(){if(InterlockedCompareExchange(&g_init,1,0)!=0)return true;bool a=TysNativeBus::subscribeIncoming(&onIncoming);bool b=TysNativeBus::subscribeWorldTick(&onTick);InterlockedExchange(&g_inSub,a?1:0);InterlockedExchange(&g_tickSub,b?1:0);const char*s=(a&&b)?"READY_TRACKED_UPDATEOBJECT_GATE":"PARTIAL_TRACKED_UPDATEOBJECT_GATE";unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;return a&&b;}
const char* status(){return g_status;}
int dispatchStatus(Lua50::State L){initialize();Lua50::NewTable(L);setStr(L,"stage","US1-R2");setStr(L,"status",g_status);setBool(L,"incomingSubscribed",g_inSub!=0);setBool(L,"worldTickSubscribed",g_tickSub!=0);setNum(L,"fastGuidLookupAddress",0x00464870UL);setNum(L,"updatePackets",g_updatePackets);setNum(L,"compressedUpdatePackets",g_compressedUpdatePackets);setNum(L,"dirtySignals",g_dirtySignals);setNum(L,"coalescedSignals",g_coalescedSignals);setNum(L,"snapshotCalls",g_snapshotCalls);setNum(L,"untrackCalls",g_untrackCalls);setNum(L,"descriptorReconciles",g_descriptorReconciles);setNum(L,"descriptorFailures",g_descriptorFailures);setNum(L,"descriptorClears",g_descriptorClears);setNum(L,"descriptorEmptyPreserves",g_descriptorEmptyPreserves);setNum(L,"descriptorUnbinds",g_descriptorUnbinds);setNum(L,"healthEvents",g_healthEvents);setNum(L,"powerEvents",g_powerEvents);setNum(L,"combatEvents",g_combatEvents);setStr(L,"powerSemantics","ACTIVE_POWER_ONLY_TYPE_VALUE_MAX");setStr(L,"powerMaskSemantics","BIT0_TYPE_BIT1_VALUE_BIT2_MAX");setStr(L,"descriptorReadPolicy","ONE_OBJECT_RANGE_PLUS_ONE_DESCRIPTOR_RANGE_PER_SNAPSHOT");setStr(L,"descriptorPolicy","UNITFIELDS_PRESENCE_AUTHORITY; EMPTY_DESCRIPTOR_PRESERVES_CACHE");setBool(L,"packetBodyParsing",false);setBool(L,"compressedDecompression",false);setBool(L,"objectManagerPolling",false);setBool(L,"backgroundThread",false);return 1;}
int dispatchTrack(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!parseGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}bool n=false;Entry*e=track(g,&n);if(!e){Lua50::PushNil(L);Lua50::PushString(L,"TRACK_CAPACITY");return 2;}Lua50::PushBool(L,true);Lua50::PushString(L,n?"TRACKED_NEW":"TRACKED_EXISTING");return 2;}
int dispatchUntrack(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!parseGuid(L,2,&g)){Lua50::PushBool(L,false);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}++g_untrackCalls;Entry*e=find(g);if(!e){Lua50::PushBool(L,false);Lua50::PushString(L,"NOT_TRACKED");return 2;}*e=Entry{};Lua50::PushBool(L,true);Lua50::PushString(L,"UNTRACKED");return 2;}
int dispatchGet(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!parseGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}++g_snapshotCalls;Entry*e=find(g);if(!e){Lua50::PushNil(L);Lua50::PushString(L,"NOT_TRACKED");return 2;}return pushEntry(L,*e);}
int dispatchList(Lua50::State L){initialize();Lua50::NewTable(L);int idx=1;for(unsigned i=0;i<MAX_TRACKED;++i){if(!g_entries[i].used)continue;Lua50::PushNumber(L,idx++);pushEntry(L,g_entries[i]);Lua50::SetTable(L,-3);}return 1;}
int dispatchClear(Lua50::State L){initialize();unsigned n=0;for(unsigned i=0;i<MAX_TRACKED;++i)if(g_entries[i].used){g_entries[i]=Entry{};++n;}Lua50::PushNumber(L,n);return 1;}
}
