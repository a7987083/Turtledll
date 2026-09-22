#include "custom_event_bridge.h"

namespace TysCustomEvents {
namespace {

constexpr std::uintptr_t FRAMESCRIPT_EVENT_OBJECT_DATA = 0x00CEEF68u;
constexpr std::uintptr_t SIGNAL_EVENT_PARAM = 0x00703F50u;
constexpr std::uintptr_t SSTR_DUP_A = 0x0064A620u;

using SignalEventParamFn = int (__cdecl *)(int eventCode, char* format, ...);
using SStrDupAFn = char* (__stdcall *)(char* source, char* source2, int tag);

static int g_unitHealth=-1,g_unitPower=-1,g_unitCombat=-1;
static int g_cdStarted=-1,g_cdChanged=-1,g_cdReady=-1;

static bool readableProtection(DWORD protection){
    const DWORD p=protection&0xffu;
    return p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||
           p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}
static bool readable(std::uintptr_t address,std::size_t bytes){
    if(!address||!bytes)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((const void*)address,&m,sizeof(m))!=sizeof(m))return false;
    if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!readableProtection(m.Protect))return false;
    const std::uintptr_t begin=(std::uintptr_t)m.BaseAddress;
    const std::uintptr_t end=begin+m.RegionSize;
    return address>=begin&&address<=end&&bytes<=end-address;
}
static bool executable(std::uintptr_t address){
    MEMORY_BASIC_INFORMATION m={};
    if(!address||VirtualQuery((const void*)address,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&0xffu;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}
static bool sameText(const char*a,const char*b){
    if(!a||!b)return false;
    for(unsigned i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}
}
static void copyText(char*d,unsigned cap,const char*s){
    if(!d||!cap)return;unsigned i=0;if(s)for(;s[i]&&i+1<cap;++i)d[i]=s[i];d[i]=0;
}
static char** eventData(){
    if(!readable(FRAMESCRIPT_EVENT_OBJECT_DATA,sizeof(char**)))return 0;
    char** data=*(char***)FRAMESCRIPT_EVENT_OBJECT_DATA;
    if(!data||!readable((std::uintptr_t)data,sizeof(char*)*EVENT_COUNT_EXPANDED*4u))return 0;
    return data;
}
static void guidText(unsigned long long guid,char out[19]){
    static const char hex[]="0123456789ABCDEF";
    out[0]='0';out[1]='x';
    for(int i=0;i<16;++i){const unsigned shift=(15u-(unsigned)i)*4u;out[2+i]=hex[(unsigned)((guid>>shift)&0xFu)];}
    out[18]=0;
}
static bool emitCooldown(int slot,std::uint32_t spellId,std::uint32_t startMs,
                         std::uint32_t durationMs,std::uint32_t endMs,
                         std::uint32_t remainingMs,std::uint32_t enable,
                         std::uint32_t kind,std::uint32_t source){
    if(slot<0||!executable(SIGNAL_EVENT_PARAM))return false;
    static char fmt[]="%d%d%d%d%d%d%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(slot,fmt,spellId,startMs,durationMs,endMs,remainingMs,enable,kind,source);
    return true;
}

} // namespace

bool available(){return eventData()!=0&&executable(SIGNAL_EVENT_PARAM)&&executable(SSTR_DUP_A);}

int find(const char*name){
    char** data=eventData();if(!data||!name)return -1;
    for(int i=0;i<(int)EVENT_COUNT_EXPANDED;++i){
        char* existing=data[i*4];
        if(existing&&readable((std::uintptr_t)existing,1)&&sameText(existing,name))return i;
    }
    return -1;
}

int claim(const char*name){
    char** data=eventData();if(!data||!name||!executable(SSTR_DUP_A))return -1;
    const int old=find(name);if(old>=0)return old;
    for(int i=0;i<(int)EVENT_COUNT_EXPANDED;++i){
        if(data[i*4])continue;
        char temp[64]={};copyText(temp,sizeof(temp),name);
        char* stored=((SStrDupAFn)SSTR_DUP_A)(temp,temp,1308);
        if(!stored)return -1;
        data[i*4]=stored;
        return i;
    }
    return -1;
}

bool ensureUnitStateEvents(){
    if(g_unitHealth<0)g_unitHealth=claim("TYS_UNIT_HEALTH_CHANGED");
    if(g_unitPower<0)g_unitPower=claim("TYS_UNIT_POWER_CHANGED");
    if(g_unitCombat<0)g_unitCombat=claim("TYS_UNIT_COMBAT_CHANGED");
    return g_unitHealth>=0&&g_unitPower>=0&&g_unitCombat>=0;
}

bool emitUnitHealth(unsigned long long guid,std::uint32_t oldHealth,std::uint32_t newHealth,std::uint32_t maxHealth,bool dead){
    if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    char g[19]={};guidText(guid,g);static char fmt[]="%s%d%d%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitHealth,fmt,g,oldHealth,newHealth,maxHealth,dead?1u:0u);
    return true;
}
bool emitUnitPower(unsigned long long guid,std::uint32_t powerType,std::uint32_t oldPower,std::uint32_t newPower,std::uint32_t maxPower,std::uint32_t changeMask){
    if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    char g[19]={};guidText(guid,g);static char fmt[]="%s%d%d%d%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitPower,fmt,g,powerType,oldPower,newPower,maxPower,changeMask);
    return true;
}
bool emitUnitCombat(unsigned long long guid,bool oldCombat,bool newCombat){
    if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;
    char g[19]={};guidText(guid,g);static char fmt[]="%s%d%d";
    ((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitCombat,fmt,g,oldCombat?1u:0u,newCombat?1u:0u);
    return true;
}

bool ensureCooldownEvents(){
    if(g_cdStarted<0)g_cdStarted=claim("TYS_COOLDOWN_STARTED");
    if(g_cdChanged<0)g_cdChanged=claim("TYS_COOLDOWN_CHANGED");
    if(g_cdReady<0)g_cdReady=claim("TYS_COOLDOWN_READY");
    return g_cdStarted>=0&&g_cdChanged>=0&&g_cdReady>=0;
}
bool emitCooldownStarted(std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h){if(!ensureCooldownEvents())return false;return emitCooldown(g_cdStarted,a,b,c,d,e,f,g,h);}
bool emitCooldownChanged(std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h){if(!ensureCooldownEvents())return false;return emitCooldown(g_cdChanged,a,b,c,d,e,f,g,h);}
bool emitCooldownReady(std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g,std::uint32_t h){if(!ensureCooldownEvents())return false;return emitCooldown(g_cdReady,a,b,c,d,e,f,g,h);}

int unitHealthSlot(){return g_unitHealth;}
int unitPowerSlot(){return g_unitPower;}
int unitCombatSlot(){return g_unitCombat;}
int cooldownStartedSlot(){return g_cdStarted;}
int cooldownChangedSlot(){return g_cdChanged;}
int cooldownReadySlot(){return g_cdReady;}

} // namespace TysCustomEvents
