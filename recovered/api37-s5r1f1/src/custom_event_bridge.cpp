#include "custom_event_bridge.h"
namespace TysCustomEvents { namespace {
constexpr unsigned long FRAMESCRIPT_EVENT_OBJECT_DATA=0x00CEEF68u;
constexpr unsigned long SIGNAL_EVENT_PARAM=0x00703F50u;
constexpr unsigned long SSTR_DUP_A=0x0064A620u;
using SignalEventParamFn=int (__cdecl *)(int,char*,...);
using SStrDupAFn=char* (__stdcall *)(char*,char*,int);
static int g_unitHealth=-1,g_unitPower=-1,g_unitCombat=-1,g_cdStarted=-1,g_cdChanged=-1,g_cdReady=-1;
static bool readableProtection(DWORD p){p&=0xffu;return p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool readable(unsigned long a,unsigned long n){if(!a||!n)return false;MEMORY_BASIC_INFORMATION m={};if(VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m))return false;if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!readableProtection(m.Protect))return false;unsigned long b=(unsigned long)m.BaseAddress,e=b+(unsigned long)m.RegionSize;return a>=b&&a<=e&&n<=e-a;}
static bool executable(unsigned long a){if(!a)return false;MEMORY_BASIC_INFORMATION m={};if(VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;DWORD p=m.Protect&0xffu;return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}
static void copyText(char*d,unsigned cap,const char*s){if(!d||!cap)return;unsigned i=0;if(s)for(;s[i]&&i+1<cap;++i)d[i]=s[i];d[i]=0;}
static char** eventData(){if(!readable(FRAMESCRIPT_EVENT_OBJECT_DATA,sizeof(char**)))return 0;char**d=*(char***)FRAMESCRIPT_EVENT_OBJECT_DATA;if(!d||!readable((unsigned long)d,sizeof(char*)*EVENT_COUNT_EXPANDED*4u))return 0;return d;}
static void guidText(unsigned long long g,char out[19]){static const char h[]="0123456789ABCDEF";out[0]='0';out[1]='x';for(int i=0;i<16;++i){unsigned s=(15u-(unsigned)i)*4u;out[2+i]=h[(unsigned)((g>>s)&0xFu)];}out[18]=0;}
static bool emitCooldown(int slot,unsigned long a,unsigned long b,unsigned long c,unsigned long d,unsigned long e,unsigned long f,unsigned long g,unsigned long h){if(slot<0||!executable(SIGNAL_EVENT_PARAM))return false;static char fmt[]="%d%d%d%d%d%d%d%d";((SignalEventParamFn)SIGNAL_EVENT_PARAM)(slot,fmt,a,b,c,d,e,f,g,h);return true;}
}
bool available(){return eventData()!=0&&executable(SIGNAL_EVENT_PARAM)&&executable(SSTR_DUP_A);}
int find(const char*n){char**d=eventData();if(!d||!n)return -1;for(int i=0;i<(int)EVENT_COUNT_EXPANDED;++i){char*e=d[i*4];if(e&&readable((unsigned long)e,1)&&sameText(e,n))return i;}return -1;}
int claim(const char*n){char**d=eventData();if(!d||!n||!executable(SSTR_DUP_A))return -1;int old=find(n);if(old>=0)return old;for(int i=0;i<(int)EVENT_COUNT_EXPANDED;++i){if(d[i*4])continue;char t[64]={};copyText(t,sizeof(t),n);char*s=((SStrDupAFn)SSTR_DUP_A)(t,t,1308);if(!s)return -1;d[i*4]=s;return i;}return -1;}
bool ensureUnitStateEvents(){if(g_unitHealth<0)g_unitHealth=claim("TYS_UNIT_HEALTH_CHANGED");if(g_unitPower<0)g_unitPower=claim("TYS_UNIT_POWER_CHANGED");if(g_unitCombat<0)g_unitCombat=claim("TYS_UNIT_COMBAT_CHANGED");return g_unitHealth>=0&&g_unitPower>=0&&g_unitCombat>=0;}
bool emitUnitHealth(unsigned long long guid,unsigned long oldH,unsigned long newH,unsigned long maxH,bool dead){if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;char g[19]={};guidText(guid,g);static char f[]="%s%d%d%d%d";((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitHealth,f,g,oldH,newH,maxH,dead?1u:0u);return true;}
bool emitUnitPower(unsigned long long guid,unsigned long pt,unsigned long oldP,unsigned long newP,unsigned long maxP,unsigned long mask){if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;char g[19]={};guidText(guid,g);static char f[]="%s%d%d%d%d%d";((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitPower,f,g,pt,oldP,newP,maxP,mask);return true;}
bool emitUnitCombat(unsigned long long guid,bool oldC,bool newC){if(!ensureUnitStateEvents()||!executable(SIGNAL_EVENT_PARAM))return false;char g[19]={};guidText(guid,g);static char f[]="%s%d%d";((SignalEventParamFn)SIGNAL_EVENT_PARAM)(g_unitCombat,f,g,oldC?1u:0u,newC?1u:0u);return true;}
bool ensureCooldownEvents(){if(g_cdStarted<0)g_cdStarted=claim("TYS_COOLDOWN_STARTED");if(g_cdChanged<0)g_cdChanged=claim("TYS_COOLDOWN_CHANGED");if(g_cdReady<0)g_cdReady=claim("TYS_COOLDOWN_READY");return g_cdStarted>=0&&g_cdChanged>=0&&g_cdReady>=0;}
bool emitCooldownStarted(unsigned long a,unsigned long b,unsigned long c,unsigned long d,unsigned long e,unsigned long f,unsigned long g,unsigned long h){return ensureCooldownEvents()&&emitCooldown(g_cdStarted,a,b,c,d,e,f,g,h);}
bool emitCooldownChanged(unsigned long a,unsigned long b,unsigned long c,unsigned long d,unsigned long e,unsigned long f,unsigned long g,unsigned long h){return ensureCooldownEvents()&&emitCooldown(g_cdChanged,a,b,c,d,e,f,g,h);}
bool emitCooldownReady(unsigned long a,unsigned long b,unsigned long c,unsigned long d,unsigned long e,unsigned long f,unsigned long g,unsigned long h){return ensureCooldownEvents()&&emitCooldown(g_cdReady,a,b,c,d,e,f,g,h);}
int unitHealthSlot(){return g_unitHealth;}int unitPowerSlot(){return g_unitPower;}int unitCombatSlot(){return g_unitCombat;}int cooldownStartedSlot(){return g_cdStarted;}int cooldownChangedSlot(){return g_cdChanged;}int cooldownReadySlot(){return g_cdReady;}
} // namespace TysCustomEvents
