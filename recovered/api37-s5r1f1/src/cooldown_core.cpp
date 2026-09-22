#include <windows.h>
#include "cooldown_core.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysCooldownCore {
namespace {
constexpr unsigned MAX_TRACKED=128;
struct Entry { unsigned long spellId; long startMs; long durationMs; long enable; unsigned long lastQueryMs; bool used; bool valid; };
static Entry g_entries[MAX_TRACKED]={};
static volatile LONG g_init=0,g_inSub=0,g_tickSub=0;
static volatile LONG g_engineQueries=0,g_queryFailures=0,g_spellCooldownPackets=0,g_clearCooldownPackets=0,g_cooldownCheatPackets=0,g_cooldownEventPackets=0,g_ignoredRemotePackets=0,g_deadlineWakes=0;
static unsigned long g_lastPacketSpellId=0,g_lastChangedSpellId=0,g_lastChangeMs=0;
static char g_status[96]="NOT_INITIALIZED";
using QueryFn = void (__fastcall *)(unsigned long spellId, unsigned long unknown, long* startMs, long* durationMs, long* enable);
static bool executable(unsigned long a){ MEMORY_BASIC_INFORMATION m={}; if(!a||VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false; DWORD p=m.Protect&0xff; return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY; }
static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);} static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);} static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static Entry* slot(unsigned long spell,bool create){ if(!spell)return 0; unsigned freei=MAX_TRACKED; for(unsigned i=0;i<MAX_TRACKED;++i){ if(g_entries[i].used&&g_entries[i].spellId==spell)return &g_entries[i]; if(!g_entries[i].used&&freei==MAX_TRACKED)freei=i;} if(!create||freei==MAX_TRACKED)return 0; Entry&e=g_entries[freei]; e={}; e.used=true;e.spellId=spell;return &e; }
static bool query(unsigned long spell,Entry*out){ if(!spell||!out||!executable(WoW112::COOLDOWN_QUERY_HELPER))return false; ++g_engineQueries; long start=0,duration=0,enable=0; ((QueryFn)WoW112::COOLDOWN_QUERY_HELPER)(spell,0,&start,&duration,&enable); if(start<0){++g_queryFailures;return false;} out->spellId=spell;out->startMs=start;out->durationMs=duration;out->enable=enable;out->lastQueryMs=GetTickCount();out->valid=true;return true; }
static void onIncoming(unsigned long op,TysNativeBus::CDataStoreView*p){
    if(op==WoW112::SMSG_SPELL_COOLDOWN_OPCODE){++g_spellCooldownPackets;}
    else if(op==WoW112::SMSG_CLEAR_COOLDOWN_OPCODE){++g_clearCooldownPackets;}
    else if(op==WoW112::SMSG_COOLDOWN_CHEAT_OPCODE){++g_cooldownCheatPackets;}
    else if(op==WoW112::SMSG_COOLDOWN_EVENT_OPCODE){++g_cooldownEventPackets;}
    else return;
    unsigned long s=0; if(p&&TysNativeBus::read(p,&s)&&s){g_lastPacketSpellId=s;Entry*e=slot(s,true); if(e)e->valid=false;}
}
static void onTick(){ unsigned long now=GetTickCount(); for(unsigned i=0;i<MAX_TRACKED;++i){Entry&e=g_entries[i];if(!e.used)continue;if(!e.valid|| (e.durationMs>0 && now-e.lastQueryMs>=50)){Entry n=e;if(query(e.spellId,&n)){bool changed=!e.valid||e.startMs!=n.startMs||e.durationMs!=n.durationMs||e.enable!=n.enable;e=n;if(changed){g_lastChangedSpellId=e.spellId;g_lastChangeMs=now;}}}} }
static int pushEntry(Lua50::State L,const Entry&e){ Lua50::NewTable(L);setNum(L,"spellId",e.spellId);setBool(L,"valid",e.valid);setNum(L,"startMs",e.startMs);setNum(L,"recoveryTimeMs",e.durationMs);setNum(L,"enable",e.enable);unsigned long now=GetTickCount(); long remain=e.valid&&e.durationMs>0?(long)(e.startMs+e.durationMs-(long)now):0;if(remain<0)remain=0;setNum(L,"remainingMs",remain);setBool(L,"ready",e.valid&&remain==0);setStr(L,"querySource","CLIENT_ENGINE_COOLDOWN_MANAGER");return 1; }
}

bool initialize(){ if(InterlockedCompareExchange(&g_init,1,0)!=0)return true; bool a=TysNativeBus::subscribeIncoming(&onIncoming);bool b=TysNativeBus::subscribeWorldTick(&onTick);InterlockedExchange(&g_inSub,a?1:0);InterlockedExchange(&g_tickSub,b?1:0); const char*s=(a&&b&&executable(WoW112::COOLDOWN_QUERY_HELPER))?"READY_COOLDOWN_CORE_C1R2":"PARTIAL_COOLDOWN_CORE_C1R2"; unsigned i=0;for(;s[i]&&i+1<sizeof(g_status);++i)g_status[i]=s[i];g_status[i]=0;return a&&b; }
const char* status(){return g_status;}
int dispatchStatus(Lua50::State L){Lua50::NewTable(L);setStr(L,"stage","CD1-R2");setStr(L,"status",g_status);setBool(L,"incomingSubscribed",g_inSub!=0);setBool(L,"worldTickSubscribed",g_tickSub!=0);setNum(L,"queryHelperAddress",WoW112::COOLDOWN_QUERY_HELPER);setBool(L,"queryHelperReady",executable(WoW112::COOLDOWN_QUERY_HELPER));setNum(L,"engineQueries",g_engineQueries);setNum(L,"queryFailures",g_queryFailures);setNum(L,"spellCooldownPackets",g_spellCooldownPackets);setNum(L,"clearCooldownPackets",g_clearCooldownPackets);setNum(L,"cooldownCheatPackets",g_cooldownCheatPackets);setNum(L,"cooldownEventPackets",g_cooldownEventPackets);setNum(L,"lastPacketSpellId",g_lastPacketSpellId);setNum(L,"lastChangedSpellId",g_lastChangedSpellId);setNum(L,"lastChangeMs",g_lastChangeMs);setStr(L,"readySemantics","READY_ONLY_WHEN_ENGINE_REPORTS_NO_COOLDOWN");setStr(L,"resetSemantics","CLEAR_OR_CHEAT_POST_HANDLER_ENGINE_REQUERY");setBool(L,"spellbookPolling",false);setBool(L,"backgroundThread",false);setBool(L,"directHook",false);return 1;}
int dispatchGet(Lua50::State L){if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_SPELL_ID");return 2;}unsigned long s=(unsigned long)Lua50::ToNumber(L,2);Entry*e=slot(s,true);if(!e){Lua50::PushNil(L);Lua50::PushString(L,"CAPACITY");return 2;}Entry n=*e;if(!query(s,&n)){Lua50::PushNil(L);Lua50::PushString(L,"QUERY_FAILED");return 2;}*e=n;return pushEntry(L,*e);}
int dispatchList(Lua50::State L){Lua50::NewTable(L);int idx=1;for(unsigned i=0;i<MAX_TRACKED;++i){if(!g_entries[i].used)continue;Lua50::PushNumber(L,idx++);pushEntry(L,g_entries[i]);Lua50::SetTable(L,-3);}return 1;}
}
