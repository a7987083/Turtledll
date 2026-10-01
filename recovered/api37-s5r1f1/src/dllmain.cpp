#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "wow112_offsets.h"
#include "lua50.h"
#include "dbc_internal.h"
#include "visual_native.h"
#include "moonmarker_compat.h"
#include "m2scan_media.h"
#include "moonmarker_advanced.h"
#include "moonmarker_runtime_guard.h"
#include "profiler_handoff_b1r5r8.h"
#include "dreamavatar_native.h"
#include "dreamweapon_native.h"
#include "drinkwalk_native.h"
#include "los_pair_cache.h"
#include "aura_native.h"
#include "aura_duration.h"
#include "aura_caster.h"
#include "aura_state.h"
#include "spell_cast_core.h"
#include "native_bus.h"
#include "corpse_marker.h"
#include "foundation_f1.h"
#include "foundation_stats.h"
#include "cooldown_core.h"
#include "unit_state_core.h"
#include "spatial_core.h"

extern "C" int _fltused = 0;
extern "C" void* __cdecl memcpy(void* d,const void* s,unsigned int n){ unsigned char* a=(unsigned char*)d; const unsigned char* b=(const unsigned char*)s; for(unsigned int i=0;i<n;++i)a[i]=b[i]; return d; }
extern "C" void* __cdecl memset(void* d,int c,unsigned int n){ unsigned char* a=(unsigned char*)d; for(unsigned int i=0;i<n;++i)a[i]=(unsigned char)c; return d; }
extern "C" void* __cdecl memmove(void* d,const void* s,unsigned int n){ unsigned char* a=(unsigned char*)d; const unsigned char* b=(const unsigned char*)s; if(a==b||n==0)return d; if(a<b){ for(unsigned int i=0;i<n;++i)a[i]=b[i]; } else { for(unsigned int i=n;i>0;--i)a[i-1]=b[i-1]; } return d; }

namespace TYS {
constexpr const char* DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1-F1-CD1R2-US1R2-S5R1F1";
constexpr const char* API_VERSION = "37";
constexpr const char* BUILD_ID = "20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration";
constexpr DWORD DLL_PROCESS_ATTACH_ = 1;
constexpr DWORD DLL_PROCESS_DETACH_ = 0;

static HMODULE g_self = 0;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static volatile LONG g_loadOnce = 0;
static volatile LONG g_loaded = 0;
static volatile LONG g_luaRegistered = 0;
static Lua50::State g_lastRegisteredState = 0;
static char g_lastRegisterCode[64] = "NOT_REGISTERED";
static char g_conflictReport[8192] = "NOT_SCANNED";
static char g_recordScratch[65536] = {0};
static char g_snapshotRows[60000] = {0};

static volatile LONG g_facingEnabled = 1; // PLR2: available for the entire WoW process.
static char g_facingStatus[64] = "AVAILABLE_LAZY";
static char g_facingResolveObserve[256] = "NOT_SCANNED";

static volatile LONG g_speedEnabled = 1; // PLR2: available for the entire WoW process.
static char g_speedStatus[64] = "AVAILABLE_LAZY";
static char g_speedResolveObserve[256] = "NOT_SCANNED";
static char g_speedEffectiveObserve[256] = "NOT_SCANNED";

static volatile LONG g_groundProbeEnabled = 1; // PLR2: explicit-call only; available for process lifetime.
static char g_groundProbeStatus[64] = "AVAILABLE_LAZY";
static char g_groundProbeObserve[640] = "NOT_SCANNED";

static volatile LONG g_relationEnabled = 1; // PLR2: explicit-call only; available for process lifetime.
static char g_relationStatus[64] = "AVAILABLE_LAZY";
static char g_relationObserve[512] = "NOT_SCANNED";

static volatile LONG g_dbcEnabled = 1; // PLR2: available; MPQ/DBC loading remains lazy.
static char g_dbcStatus[64] = "AVAILABLE_LAZY";
static char g_dbcObserve[1536] = "NOT_SCANNED";

static volatile LONG g_visualEnabled = 1; // PLR2: available; render hooks remain lazy until first consumer.
static char g_visualStatus[64] = "AVAILABLE_LAZY";
static char g_visualObserve[2304] = "NOT_SCANNED";

static volatile LONG g_dreamAvatarEnabled = 1; // PLR2: available; native preflight remains lazy.
static char g_dreamAvatarStatus[64] = "AVAILABLE_LAZY";

static volatile LONG g_dreamWeaponEnabled = 1; // PLR2: available; native preflight remains lazy.
static char g_dreamWeaponStatus[64] = "AVAILABLE_LAZY";
// DW5: DAS1 is an explicit-call shared transport, not a feature toggle. This
// flag only caches ABI preflight; it creates no hook/timer/thread/background work.
static volatile LONG g_das1PreflightReady = 0;
static char g_das1Status[64] = "NOT_CHECKED";

// PLR2 process-lifetime runtime state. Features are always available; readiness
// is separate and initialized only when the corresponding API is actually used.
static volatile LONG g_dbcReady = 0;
static volatile LONG g_dreamAvatarReady = 0;
static volatile LONG g_dreamWeaponReady = 0;
static Lua50::State g_runtimeLifecycleState = 0;
static volatile LONG g_worldPhase = 0; // 0 unknown, 1 entered, 2 leaving
static char g_runtimeLifecycleStatus[96] = "NOT_INSTALLED";

using SceneEndFn = void (__thiscall *)(unsigned long);
using ScenePresentFn = void (__thiscall *)(void*, int);
static SceneEndFn g_visualSceneNext = 0;
static ScenePresentFn g_scenePresentNext = 0;
static bool g_visualSceneCreated = false;
static bool g_visualSceneEnabled = false;
static bool g_scenePresentCreated = false;
static bool g_scenePresentEnabled = false;
static unsigned long g_visualSceneTarget = 0;
static unsigned long g_scenePresentTarget = 0;
static volatile LONG g_sceneIsPresenting = 0;
static char g_visualSceneMode[96] = "NOT_PREPARED";
static char g_scenePresentMode[96] = "NOT_PREPARED";
constexpr unsigned long GXDEVICE_SCENE_PRESENT = 0x00592430UL;

using FrameRegisterFn = void (__fastcall *)(const char*, Lua50::CFunction);
using LoadScriptFn = void (__stdcall *)();
using ResolveUnitFn = void* (__fastcall *)(const char*);
using EffectiveSpeedFn = float (__thiscall *)(void*, int);
using UnitReactionFn = int (__thiscall *)(void*, void*);
using CanAttackFn = bool (__thiscall *)(void*, void*);
using GetCreatureTypeFn = int (__fastcall *)(void*);
using GetPositionFn = float* (__thiscall *)(void*, float*);
using CWorldIntersectFn = bool (__fastcall *)(const float*, const float*, int, float*, float*, unsigned long);

static LoadScriptFn g_playerNext = 0;
static bool g_playerCreated = false;
static bool g_playerEnabled = false;
static unsigned long g_playerHookTarget = 0;
static char g_playerHookMode[96] = "NOT_PREPARED";
static char g_playerObserve[256] = "NOT_SCANNED";
static char g_glueObserve[256] = "NOT_SCANNED";
static char g_frameObserve[256] = "NOT_SCANNED";

static unsigned long slen(const char* s){ unsigned long n=0; if(s)while(s[n])++n; return n; }
static bool eq(const char* a,const char* b){ if(!a||!b)return false; while(*a&&*b){ if(*a!=*b)return false; ++a;++b;} return *a==*b; }
static bool ieq(const char* a,const char* b){ if(!a||!b)return false; while(*a&&*b){ char x=*a,y=*b; if(x>='A'&&x<='Z')x=(char)(x+32); if(y>='A'&&y<='Z')y=(char)(y+32); if(x!=y)return false; ++a;++b;} return *a==*b; }
static bool startsI(const char* s,const char* p){ if(!s||!p)return false; while(*p){ char x=*s,y=*p; if(!x)return false; if(x>='A'&&x<='Z')x=(char)(x+32); if(y>='A'&&y<='Z')y=(char)(y+32); if(x!=y)return false; ++s;++p;} return true; }
static void cpy(char* out,unsigned long cap,const char* s){ if(!cap)return; unsigned long i=0; if(s){ for(;s[i]&&i+1<cap;++i)out[i]=s[i]; } out[i]=0; }
static void cat(char* out,unsigned long cap,const char* s){ unsigned long n=slen(out); if(n>=cap)return; cpy(out+n,cap-n,s); }
static const char* baseName(const char* p){ if(!p)return ""; const char* r=p; while(*p){ if(*p=='\\'||*p=='/')r=p+1; ++p;} return r; }
static char hexDigit(unsigned v){ return (char)(v<10?('0'+v):('A'+v-10)); }
static void hex32(char* out,unsigned long cap,unsigned long v){ if(cap<11){ if(cap)out[0]=0; return; } out[0]='0';out[1]='x'; for(int i=0;i<8;++i){ unsigned shift=(unsigned)(28-i*4); out[2+i]=hexDigit((v>>shift)&15); } out[10]=0; }
static void hexBytes(char* out,unsigned long cap,const unsigned char* p,unsigned count){ if(!cap)return; out[0]=0; for(unsigned i=0;i<count;++i){ unsigned long n=slen(out); if(n+3>=cap)break; if(i){out[n++]=' ';out[n]=0;} out[n++]=hexDigit((p[i]>>4)&15); out[n++]=hexDigit(p[i]&15); out[n]=0; } }

static void logOpen(){
    if(g_log!=INVALID_HANDLE_VALUE)return;
    char path[MAX_PATH]={0};
    if(!GetModuleFileNameA(g_self,path,MAX_PATH))return;
    char* last=path; for(char* p=path;*p;++p)if(*p=='\\'||*p=='/')last=p+1;
    cpy(last,(unsigned long)(MAX_PATH-(last-path)),"taiyangshendian.load.log");
    g_log=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
}
static void logText(const char* s){ if(g_log==INVALID_HANDLE_VALUE||!s)return; DWORD n=0; WriteFile(g_log,s,slen(s),&n,0); }

struct BuildInfo { bool ok; unsigned long timestamp; unsigned long base; unsigned short machine; unsigned short magic; };
static BuildInfo detectBuild(){
    BuildInfo r={false,0,0,0,0}; unsigned char* base=(unsigned char*)GetModuleHandleA(0); if(!base)return r;
    r.base=(unsigned long)base; if(*(unsigned short*)base!=0x5A4D)return r;
    unsigned long peoff=*(unsigned long*)(base+0x3C); unsigned char* pe=base+peoff;
    if(*(unsigned long*)pe!=0x00004550UL)return r;
    r.machine=*(unsigned short*)(pe+4); r.timestamp=*(unsigned long*)(pe+8); r.magic=*(unsigned short*)(pe+24);
    r.ok=(r.base==WoW112::IMAGE_BASE && r.machine==WoW112::MACHINE_I386 && r.magic==WoW112::PE32_MAGIC && r.timestamp==WoW112::PE_TIMESTAMP);
    return r;
}
static bool executable(unsigned long a){ MEMORY_BASIC_INFORMATION m={}; if(VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m))return false; if(m.State!=MEM_COMMIT)return false; DWORD p=m.Protect&0xFF; return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY; }
static bool readable(const void* p,unsigned long n){
    if(!p||!n)return false; MEMORY_BASIC_INFORMATION m={}; if(VirtualQuery(p,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;
    DWORD q=m.Protect&0xFF; if(q==PAGE_NOACCESS||q==PAGE_EXECUTE)return false;
    unsigned long a=(unsigned long)p, b=(unsigned long)m.BaseAddress+(unsigned long)m.RegionSize; return a+n>=a && a+n<=b;
}

struct EntryInfo { bool jmpPatched; bool startsCall; unsigned long destination; char owner[128]; char kind[24]; char bytes[64]; };
static EntryInfo inspectEntry(unsigned long a){
    EntryInfo r={false,false,0,{0},{0},{0}}; cpy(r.owner,sizeof(r.owner),"WoW.exe"); cpy(r.kind,sizeof(r.kind),"NORMAL");
    const unsigned char* p=(const unsigned char*)a; hexBytes(r.bytes,sizeof(r.bytes),p,12);
    if(p[0]==0xE9){ r.jmpPatched=true; r.destination=a+5+(unsigned long)(*(const long*)(p+1)); cpy(r.kind,sizeof(r.kind),"JMP_REL32"); }
    else if(p[0]==0xFF&&p[1]==0x25){ r.jmpPatched=true; unsigned long q=*(const unsigned long*)(p+2); r.destination=*(const unsigned long*)q; cpy(r.kind,sizeof(r.kind),"JMP_INDIRECT"); }
    else if(p[0]==0x68&&p[5]==0xC3){ r.jmpPatched=true; r.destination=*(const unsigned long*)(p+1); cpy(r.kind,sizeof(r.kind),"PUSH_RET"); }
    else if(p[0]==0xE8){ r.startsCall=true; r.destination=a+5+(unsigned long)(*(const long*)(p+1)); cpy(r.kind,sizeof(r.kind),"NORMAL_CALL_FIRST_INSN"); }
    if(r.jmpPatched){ MEMORY_BASIC_INFORMATION m={}; if(VirtualQuery((void*)r.destination,&m,sizeof(m))==sizeof(m)&&m.AllocationBase){ char full[MAX_PATH]={0}; if(GetModuleFileNameA((HMODULE)m.AllocationBase,full,MAX_PATH))cpy(r.owner,sizeof(r.owner),baseName(full)); else cpy(r.owner,sizeof(r.owner),"UNKNOWN_MODULE"); } else cpy(r.owner,sizeof(r.owner),"UNKNOWN_ADDRESS"); }
    return r;
}
static void formatEntry(char* out,unsigned long cap,const EntryInfo& e){ out[0]=0; cat(out,cap,"OWNER=");cat(out,cap,e.owner);cat(out,cap,"|KIND=");cat(out,cap,e.kind);cat(out,cap,"|DEST="); char h[16]={0};hex32(h,sizeof(h),e.destination);cat(out,cap,h);cat(out,cap,"|BYTES=");cat(out,cap,e.bytes); }
static EntryInfo observeEntry(unsigned long target,const char* label,char* out,unsigned long cap){ EntryInfo e=inspectEntry(target); formatEntry(out,cap,e); logText(label); logText(": "); logText(out); logText("\r\n"); return e; }

static bool validUnitToken(const char* s){
    if(!s||!*s)return false;
    if(ieq(s,"player")||ieq(s,"target")||ieq(s,"mouseover")||ieq(s,"pet"))return true;
    if(startsI(s,"party")){ const char* p=s+5; return p[0]>='1'&&p[0]<='4'&&p[1]==0; }
    if(startsI(s,"raid")){ const char* p=s+4; int n=0; if(!*p)return false; while(*p>='0'&&*p<='9'){n=n*10+(*p-'0');++p;} return *p==0&&n>=1&&n<=40; }
    return false;
}
static void* resolveUnit(const char* token){ return ((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)(token); }

static float absf(float v){ return v<0.0f?-v:v; }
static float sqrtApprox(float v){ if(v<=0.0f)return 0.0f; float x=v>1.0f?v:1.0f; for(int i=0;i<8;++i)x=0.5f*(x+v/x); return x; }
static void appendChar(char* out,unsigned long cap,char c){ unsigned long n=slen(out); if(n+1>=cap)return; out[n]=c;out[n+1]=0; }
static void appendUInt(char* out,unsigned long cap,unsigned long v){ char t[16]={0}; int n=0; do{t[n++]=(char)('0'+(v%10));v/=10;}while(v&&n<15); while(n>0)appendChar(out,cap,t[--n]); }
static void appendInt(char* out,unsigned long cap,long v){ if(v<0){appendChar(out,cap,'-'); unsigned long u=(unsigned long)(-(v+1));u+=1;appendUInt(out,cap,u);}else appendUInt(out,cap,(unsigned long)v); }
static void appendFloat4(char* out,unsigned long cap,float v){
    if(!(v==v)){cat(out,cap,"0.0000");return;} bool neg=v<0.0f; if(neg)v=-v; double scaled=(double)v*10000.0+0.5; unsigned long q=(unsigned long)scaled; unsigned long whole=q/10000UL, frac=q%10000UL;
    if(neg)appendChar(out,cap,'-'); appendUInt(out,cap,whole); appendChar(out,cap,'.'); appendChar(out,cap,(char)('0'+(frac/1000)%10));appendChar(out,cap,(char)('0'+(frac/100)%10));appendChar(out,cap,(char)('0'+(frac/10)%10));appendChar(out,cap,(char)('0'+frac%10));
}
static void hex64(char* out,unsigned long cap,unsigned long long v){ if(cap<17){if(cap)out[0]=0;return;} for(int i=0;i<16;++i){unsigned shift=(unsigned)(60-i*4);out[i]=hexDigit((unsigned)((v>>shift)&15ULL));}out[16]=0; }
static int hexVal(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static bool parseGuid(const char* s,unsigned long long* out){
    if(!s||!out)return false; while(*s==' '||*s=='\t')++s; if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2; unsigned long long v=0; int digits=0;
    while(*s){ int h=hexVal(*s); if(h<0)break; if(digits>=16)return false; v=(v<<4)|(unsigned)h; ++digits;++s; }
    while(*s==' '||*s=='\t')++s; if(*s||digits==0||v==0)return false; *out=v; return true;
}
static bool objectType(void* obj,unsigned long* type){ if(!obj||!type||!readable((unsigned char*)obj+WoW112::OFF_CGOBJECT_TYPE,4))return false; *type=*(unsigned long*)((unsigned char*)obj+WoW112::OFF_CGOBJECT_TYPE); return true; }
static bool objectGuid(void* obj,unsigned long long* guid){ if(!obj||!guid||!readable((unsigned char*)obj+WoW112::OFF_CGOBJECT_GUID,8))return false; *guid=*(unsigned long long*)((unsigned char*)obj+WoW112::OFF_CGOBJECT_GUID); return *guid!=0; }
static bool objectEntry(void* obj,unsigned long* entry){
    if(!obj||!entry||!readable((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR,4))return false; unsigned char* d=*(unsigned char**)((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR); if(!d||!readable(d+WoW112::OFF_OBJECT_FIELD_ENTRY,4))return false; *entry=*(unsigned long*)(d+WoW112::OFF_OBJECT_FIELD_ENTRY); return true;
}
static bool objectPosition(void* obj,float out[3]){
    if(!obj||!out||!readable(obj,4))return false; void** vt=*(void***)obj; if(!vt||!readable((unsigned char*)vt+WoW112::OFF_CGOBJECT_VTBL_GET_POSITION,4))return false; void* f=*(void**)((unsigned char*)vt+WoW112::OFF_CGOBJECT_VTBL_GET_POSITION); if(!f||!executable((unsigned long)f))return false; float tmp[3]={0,0,0}; float* p=((GetPositionFn)f)(obj,tmp); if(!p)return false; if(p!=tmp){if(!readable(p,12))return false;tmp[0]=p[0];tmp[1]=p[1];tmp[2]=p[2];} if(!(tmp[0]==tmp[0])||!(tmp[1]==tmp[1])||!(tmp[2]==tmp[2]))return false; out[0]=tmp[0];out[1]=tmp[1];out[2]=tmp[2];return true;
}
static bool objectManager(unsigned long* mgr,unsigned long* head,long* nextBase){
    if(!mgr||!head||!nextBase||!readable((void*)WoW112::OBJECT_MANAGER_PTR,4))return false; unsigned long m=*(unsigned long*)WoW112::OBJECT_MANAGER_PTR; if(!m||!readable((void*)(m+WoW112::OFF_OBJECT_MANAGER_HEAD),4)||!readable((void*)(m+WoW112::OFF_OBJECT_MANAGER_NEXT_BASE),4))return false; *mgr=m;*head=*(unsigned long*)(m+WoW112::OFF_OBJECT_MANAGER_HEAD);*nextBase=*(long*)(m+WoW112::OFF_OBJECT_MANAGER_NEXT_BASE);return true;
}
static unsigned long nextObject(unsigned long current,long nextBase){ unsigned long a=(unsigned long)((long)current+nextBase+4); if(!a||!readable((void*)a,4))return 0; return *(unsigned long*)a; }
static void* findObjectByGuid(unsigned long long guid){
    if(!guid)return 0;
    TysFoundationStats::noteFastAttempt();
    if(executable(WoW112::FAST_GUID_LOOKUP)){
        using FastGuidFn=unsigned long(__fastcall*)(unsigned long long);
        unsigned long candidate=((FastGuidFn)WoW112::FAST_GUID_LOOKUP)(guid);
        if(candidate){
            unsigned long long live=0;
            if(!(candidate&1UL)&&readable((void*)candidate,0x38)&&objectGuid((void*)candidate,&live)&&live==guid){
                TysFoundationStats::noteFastHit();return (void*)candidate;
            }
            TysFoundationStats::noteFastRejected();
        }else TysFoundationStats::noteFastMiss();
    }else TysFoundationStats::noteFastMiss();
    TysFoundationStats::noteFallbackAttempt();
    unsigned long mgr=0,cur=0;long base=0;if(!objectManager(&mgr,&cur,&base)){TysFoundationStats::noteFallbackMiss();return 0;}
    for(unsigned n=0;cur&&!(cur&1)&&n<4096;++n){
        if(readable((void*)cur,0x38)){unsigned long long g=0;if(objectGuid((void*)cur,&g)&&g==guid){TysFoundationStats::noteFallbackHit();return (void*)cur;}}
        unsigned long nxt=nextObject(cur,base);if(nxt==cur)break;cur=nxt;
    }
    TysFoundationStats::noteFallbackMiss();return 0;
}

// ARX1/API33 helpers. These are explicit-call-only and install no hooks.
static bool resolveUnitOrGuid(const char* value,void** obj,unsigned long long* guid){
    if(obj)*obj=0;if(guid)*guid=0;if(!value||!*value)return false;
    void* o=0;unsigned long long g=0;
    if(validUnitToken(value)){o=resolveUnit(value);if(!o||!objectGuid(o,&g))return false;}
    else {if(!parseGuid(value,&g))return false;o=findObjectByGuid(g);if(!o)return false;}
    if(obj)*obj=o;if(guid)*guid=g;return true;
}
static bool unitCollisionHeight(void* obj,float* out){
    if(!obj||!out)return false;unsigned long t=0;if(!objectType(obj,&t)||(t!=WoW112::TYPE_UNIT&&t!=WoW112::TYPE_PLAYER))return false;
    unsigned char* u=(unsigned char*)obj;if(!readable(u+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR,4))return false;
    unsigned char* m=*(unsigned char**)(u+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR);if(!m||!readable(m+WoW112::OFF_MOVEMENT_COLLISION_HEIGHT,4))return false;
    float h=*(float*)(m+WoW112::OFF_MOVEMENT_COLLISION_HEIGHT);if(!(h==h)||h<=0.05f||h>100.0f)return false;*out=h;return true;
}
static bool lineBlocked(const float a[3],const float b[3],bool* blocked){
    if(!a||!b||!blocked||!executable(WoW112::CWORLD_INTERSECT))return false;float hit[3]={0,0,0};float distance=1.0f;
    bool result=((CWorldIntersectFn)WoW112::CWORLD_INTERSECT)(a,b,0,hit,&distance,WoW112::CWORLD_INTERSECT_LOS_FLAGS);
    *blocked=result&&distance>=0.0f&&distance<=1.0f;return true;
}
static bool unitLineOfSight(void* aObj,void* bObj,bool* inSight){
    if(!aObj||!bObj||!inSight)return false;if(aObj==bObj){*inSight=true;return true;}
    unsigned long ta=0,tb=0;if(!objectType(aObj,&ta)||!objectType(bObj,&tb))return false;
    if((ta!=WoW112::TYPE_UNIT&&ta!=WoW112::TYPE_PLAYER)||(tb!=WoW112::TYPE_UNIT&&tb!=WoW112::TYPE_PLAYER))return false;
    float ha=2.4f,hb=2.4f,tmp=0.0f;if(unitCollisionHeight(aObj,&tmp))ha=tmp;if(unitCollisionHeight(bObj,&tmp))hb=tmp;
    // Keep B as the taller unit. This mirrors the newer UnitXP_SP3 1.12 logic:
    // first test equal eye height, then (only if needed) the taller target's own height.
    if(ha>hb){void* to=aObj;aObj=bObj;bObj=to;float th=ha;ha=hb;hb=th;}
    float a[3]={0,0,0},b[3]={0,0,0};if(!objectPosition(aObj,a)||!objectPosition(bObj,b))return false;
    a[2]+=ha;b[2]+=ha;bool blocked=false;if(!lineBlocked(a,b,&blocked))return false;if(!blocked){*inSight=true;return true;}
    if(absf(ha-hb)<0.001f){*inSight=false;return true;}
    if(!objectPosition(aObj,a)||!objectPosition(bObj,b))return false;a[2]+=ha;b[2]+=hb;if(!lineBlocked(a,b,&blocked))return false;*inSight=!blocked;return true;
}
static void distanceInfo(const float p[3],const float q[3],float* xy,float* dz){ float dx=p[0]-q[0],dy=p[1]-q[1]; if(xy)*xy=sqrtApprox(dx*dx+dy*dy); if(dz)*dz=absf(p[2]-q[2]); }
static void appendUnitRecord(char* out,unsigned long cap,void* obj,const float player[3]){
    unsigned long long g=0;unsigned long e=0,t=0;float p[3]={0};if(!objectGuid(obj,&g)||!objectType(obj,&t)||!objectPosition(obj,p))return;objectEntry(obj,&e);char h[17]={0};hex64(h,sizeof(h),g);float xy=0,dz=0;distanceInfo(p,player,&xy,&dz);cat(out,cap,"U|");cat(out,cap,h);appendChar(out,cap,'|');appendUInt(out,cap,e);appendChar(out,cap,'|');appendUInt(out,cap,t);appendChar(out,cap,'|');appendFloat4(out,cap,p[0]);appendChar(out,cap,'|');appendFloat4(out,cap,p[1]);appendChar(out,cap,'|');appendFloat4(out,cap,p[2]);appendChar(out,cap,'|');appendFloat4(out,cap,xy);appendChar(out,cap,'|');appendFloat4(out,cap,dz);
}
static bool dynamicFields(void* obj,unsigned long long* caster,unsigned long* spell,float* radius,float p[3]){
    if(!obj||!readable((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR,4))return false; unsigned char* d=*(unsigned char**)((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR); if(!d||!readable(d+WoW112::OFF_DYNAMIC_CASTER_GUID,0x20))return false;
    *caster=*(unsigned long long*)(d+WoW112::OFF_DYNAMIC_CASTER_GUID);*spell=*(unsigned long*)(d+WoW112::OFF_DYNAMIC_SPELL_ID);*radius=*(float*)(d+WoW112::OFF_DYNAMIC_RADIUS);p[0]=*(float*)(d+WoW112::OFF_DYNAMIC_X);p[1]=*(float*)(d+WoW112::OFF_DYNAMIC_Y);p[2]=*(float*)(d+WoW112::OFF_DYNAMIC_Z);return (*radius==*radius)&&(p[0]==p[0])&&(p[1]==p[1])&&(p[2]==p[2]);
}
static void appendDynamicRecord(char* out,unsigned long cap,void* obj,const float player[3]){
    unsigned long long g=0,c=0;unsigned long sp=0;float r=0,p[3]={0};if(!objectGuid(obj,&g)||!dynamicFields(obj,&c,&sp,&r,p))return;char hg[17]={0},hc[17]={0};hex64(hg,sizeof(hg),g);hex64(hc,sizeof(hc),c);float xy=0,dz=0;distanceInfo(p,player,&xy,&dz);cat(out,cap,"D|");cat(out,cap,hg);appendChar(out,cap,'|');cat(out,cap,hc);appendChar(out,cap,'|');appendUInt(out,cap,sp);appendChar(out,cap,'|');appendFloat4(out,cap,r);appendChar(out,cap,'|');appendFloat4(out,cap,p[0]);appendChar(out,cap,'|');appendFloat4(out,cap,p[1]);appendChar(out,cap,'|');appendFloat4(out,cap,p[2]);appendChar(out,cap,'|');appendFloat4(out,cap,xy);appendChar(out,cap,'|');appendFloat4(out,cap,dz);
}
static void appendGameObjectRecord(char* out,unsigned long cap,void* obj,const float player[3]){
    unsigned long long g=0;unsigned long e=0,t=0;float p[3]={0};if(!objectGuid(obj,&g)||!objectType(obj,&t)||!objectPosition(obj,p))return;objectEntry(obj,&e);char h[17]={0};hex64(h,sizeof(h),g);float xy=0,dz=0;distanceInfo(p,player,&xy,&dz);cat(out,cap,"O|");cat(out,cap,h);appendChar(out,cap,'|');appendUInt(out,cap,e);appendChar(out,cap,'|');appendUInt(out,cap,t);appendChar(out,cap,'|');appendFloat4(out,cap,p[0]);appendChar(out,cap,'|');appendFloat4(out,cap,p[1]);appendChar(out,cap,'|');appendFloat4(out,cap,p[2]);appendChar(out,cap,'|');appendFloat4(out,cap,xy);appendChar(out,cap,'|');appendFloat4(out,cap,dz);
}
static const char* scopeForGuid(unsigned long long guid){
    unsigned long long g=0;void* u=resolveUnit("player");if(u&&objectGuid(u,&g)&&g==guid)return "SELF"; char tok[8]={0};
    for(int i=1;i<=4;++i){cpy(tok,sizeof(tok),"party");appendChar(tok,sizeof(tok),(char)('0'+i));u=resolveUnit(tok);if(u&&objectGuid(u,&g)&&g==guid)return "PARTY";}
    for(int i=1;i<=40;++i){cpy(tok,sizeof(tok),"raid");if(i>=10){appendChar(tok,sizeof(tok),(char)('0'+i/10));}appendChar(tok,sizeof(tok),(char)('0'+i%10));u=resolveUnit(tok);if(u&&objectGuid(u,&g)&&g==guid)return "RAID";}
    return "UNKNOWN";
}


// GroundProbe.TotemSnapshot runtime fields. These are WoW 1.12.1 build 5875
// update-field indices (DWORD slots), matching the previously live-tested B3.4
// implementation: SUMMONEDBY=0x0C, CREATEDBY=0x0E, AURA[0]=0x2F,
// CREATED_BY_SPELL=0x92. No hook or background watcher is installed.
constexpr unsigned long UNIT_FIELD_SUMMONEDBY_INDEX = 0x0CUL;
constexpr unsigned long UNIT_FIELD_CREATEDBY_INDEX = 0x0EUL;
constexpr unsigned long UNIT_FIELD_AURA_FIRST_INDEX = 0x2FUL;
constexpr unsigned long UNIT_FIELD_AURA_COUNT = 48UL;
constexpr unsigned long UNIT_FIELD_CREATED_BY_SPELL_INDEX = 0x92UL;
constexpr int CREATURE_TYPE_TOTEM = 11;

static bool descriptorPtr(void* obj,unsigned char** out){
    if(!obj||!out||!readable((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR,4))return false;
    unsigned char* d=*(unsigned char**)((unsigned char*)obj+WoW112::OFF_CGOBJECT_DESCRIPTOR);
    if(!d)return false; *out=d; return true;
}
static bool descriptorU32At(unsigned char* d,unsigned long fieldIndex,unsigned long* out){
    if(!d||!out)return false; unsigned long off=fieldIndex*4UL;
    if(!readable(d+off,4))return false; *out=*(unsigned long*)(d+off); return true;
}
static bool descriptorU64At(unsigned char* d,unsigned long fieldIndex,unsigned long long* out){
    if(!d||!out)return false; unsigned long off=fieldIndex*4UL;
    if(!readable(d+off,8))return false; *out=*(unsigned long long*)(d+off); return true;
}

struct GroupGuidCache {
    unsigned long long self;
    unsigned long long party[4];
    unsigned long long raid[40];
};
static void readUnitGuidToken(const char* token,unsigned long long* out){
    if(!out)return; *out=0; void* u=resolveUnit(token); if(u)objectGuid(u,out);
}
static void buildGroupGuidCache(GroupGuidCache* c){
    if(!c)return; memset(c,0,sizeof(*c)); readUnitGuidToken("player",&c->self); char tok[8]={0};
    for(int i=1;i<=4;++i){cpy(tok,sizeof(tok),"party");appendChar(tok,sizeof(tok),(char)('0'+i));readUnitGuidToken(tok,&c->party[i-1]);}
    for(int i=1;i<=40;++i){cpy(tok,sizeof(tok),"raid");if(i>=10)appendChar(tok,sizeof(tok),(char)('0'+i/10));appendChar(tok,sizeof(tok),(char)('0'+i%10));readUnitGuidToken(tok,&c->raid[i-1]);}
}
static const char* totemScopeForOwner(unsigned long long guid,const GroupGuidCache& c){
    if(!guid)return "UNKNOWN"; if(c.self&&guid==c.self)return "SELF";
    for(int i=0;i<4;++i)if(c.party[i]&&guid==c.party[i])return "PARTY";
    for(int i=0;i<40;++i)if(c.raid[i]&&guid==c.raid[i])return "RAID";
    return "OTHER";
}

enum TotemMode { TOTEM_SELF,TOTEM_PARTY,TOTEM_PARTY_ONLY,TOTEM_RAID,TOTEM_ALL,TOTEM_OFF };
static TotemMode parseTotemMode(const char* raw){
    if(!raw||!*raw||ieq(raw,"SELF"))return TOTEM_SELF; if(ieq(raw,"PARTY"))return TOTEM_PARTY;
    if(ieq(raw,"PARTY_ONLY"))return TOTEM_PARTY_ONLY; if(ieq(raw,"RAID"))return TOTEM_RAID;
    if(ieq(raw,"ALL"))return TOTEM_ALL; if(ieq(raw,"OFF"))return TOTEM_OFF;
    return TOTEM_SELF; // invalid mode fails closed
}
static const char* totemModeName(TotemMode mode){
    if(mode==TOTEM_PARTY)return "PARTY"; if(mode==TOTEM_PARTY_ONLY)return "PARTY_ONLY"; if(mode==TOTEM_RAID)return "RAID";
    if(mode==TOTEM_ALL)return "ALL"; if(mode==TOTEM_OFF)return "OFF"; return "SELF";
}
static bool totemModeAllows(TotemMode mode,const char* scope){
    if(mode==TOTEM_OFF)return false; if(mode==TOTEM_ALL)return true; if(mode==TOTEM_SELF)return ieq(scope,"SELF");
    if(mode==TOTEM_PARTY)return ieq(scope,"SELF")||ieq(scope,"PARTY"); if(mode==TOTEM_PARTY_ONLY)return ieq(scope,"PARTY");
    if(mode==TOTEM_RAID)return ieq(scope,"SELF")||ieq(scope,"PARTY")||ieq(scope,"RAID"); return false;
}
static unsigned long long totemOwnerGuid(unsigned char* d){
    unsigned long long created=0,summoned=0; descriptorU64At(d,UNIT_FIELD_CREATEDBY_INDEX,&created); descriptorU64At(d,UNIT_FIELD_SUMMONEDBY_INDEX,&summoned); return created?created:summoned;
}
static void appendTotemAuraCsv(char* out,unsigned long cap,unsigned char* d){
    unsigned long seen[UNIT_FIELD_AURA_COUNT]={0}; unsigned seenCount=0;
    for(unsigned i=0;i<UNIT_FIELD_AURA_COUNT;++i){unsigned long spell=0;if(!descriptorU32At(d,UNIT_FIELD_AURA_FIRST_INDEX+i,&spell)||!spell)continue;
        bool duplicate=false;for(unsigned j=0;j<seenCount;++j)if(seen[j]==spell){duplicate=true;break;}if(duplicate)continue;
        if(seenCount<UNIT_FIELD_AURA_COUNT)seen[seenCount++]=spell; if(out[0])appendChar(out,cap,','); appendUInt(out,cap,spell);
    }
}

static void appendCallableEntrySummary(char* out,unsigned long cap,const char* name,const EntryInfo& e,const char* classification){
    if(out[0])cat(out,cap,";");cat(out,cap,name);cat(out,cap,":");cat(out,cap,classification);cat(out,cap,"@");cat(out,cap,e.owner);cat(out,cap,"/");cat(out,cap,e.kind);if(e.destination){cat(out,cap,"->");char h[16]={0};hex32(h,sizeof(h),e.destination);cat(out,cap,h);}
}
static const char* classifyCallableEntry(const EntryInfo& e,bool* warning){
    if(!e.jmpPatched)return "SAFE";
    if(!e.destination||!executable(e.destination))return "CONFLICT";
    if(ieq(e.owner,"UNKNOWN_ADDRESS")||ieq(e.owner,"UNKNOWN_MODULE")){if(warning)*warning=true;return "WARNING";}
    return "CHAINABLE";
}
static bool callableEntriesPreflight(const unsigned long* addrs,const char** names,unsigned count,char* observe,unsigned long cap,const char** code){
    bool anyPatched=false,anyWarning=false;observe[0]=0;
    for(unsigned i=0;i<count;++i){
        if(!executable(addrs[i])){EntryInfo x={false,false,0,{0},{0},{0}};cpy(x.owner,sizeof(x.owner),"WoW.exe");cpy(x.kind,sizeof(x.kind),"NOT_EXECUTABLE");appendCallableEntrySummary(observe,cap,names[i],x,"CONFLICT");*code="API_NOT_FOUND";return false;}
        EntryInfo e=inspectEntry(addrs[i]);bool warning=false;const char* c=classifyCallableEntry(e,&warning);appendCallableEntrySummary(observe,cap,names[i],e,c);if(eq(c,"CONFLICT")){*code="HOOK_CONFLICT";return false;}if(e.jmpPatched)anyPatched=true;if(warning)anyWarning=true;
    }
    *code=anyWarning?"WARNING_EXISTING_HOOK":(anyPatched?"OK_CHAINED":"OK");return true;
}
static bool facingPreflight(const char** code){
    BuildInfo b=detectBuild();if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}const unsigned long a[1]={WoW112::RESOLVE_UNIT_TOKEN};const char* n[1]={"ResolveUnit"};char tmp[512]={0};bool ok=callableEntriesPreflight(a,n,1,tmp,sizeof(tmp),code);cpy(g_facingResolveObserve,sizeof(g_facingResolveObserve),tmp);return ok;
}
static bool speedPreflight(const char** code){
    BuildInfo b=detectBuild();if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}const unsigned long a[2]={WoW112::RESOLVE_UNIT_TOKEN,WoW112::MOVEMENT_GET_EFFECTIVE_SPEED};const char* n[2]={"ResolveUnit","EffectiveSpeed"};char tmp[768]={0};bool ok=callableEntriesPreflight(a,n,2,tmp,sizeof(tmp),code);cpy(g_speedResolveObserve,sizeof(g_speedResolveObserve),tmp);EntryInfo e=inspectEntry(WoW112::MOVEMENT_GET_EFFECTIVE_SPEED);formatEntry(g_speedEffectiveObserve,sizeof(g_speedEffectiveObserve),e);return ok;
}
static bool groundProbePreflight(const char** code){
    BuildInfo b=detectBuild();if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}const unsigned long a[2]={WoW112::RESOLVE_UNIT_TOKEN,WoW112::UNIT_GET_CREATURE_TYPE};const char* n[2]={"ResolveUnit","CreatureType"};return callableEntriesPreflight(a,n,2,g_groundProbeObserve,sizeof(g_groundProbeObserve),code);
}
static bool relationPreflight(const char** code){
    BuildInfo b=detectBuild();if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}const unsigned long a[3]={WoW112::RESOLVE_UNIT_TOKEN,WoW112::UNIT_REACTION,WoW112::CAN_ATTACK};const char* n[3]={"ResolveUnit","Reaction","CanAttack"};return callableEntriesPreflight(a,n,3,g_relationObserve,sizeof(g_relationObserve),code);
}

static void appendDbcEntrySummary(char* out,unsigned long cap,const char* name,const EntryInfo& e,const char* classification){
    if(out[0])cat(out,cap,";"); cat(out,cap,name); cat(out,cap,":"); cat(out,cap,classification); cat(out,cap,"@"); cat(out,cap,e.owner); cat(out,cap,"/"); cat(out,cap,e.kind);
    if(e.destination){ cat(out,cap,"->"); char h[16]={0}; hex32(h,sizeof(h),e.destination); cat(out,cap,h); }
}
static bool dbcPreflight(const char** code){
    BuildInfo b=detectBuild(); if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}
    const unsigned long addrs[6]={0x00648DD0UL,0x006477C0UL,0x00648460UL,0x006487F0UL,0x00648730UL,0x00648EF0UL};
    const char* names[6]={"OpenArchive","OpenFile","ReadFile","GetFileSize","CloseFile","CloseArchive"};
    bool anyPatched=false, anyWarning=false;
    g_dbcObserve[0]=0;
    for(unsigned i=0;i<6;++i){
        if(!executable(addrs[i])){
            EntryInfo empty={false,false,0,{0},{0},{0}}; cpy(empty.owner,sizeof(empty.owner),"WoW.exe"); cpy(empty.kind,sizeof(empty.kind),"NOT_EXECUTABLE");
            appendDbcEntrySummary(g_dbcObserve,sizeof(g_dbcObserve),names[i],empty,"CONFLICT"); *code="API_NOT_FOUND"; return false;
        }
        EntryInfo e=inspectEntry(addrs[i]);
        if(!e.jmpPatched){ appendDbcEntrySummary(g_dbcObserve,sizeof(g_dbcObserve),names[i],e,"SAFE"); continue; }
        anyPatched=true;
        // An existing entry detour is not itself a conflict.  TaiYangShenDian does
        // not install an SFile hook; it calls the public entry and therefore naturally
        // traverses the existing chain.  Refuse only a broken/non-executable target.
        if(!e.destination || !executable(e.destination)){
            appendDbcEntrySummary(g_dbcObserve,sizeof(g_dbcObserve),names[i],e,"CONFLICT"); *code="HOOK_CONFLICT"; return false;
        }
        if(ieq(e.owner,"UNKNOWN_ADDRESS")||ieq(e.owner,"UNKNOWN_MODULE")){
            appendDbcEntrySummary(g_dbcObserve,sizeof(g_dbcObserve),names[i],e,"WARNING"); anyWarning=true;
        } else {
            appendDbcEntrySummary(g_dbcObserve,sizeof(g_dbcObserve),names[i],e,"CHAINABLE");
        }
    }
    *code=anyWarning?"WARNING_EXISTING_HOOK":(anyPatched?"OK_CHAINED":"OK"); return true;
}

static void appendVisualEntrySummary(char* out,unsigned long cap,const char* name,const EntryInfo& e,const char* classification){
    if(out[0])cat(out,cap,";"); cat(out,cap,name); cat(out,cap,":"); cat(out,cap,classification); cat(out,cap,"@"); cat(out,cap,e.owner); cat(out,cap,"/"); cat(out,cap,e.kind);
    if(e.destination){ cat(out,cap,"->"); char h[16]={0}; hex32(h,sizeof(h),e.destination); cat(out,cap,h); }
}
static bool visualPreflight(const char** code){
    BuildInfo b=detectBuild(); if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}
    if(!readable((const void*)TysVisual::WORLD_M2_CONTEXT_PTR,4)){*code="API_NOT_FOUND";return false;}
    const unsigned long addrs[10]={TysVisual::CREATE_MODEL,TysVisual::RELEASE_MODEL,TysVisual::ENSURE_RENDER_READY,TysVisual::SET_WORLD_MATRIX,TysVisual::ATTACH_RENDER_LIST,TysVisual::SET_ACTIVE_TIMESTAMP,TysVisual::SET_ALPHA,TysVisual::SET_COLOR,TysVisual::SET_SEQUENCE,TysVisual::SCENE_END};
    const char* names[10]={"Create","Release","Ready","WorldMatrix","RenderList","ActiveTime","Alpha","Color","Sequence","SceneEnd"};
    bool anyPatched=false,anyWarning=false; g_visualObserve[0]=0;
    for(unsigned i=0;i<10;++i){
        if(!executable(addrs[i])){ EntryInfo x={false,false,0,{0},{0},{0}};cpy(x.owner,sizeof(x.owner),"WoW.exe");cpy(x.kind,sizeof(x.kind),"NOT_EXECUTABLE");appendVisualEntrySummary(g_visualObserve,sizeof(g_visualObserve),names[i],x,"CONFLICT");*code="API_NOT_FOUND";return false;}
        EntryInfo e=inspectEntry(addrs[i]);
        if(!e.jmpPatched){appendVisualEntrySummary(g_visualObserve,sizeof(g_visualObserve),names[i],e,"SAFE");continue;}
        anyPatched=true;
        if(!e.destination||!executable(e.destination)){appendVisualEntrySummary(g_visualObserve,sizeof(g_visualObserve),names[i],e,"CONFLICT");*code="HOOK_CONFLICT";return false;}
        if(ieq(e.owner,"UNKNOWN_ADDRESS")||ieq(e.owner,"UNKNOWN_MODULE")){appendVisualEntrySummary(g_visualObserve,sizeof(g_visualObserve),names[i],e,"WARNING");anyWarning=true;}
        else appendVisualEntrySummary(g_visualObserve,sizeof(g_visualObserve),names[i],e,"CHAINABLE");
    }
    *code=anyWarning?"WARNING_EXISTING_HOOK":(anyPatched?"OK_CHAINED":"OK");return true;
}

static bool dreamAvatarPreflight(const char** code){
    BuildInfo b=detectBuild(); if(!b.ok){*code="UNSUPPORTED_BUILD";return false;}
    // Item.dbc / SFile is optional: only NPC display->item reverse lookup uses it.
    // Character, weapons, glows, mounts and sync remain usable without that optional lookup.
    const unsigned long a[3]={0x00515970UL,0x0060ABE0UL,0x005FFA50UL};
    const char* n[3]={"UnitGUID","UpdateDisplayInfo","RefreshMount"};
    char tmp[768]={0}; bool ok=callableEntriesPreflight(a,n,3,tmp,sizeof(tmp),code);
    if(ok)*code="OK"; return ok;
}

static bool isMoonMarkerGuardedCommand(const char* cmd){
    if(!cmd)return false;
    if(eq(cmd,"MMAuth"))return true;
    if(eq(cmd,"MoonMarker.Place")||eq(cmd,"MoonMarker.Remote")||eq(cmd,"MoonMarker.Clear"))return true;
    if(eq(cmd,"MoonMarker.Targeting.Begin")||eq(cmd,"MoonMarker.Targeting.Update")
        ||eq(cmd,"MoonMarker.Targeting.Cancel")||eq(cmd,"MoonMarker.Targeting.Commit"))return true;
    if(eq(cmd,"MoonMarker.Main.Remote"))return true;
    if(startsI(cmd,"MoonMarker.Advanced."))return true;
    // DreamAvatar/DreamWeapon aliases have their own frozen TYS migration/auth
    // boundary and are intentionally not folded back under the MoonMarker guard.
    return false;
}

static int pushMoonMarkerGuardFailure(Lua50::State L){
    Lua50::PushBool(L,false);
    Lua50::PushString(L,TysMoonRuntimeGuard::statusCode());
    Lua50::PushString(L,TysMoonRuntimeGuard::userMessage());
    return 3;
}

static int doMoonMarkerRuntimeStatus(Lua50::State L){
    // MM2 fidelity: expose the original MoonMarker Runtime Guard state rather
    // than conflating it with TaiYangShenDian's separate visual feature gate.
    TysMoonRuntimeGuard::initialize();
    Lua50::PushBool(L,TysMoonRuntimeGuard::enabled());
    Lua50::PushString(L,TysMoonRuntimeGuard::statusCode());
    Lua50::PushString(L,TysMoonRuntimeGuard::userMessage());
    Lua50::PushString(L,TysMoonRuntimeGuard::detail());
    const char* fp=TysMoonRuntimeGuard::fingerprint();
    if(fp&&fp[0])Lua50::PushString(L,fp);else Lua50::PushNil(L);
    return 5;
}

static void __fastcall scenePresentHook(void* self,void*,int unknown){
    // Canonical B1R5R8 behavior: with the software cursor enabled WoW 1.12 can
    // execute sceneEnd twice for one displayed frame. gxDevice_scenePresent is
    // immediately before the presenting sceneEnd, so arm a one-shot gate here.
    if(g_scenePresentNext)g_scenePresentNext(self,unknown);
    InterlockedExchange(&g_sceneIsPresenting,1);
}

static void __fastcall visualSceneEndHook(unsigned long device,void*){
    // Preserve the existing Visual cadence, but only publish a Profiler frame
    // boundary for the sceneEnd that follows gxDevice_scenePresent. This restores
    // the known-good B1R5R8 FPS/frame semantics without changing the Profiler core.
    const bool presenting=InterlockedExchange(&g_sceneIsPresenting,0)!=0;
    if(presenting&&TysProfiler::needsFrameBoundary())TysProfiler::onFrameBoundary();
    if(InterlockedCompareExchange(&g_visualEnabled,0,0)!=0){TysMoonAdvanced::observeWorldContext(TysVisual::worldContextToken());TysVisual::updateNativeFrame(device);}
    if(g_visualSceneNext)g_visualSceneNext(device);
}

static void disableScenePresentHook(){
    InterlockedExchange(&g_sceneIsPresenting,0);
    if(!g_scenePresentCreated||!g_scenePresentTarget){g_scenePresentEnabled=false;return;}
    MH_STATUS st=MH_DisableHook((LPVOID)g_scenePresentTarget);
    if(st==MH_OK||st==MH_ERROR_DISABLED)g_scenePresentEnabled=false;
    else cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"MH_DISABLE_PRESENT_FAILED");
}

static bool enableScenePresentHook(){
    if(g_scenePresentCreated){
        MH_STATUS en=MH_EnableHook((LPVOID)g_scenePresentTarget);
        if(en==MH_OK||en==MH_ERROR_ENABLED){g_scenePresentEnabled=true;InterlockedExchange(&g_sceneIsPresenting,0);return true;}
        cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"MH_REENABLE_PRESENT_FAILED");return false;
    }
    EntryInfo present=inspectEntry(GXDEVICE_SCENE_PRESENT);
    unsigned long target=GXDEVICE_SCENE_PRESENT;
    if(present.jmpPatched){
        if(!present.destination||!executable(present.destination)){cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"PRESENT_CONFLICT");return false;}
        target=present.destination;
        if(ieq(present.owner,"UNKNOWN_ADDRESS")||ieq(present.owner,"UNKNOWN_MODULE"))cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"CHAIN_WARNING_EXISTING_PRESENT");
        else cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"CHAIN_EXISTING_PRESENT");
    }else cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"HOOK_WOW_PRESENT");
    if(!executable(target)){cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"PRESENT_TARGET_NOT_EXECUTABLE");return false;}
    MH_STATUS init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"MH_INIT_PRESENT_FAILED");return false;}
    g_scenePresentTarget=target;
    MH_STATUS cr=MH_CreateHook((LPVOID)target,(LPVOID)&scenePresentHook,(LPVOID*)&g_scenePresentNext);
    if(cr!=MH_OK){cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"MH_CREATE_PRESENT_FAILED");g_scenePresentTarget=0;return false;}
    g_scenePresentCreated=true;
    MH_STATUS en=MH_EnableHook((LPVOID)target);
    if(en==MH_OK||en==MH_ERROR_ENABLED){g_scenePresentEnabled=true;InterlockedExchange(&g_sceneIsPresenting,0);return true;}
    cpy(g_scenePresentMode,sizeof(g_scenePresentMode),"MH_ENABLE_PRESENT_FAILED");return false;
}

static bool enableVisualSceneHook(){
    // Profiler frame accounting requires the presenting-sceneEnd gate from the
    // original B1R5R8 implementation. Keep both hooks under the same lifetime.
    if(!enableScenePresentHook())return false;
    if(g_visualSceneCreated){
        MH_STATUS en=MH_EnableHook((LPVOID)g_visualSceneTarget);
        if(en==MH_OK||en==MH_ERROR_ENABLED){g_visualSceneEnabled=true;return true;}
        cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"MH_REENABLE_FAILED");disableScenePresentHook();return false;
    }
    EntryInfo scene=inspectEntry(TysVisual::SCENE_END);
    unsigned long target=TysVisual::SCENE_END;
    if(scene.jmpPatched){
        if(!scene.destination||!executable(scene.destination)){cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"SCENEEND_CONFLICT");disableScenePresentHook();return false;}
        target=scene.destination;
        if(ieq(scene.owner,"UNKNOWN_ADDRESS")||ieq(scene.owner,"UNKNOWN_MODULE"))cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"CHAIN_WARNING_EXISTING_SCENEEND");
        else cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"CHAIN_EXISTING_SCENEEND");
    }else cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"HOOK_WOW_SCENEEND");
    if(!executable(target)){cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"SCENEEND_TARGET_NOT_EXECUTABLE");disableScenePresentHook();return false;}
    MH_STATUS init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"MH_INIT_FAILED");disableScenePresentHook();return false;}
    g_visualSceneTarget=target;
    MH_STATUS cr=MH_CreateHook((LPVOID)target,(LPVOID)&visualSceneEndHook,(LPVOID*)&g_visualSceneNext);
    if(cr!=MH_OK){cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"MH_CREATE_SCENEEND_FAILED");g_visualSceneTarget=0;disableScenePresentHook();return false;}
    g_visualSceneCreated=true;
    MH_STATUS en=MH_EnableHook((LPVOID)target);
    if(en==MH_OK||en==MH_ERROR_ENABLED){g_visualSceneEnabled=true;return true;}
    cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"MH_ENABLE_SCENEEND_FAILED");disableScenePresentHook();return false;
}

static void disableVisualSceneHook(){
    // Shared hook stays alive while either Visual or Profiler still consumes the
    // frame boundary.  This is the only ownership rule; no idle polling remains.
    if(InterlockedCompareExchange(&g_visualEnabled,0,0)!=0||TysProfiler::needsFrameBoundary())return;
    if(!g_visualSceneCreated||!g_visualSceneTarget){g_visualSceneEnabled=false;disableScenePresentHook();return;}
    MH_STATUS st=MH_DisableHook((LPVOID)g_visualSceneTarget);
    if(st==MH_OK||st==MH_ERROR_DISABLED){g_visualSceneEnabled=false;disableScenePresentHook();}
    else cpy(g_visualSceneMode,sizeof(g_visualSceneMode),"MH_DISABLE_SCENEEND_FAILED");
}

static bool ensureVisualRuntime(const char** outCode){
    if(g_visualSceneEnabled){if(outCode)*outCode="OK";return true;}
    const char* code="";
    if(!visualPreflight(&code)){cpy(g_visualStatus,sizeof(g_visualStatus),code);if(outCode)*outCode=code;return false;}
    if(!enableVisualSceneHook()){cpy(g_visualStatus,sizeof(g_visualStatus),g_visualSceneMode);if(outCode)*outCode=g_visualSceneMode;return false;}
    cpy(g_visualStatus,sizeof(g_visualStatus),"READY_NATIVE_SCENEEND_PROCESS_LIFETIME");
    if(outCode)*outCode="OK";
    return true;
}

static bool ensureDbcRuntime(const char** outCode){
    if(InterlockedCompareExchange(&g_dbcReady,0,0)!=0){if(outCode)*outCode="OK";return true;}
    const char* code="";
    if(!dbcPreflight(&code)){cpy(g_dbcStatus,sizeof(g_dbcStatus),code);if(outCode)*outCode=code;return false;}
    InterlockedExchange(&g_dbcReady,1);
    cpy(g_dbcStatus,sizeof(g_dbcStatus),"READY_LAZY_DATA");
    if(outCode)*outCode="OK";
    return true;
}

static bool ensureDreamAvatarRuntime(const char** outCode){
    if(InterlockedCompareExchange(&g_dreamAvatarReady,0,0)!=0){if(outCode)*outCode="OK";return true;}
    const char* code="";
    if(!dreamAvatarPreflight(&code)){cpy(g_dreamAvatarStatus,sizeof(g_dreamAvatarStatus),code);if(outCode)*outCode=code;return false;}
    InterlockedExchange(&g_dreamAvatarReady,1);
    cpy(g_dreamAvatarStatus,sizeof(g_dreamAvatarStatus),"READY_PROCESS_LIFETIME");
    if(outCode)*outCode="OK";
    return true;
}

static bool ensureDreamWeaponRuntime(const char** outCode){
    if(InterlockedCompareExchange(&g_dreamWeaponReady,0,0)!=0){if(outCode)*outCode="OK";return true;}
    const char* code="";
    if(!dreamAvatarPreflight(&code)){cpy(g_dreamWeaponStatus,sizeof(g_dreamWeaponStatus),code);if(outCode)*outCode=code;return false;}
    InterlockedExchange(&g_dreamWeaponReady,1);
    cpy(g_dreamWeaponStatus,sizeof(g_dreamWeaponStatus),"READY_PROCESS_LIFETIME");
    if(outCode)*outCode="OK";
    return true;
}

static bool commandNeedsVisualRuntime(const char* cmd){
    if(!cmd)return false;
    if(startsI(cmd,"AutoRange.Visual"))return true;
    if(eq(cmd,"MoonMarker.Place")||eq(cmd,"MoonMarker.Remote")||eq(cmd,"MoonMarker.Targeting.Begin")||eq(cmd,"MoonMarker.Targeting.Update")||eq(cmd,"MoonMarker.Targeting.Commit"))return true;
    if(eq(cmd,"MoonMarker.Advanced.PreviewAt")||eq(cmd,"MoonMarker.Advanced.PreviewAtPlayer")||eq(cmd,"MoonMarker.Advanced.SetPreviewTransform")||eq(cmd,"MoonMarker.Advanced.Main.PlaceAt")||eq(cmd,"MoonMarker.Main.Remote"))return true;
    return false;
}

static void handleRuntimeWorldEvent(Lua50::State L,const char* ev){
    if(!ev)return;
    if(eq(ev,"PLAYER_LEAVING_WORLD")){
        if(InterlockedCompareExchange(&g_worldPhase,2,2)==2)return;
        InterlockedExchange(&g_worldPhase,2);
        TysProfiler::onWorldLeaving(L);
        TysMoonAdvanced::onWorldLeaving();
        TysAuraCaster::onWorldLeaving();
        TysSpellCast::onWorldLeaving();
        if(g_visualSceneCreated||TysVisual::activeCount()||TysVisual::moonActiveCount())TysVisual::onWorldLeaving();
        return;
    }
    if(eq(ev,"PLAYER_ENTERING_WORLD")){
        if(InterlockedCompareExchange(&g_worldPhase,1,1)==1)return;
        InterlockedExchange(&g_worldPhase,1);
        TysProfiler::onWorldEntering(L);
        if(g_visualSceneCreated||TysVisual::activeCount()||TysVisual::moonActiveCount())TysVisual::onWorldEntering();
    }
}

static int __fastcall runtimeLifecycleOnEvent(Lua50::State L){
    if(!L)return 0;
    const int top=Lua50::GetTop(L);
    Lua50::GetGlobal(L,"event");
    const char* ev=Lua50::IsString(L,-1)?Lua50::ToString(L,-1):0;
    handleRuntimeWorldEvent(L,ev);
    Lua50::SetTop(L,top);
    return 0;
}

static bool callFrameMethodString(Lua50::State L,int frameIndex,const char* method,const char* arg){
    const int top=Lua50::GetTop(L);
    Lua50::PushString(L,method);Lua50::GetTable(L,frameIndex);
    if(Lua50::Type(L,-1)!=WoW112::LUA_TFUNCTION){Lua50::SetTop(L,top);return false;}
    Lua50::PushValue(L,frameIndex);Lua50::PushString(L,arg);
    const int rc=Lua50::PCall(L,2,0,0);Lua50::SetTop(L,top);return rc==0;
}

static bool installRuntimeLifecycleFrame(Lua50::State L){
    if(!L)return false;
    if(g_runtimeLifecycleState==L&&eq(g_runtimeLifecycleStatus,"INSTALLED"))return true;
    const int top=Lua50::GetTop(L);
    Lua50::GetGlobal(L,"CreateFrame");
    if(Lua50::Type(L,-1)!=WoW112::LUA_TFUNCTION){Lua50::SetTop(L,top);cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"CREATEFRAME_MISSING");return false;}
    Lua50::PushString(L,"Frame");Lua50::PushString(L,"TaiYangShenDianRuntimeLifecycleFrame");
    if(Lua50::PCall(L,2,1,0)!=0){Lua50::SetTop(L,top);cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"CREATEFRAME_FAILED");return false;}
    const int frameIndex=top+1;
    if(!callFrameMethodString(L,frameIndex,"RegisterEvent","PLAYER_LEAVING_WORLD")||!callFrameMethodString(L,frameIndex,"RegisterEvent","PLAYER_ENTERING_WORLD")){
        Lua50::SetTop(L,top);cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"REGISTER_EVENT_FAILED");return false;
    }
    const int methodTop=Lua50::GetTop(L);
    Lua50::PushString(L,"SetScript");Lua50::GetTable(L,frameIndex);
    if(Lua50::Type(L,-1)!=WoW112::LUA_TFUNCTION){Lua50::SetTop(L,top);cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"SETSCRIPT_MISSING");return false;}
    Lua50::PushValue(L,frameIndex);Lua50::PushString(L,"OnEvent");Lua50::PushCClosure(L,&runtimeLifecycleOnEvent,0);
    const int rc=Lua50::PCall(L,3,0,0);
    Lua50::SetTop(L,methodTop);
    Lua50::SetTop(L,top);
    if(rc!=0){cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"SETSCRIPT_FAILED");return false;}
    g_runtimeLifecycleState=L;
    InterlockedExchange(&g_worldPhase,0);
    cpy(g_runtimeLifecycleStatus,sizeof(g_runtimeLifecycleStatus),"INSTALLED");
    return true;
}

static int doProfilerDispatch(Lua50::State L,const char* cmd){
    const char* sub=(Lua50::GetTop(L)>=2&&Lua50::IsString(L,2))?Lua50::ToString(L,2):"status";
    // Start must have a real native frame boundary before it reports success.
    // Profiler does not need the full Visual/M2 ABI, but it still refuses an
    // unsupported WoW build before touching the shared sceneEnd address.
    if(ieq(sub,"start")&&!g_visualSceneEnabled){
        BuildInfo b=detectBuild();
        if(!b.ok){Lua50::PushBool(L,false);Lua50::PushString(L,"UNSUPPORTED_BUILD");return 2;}
        if(!enableVisualSceneHook()){
            Lua50::PushBool(L,false);Lua50::PushString(L,"HOOK_INSTALL_FAILED");return 2;
        }
    }
    int r=TysProfiler::dispatch(L,cmd);
    // stop/reset/hardreset may release the last consumer.
    disableVisualSceneHook();
    return r;
}

static int pushError(Lua50::State L,const char* code){ Lua50::PushNil(L); Lua50::PushString(L,code); return 2; }
static void pushBoolCode(Lua50::State L,bool ok,const char* code){ Lua50::PushBool(L,ok); Lua50::PushString(L,code); }
static double round4(float v){ double x=(double)v*10000.0; long q=(long)(x+(x>=0.0?0.5:-0.5)); return (double)q/10000.0; }
static void buildConflictReport();

static int doFacing(Lua50::State L){
    if(InterlockedCompareExchange(&g_facingEnabled,0,0)==0)return pushError(L,"FEATURE_DISABLED");
    const char* token=(Lua50::GetTop(L)>=2&&Lua50::IsString(L,2))?Lua50::ToString(L,2):"player";
    if(!validUnitToken(token))return pushError(L,"BAD_ARGUMENT");
    unsigned char* unit=(unsigned char*)resolveUnit(token); if(!unit)return pushError(L,"UNIT_NOT_FOUND");
    if(!readable(unit+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR,4))return pushError(L,"DATA_UNAVAILABLE");
    unsigned char* movement=*(unsigned char**)(unit+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR); if(!movement)return pushError(L,"DATA_UNAVAILABLE");
    if(!readable(movement+WoW112::OFF_MOVEMENT_FACING,4))return pushError(L,"DATA_UNAVAILABLE");
    float f=*(float*)(movement+WoW112::OFF_MOVEMENT_FACING);
    if(!(f==f)||f < -100.0f||f > 100.0f)return pushError(L,"DATA_UNAVAILABLE");
    Lua50::PushNumber(L,(double)f); return 1;
}

static int doSpeed(Lua50::State L){
    if(InterlockedCompareExchange(&g_speedEnabled,0,0)==0)return pushError(L,"FEATURE_DISABLED");
    const char* token=(Lua50::GetTop(L)>=2&&Lua50::IsString(L,2))?Lua50::ToString(L,2):"player";
    if(!validUnitToken(token))return pushError(L,"BAD_ARGUMENT");
    unsigned char* unit=(unsigned char*)resolveUnit(token); if(!unit)return pushError(L,"UNIT_NOT_FOUND");
    if(!readable(unit+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR,4))return pushError(L,"DATA_UNAVAILABLE");
    unsigned char* movement=*(unsigned char**)(unit+WoW112::OFF_UNIT_MOVEMENT_INFO_PTR); if(!movement)return pushError(L,"DATA_UNAVAILABLE");
    if(!readable(movement+WoW112::OFF_MOVEMENT_RUN_SPEED,4)||!readable(movement+WoW112::OFF_MOVEMENT_SWIM_SPEED,4))return pushError(L,"DATA_UNAVAILABLE");
    if(!executable(WoW112::MOVEMENT_GET_EFFECTIVE_SPEED))return pushError(L,"API_NOT_FOUND");
    float current=((EffectiveSpeedFn)WoW112::MOVEMENT_GET_EFFECTIVE_SPEED)(movement,0);
    float run=*(float*)(movement+WoW112::OFF_MOVEMENT_RUN_SPEED);
    float swim=*(float*)(movement+WoW112::OFF_MOVEMENT_SWIM_SPEED);
    if(!(current==current)||!(run==run)||!(swim==swim)||current < -0.01f||run < -0.01f||swim < -0.01f||current > 1000.0f||run > 1000.0f||swim > 1000.0f)return pushError(L,"DATA_UNAVAILABLE");
    Lua50::PushNumber(L,round4(current)); Lua50::PushNumber(L,round4(run)); Lua50::PushNumber(L,round4(swim)); return 3;
}

static int requireFeature(Lua50::State L,volatile LONG* flag){ if(InterlockedCompareExchange(flag,0,0)==0)return pushError(L,"FEATURE_DISABLED"); return 0; }
static void tableSetString(Lua50::State L,const char* key,const char* value){Lua50::PushString(L,key);Lua50::PushString(L,value);Lua50::SetTable(L,-3);}
static void tableSetBool(Lua50::State L,const char* key,bool value){Lua50::PushString(L,key);Lua50::PushBool(L,value);Lua50::SetTable(L,-3);}
static void tableSetNumber(Lua50::State L,const char* key,double value){Lua50::PushString(L,key);Lua50::PushNumber(L,value);Lua50::SetTable(L,-3);}

static int doUnitGuid(Lua50::State L){
    if(!detectBuild().ok)return pushError(L,"UNSUPPORTED_BUILD");
    if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");const char* token=Lua50::ToString(L,2);
    if(!validUnitToken(token))return pushError(L,"BAD_ARGUMENT");void* o=resolveUnit(token);unsigned long long g=0;if(!o||!objectGuid(o,&g))return pushError(L,"UNIT_NOT_FOUND");
    char h[17]={0};hex64(h,sizeof(h),g);Lua50::PushString(L,h);Lua50::PushString(L,"OK");return 2;
}

static int doUnitInSight(Lua50::State L){
    if(!detectBuild().ok)return pushError(L,"UNSUPPORTED_BUILD");if(Lua50::GetTop(L)<3||!Lua50::IsString(L,2)||!Lua50::IsString(L,3))return pushError(L,"BAD_ARGUMENT");
    void* a=0;void* b=0;unsigned long long ga=0,gb=0;const char* av=Lua50::ToString(L,2);const char* bv=Lua50::ToString(L,3);
    if(!resolveUnitOrGuid(av,&a,&ga)||!resolveUnitOrGuid(bv,&b,&gb))return pushError(L,"UNIT_NOT_VISIBLE");
    unsigned long ta=0,tb=0;if(!objectType(a,&ta)||!objectType(b,&tb)||(ta!=WoW112::TYPE_UNIT&&ta!=WoW112::TYPE_PLAYER)||(tb!=WoW112::TYPE_UNIT&&tb!=WoW112::TYPE_PLAYER))return pushError(L,"UNIT_NOT_SUPPORTED");
    const TysLosPairCache::Tick32 now=(TysLosPairCache::Tick32)GetTickCount();
    bool los=false;if(!TysLosPairCache::tryGet(ga,gb,now,TysLosPairCache::DEFAULT_TTL_MS,&los)){
        TysLosPairCache::noteRecompute();
        if(!unitLineOfSight(a,b,&los))return pushError(L,"LOS_UNAVAILABLE");
        TysLosPairCache::put(ga,gb,now,los);
    }
    Lua50::PushBool(L,los);Lua50::PushString(L,los?"OK":"BLOCKED");return 2;
}

static int doUnitStateByGuid(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;if(!detectBuild().ok)return pushError(L,"UNSUPPORTED_BUILD");
    if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");unsigned long long g=0;if(!parseGuid(Lua50::ToString(L,2),&g))return pushError(L,"GUID_INVALID");
    char h[17]={0};hex64(h,sizeof(h),g);Lua50::NewTable(L);tableSetString(L,"guid",h);void* o=findObjectByGuid(g);
    if(!o){tableSetBool(L,"visible",false);tableSetBool(L,"deadKnown",false);tableSetString(L,"code","GUID_NOT_VISIBLE");return 1;}
    unsigned long t=0;if(!objectType(o,&t)||(t!=WoW112::TYPE_UNIT&&t!=WoW112::TYPE_PLAYER)){tableSetBool(L,"visible",true);tableSetBool(L,"deadKnown",false);tableSetString(L,"code","GUID_NOT_UNIT");return 1;}
    tableSetBool(L,"visible",true);tableSetNumber(L,"objectType",(double)t);unsigned long entry=0;if(objectEntry(o,&entry))tableSetNumber(L,"entry",(double)entry);
    float p[3]={0,0,0};if(objectPosition(o,p)){tableSetNumber(L,"x",(double)p[0]);tableSetNumber(L,"y",(double)p[1]);tableSetNumber(L,"z",(double)p[2]);}
    unsigned char* d=0;unsigned long hp=0,maxHp=0,dyn=0;bool fields=descriptorPtr(o,&d)&&descriptorU32At(d,WoW112::UNIT_FIELD_HEALTH_INDEX,&hp)&&descriptorU32At(d,WoW112::UNIT_FIELD_MAXHEALTH_INDEX,&maxHp)&&descriptorU32At(d,WoW112::UNIT_DYNAMIC_FLAGS_INDEX,&dyn);
    if(fields){bool dead=(hp<1UL)||((dyn&WoW112::UNIT_DYNFLAG_DEAD)!=0);tableSetBool(L,"deadKnown",true);tableSetBool(L,"dead",dead);tableSetNumber(L,"health",(double)hp);tableSetNumber(L,"maxHealth",(double)maxHp);tableSetNumber(L,"dynamicFlags",(double)dyn);tableSetString(L,"code","OK");}
    else {tableSetBool(L,"deadKnown",false);tableSetString(L,"code","UNIT_FIELDS_UNAVAILABLE");}
    return 1;
}

static int doGroundStatus(Lua50::State L){ int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;BuildInfo b=detectBuild();Lua50::PushBool(L,b.ok);Lua50::PushString(L,b.ok?"OK":"UNSUPPORTED_BUILD");Lua50::PushString(L,b.ok?"GroundProbe one-shot query pack ready":"unsupported client");return 3; }
static int doPlayerPosition(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;g_recordScratch[0]=0;void* p=resolveUnit("player");if(!p){Lua50::PushString(L,"E|PLAYER_OBJECT_UNAVAILABLE|player object is not available");return 1;}float v[3]={0};if(!objectPosition(p,v)){Lua50::PushString(L,"E|PLAYER_POSITION_INVALID|player position is not readable");return 1;}cat(g_recordScratch,sizeof(g_recordScratch),"P|");appendFloat4(g_recordScratch,sizeof(g_recordScratch),v[0]);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendFloat4(g_recordScratch,sizeof(g_recordScratch),v[1]);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendFloat4(g_recordScratch,sizeof(g_recordScratch),v[2]);Lua50::PushString(L,g_recordScratch);return 1;
}
static int doUnitByGuid(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)){Lua50::PushString(L,"E|GUID_INVALID|caster GUID could not be parsed");return 1;}unsigned long long g=0;if(!parseGuid(Lua50::ToString(L,2),&g)){Lua50::PushString(L,"E|GUID_INVALID|caster GUID could not be parsed");return 1;}void* o=findObjectByGuid(g);if(!o){Lua50::PushString(L,"E|GUID_NOT_VISIBLE|");return 1;}unsigned long t=0;if(!objectType(o,&t)){Lua50::PushString(L,"E|UNIT_TYPE_UNREADABLE|object type is not readable");return 1;}if(t!=WoW112::TYPE_UNIT&&t!=WoW112::TYPE_PLAYER){Lua50::PushString(L,"E|GUID_NOT_UNIT|");return 1;}float player[3]={0};void* po=resolveUnit("player");if(!po||!objectPosition(po,player)){Lua50::PushString(L,"E|PLAYER_POSITION_INVALID|player position is not readable");return 1;}g_recordScratch[0]=0;appendUnitRecord(g_recordScratch,sizeof(g_recordScratch),o,player);if(!g_recordScratch[0])Lua50::PushString(L,"E|UNIT_POSITION_INVALID|unit position is not readable");else Lua50::PushString(L,g_recordScratch);return 1;
}
static int doDynamicByGuid(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)){Lua50::PushString(L,"E|GUID_INVALID|dynamic GUID could not be parsed");return 1;}unsigned long long g=0;if(!parseGuid(Lua50::ToString(L,2),&g)){Lua50::PushString(L,"E|GUID_INVALID|dynamic GUID could not be parsed");return 1;}void* o=findObjectByGuid(g);if(!o){Lua50::PushString(L,"E|GUID_NOT_VISIBLE|");return 1;}unsigned long t=0;if(!objectType(o,&t)){Lua50::PushString(L,"E|OBJECT_TYPE_UNREADABLE|dynamic object type is not readable");return 1;}if(t!=WoW112::TYPE_DYNAMICOBJECT){Lua50::PushString(L,"E|GUID_NOT_DYNAMIC|");return 1;}float player[3]={0};void* po=resolveUnit("player");if(!po||!objectPosition(po,player)){Lua50::PushString(L,"E|PLAYER_POSITION_INVALID|player position is not readable");return 1;}g_recordScratch[0]=0;appendDynamicRecord(g_recordScratch,sizeof(g_recordScratch),o,player);if(!g_recordScratch[0])Lua50::PushString(L,"E|DYNAMIC_FIELDS_INVALID|dynamic fields are not readable");else Lua50::PushString(L,g_recordScratch);return 1;
}
static int doScopeByGuid(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;unsigned long long g=0;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)||!parseGuid(Lua50::ToString(L,2),&g)){Lua50::PushString(L,"G|UNKNOWN|0000000000000000|GUID_INVALID");return 1;}char h[17]={0};hex64(h,sizeof(h),g);g_recordScratch[0]=0;cat(g_recordScratch,sizeof(g_recordScratch),"G|");cat(g_recordScratch,sizeof(g_recordScratch),scopeForGuid(g));appendChar(g_recordScratch,sizeof(g_recordScratch),'|');cat(g_recordScratch,sizeof(g_recordScratch),h);cat(g_recordScratch,sizeof(g_recordScratch),"|OK");Lua50::PushString(L,g_recordScratch);return 1;
}
static int doSnapshot(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;float range=120.0f;bool includeGame=true;if(Lua50::GetTop(L)>=2&&Lua50::IsNumber(L,2))range=(float)Lua50::ToNumber(L,2);if(Lua50::GetTop(L)>=3)includeGame=Lua50::ToBoolean(L,3);if(!(range==range)||range<=0.0f||range>999.0f)range=120.0f;
    float player[3]={0};void* po=resolveUnit("player");if(!po){Lua50::PushString(L,"E|PLAYER_OBJECT_UNAVAILABLE|player object is not available");return 1;}if(!objectPosition(po,player)){Lua50::PushString(L,"E|PLAYER_POSITION_INVALID|player position is not readable");return 1;}
    unsigned long mgr=0,cur=0;long base=0;if(!objectManager(&mgr,&cur,&base)){Lua50::PushString(L,"E|OBJECT_MANAGER_UNAVAILABLE|object manager is not available");return 1;}g_snapshotRows[0]=0;unsigned units=0,dyn=0,gos=0;float range2=range*range;
    for(unsigned n=0;cur&&!(cur&1)&&n<4096;++n){void* o=(void*)cur;unsigned long t=0;float p[3]={0};if(objectType(o,&t)){bool in=false;if(t==WoW112::TYPE_DYNAMICOBJECT){unsigned long long c=0;unsigned long sp=0;float r=0;if(dynamicFields(o,&c,&sp,&r,p)){float dx=p[0]-player[0],dy=p[1]-player[1];in=dx*dx+dy*dy<=range2;if(in){if(g_snapshotRows[0])appendChar(g_snapshotRows,sizeof(g_snapshotRows),'\n');appendDynamicRecord(g_snapshotRows,sizeof(g_snapshotRows),o,player);++dyn;}}}else if((t==WoW112::TYPE_UNIT||t==WoW112::TYPE_PLAYER)&&objectPosition(o,p)){float dx=p[0]-player[0],dy=p[1]-player[1];in=dx*dx+dy*dy<=range2;if(in){if(g_snapshotRows[0])appendChar(g_snapshotRows,sizeof(g_snapshotRows),'\n');appendUnitRecord(g_snapshotRows,sizeof(g_snapshotRows),o,player);++units;}}else if(includeGame&&t==WoW112::TYPE_GAMEOBJECT&&objectPosition(o,p)){float dx=p[0]-player[0],dy=p[1]-player[1];in=dx*dx+dy*dy<=range2;if(in){if(g_snapshotRows[0])appendChar(g_snapshotRows,sizeof(g_snapshotRows),'\n');appendGameObjectRecord(g_snapshotRows,sizeof(g_snapshotRows),o,player);++gos;}}}
        unsigned long nxt=nextObject(cur,base);if(nxt==cur)break;cur=nxt;
    }
    g_recordScratch[0]=0;cat(g_recordScratch,sizeof(g_recordScratch),"S|");appendUInt(g_recordScratch,sizeof(g_recordScratch),units);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendUInt(g_recordScratch,sizeof(g_recordScratch),dyn);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendUInt(g_recordScratch,sizeof(g_recordScratch),gos);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');cat(g_recordScratch,sizeof(g_recordScratch),includeGame?"ON":"OFF");if(g_snapshotRows[0]){appendChar(g_recordScratch,sizeof(g_recordScratch),'\n');cat(g_recordScratch,sizeof(g_recordScratch),g_snapshotRows);}Lua50::PushString(L,g_recordScratch);return 1;
}
static int doTotemSnapshot(Lua50::State L){
    int e=requireFeature(L,&g_groundProbeEnabled);if(e)return e;
    const char* rawMode=(Lua50::GetTop(L)>=2&&Lua50::IsString(L,2))?Lua50::ToString(L,2):"SELF";
    TotemMode mode=parseTotemMode(rawMode); float range=140.0f;
    if(Lua50::GetTop(L)>=3&&Lua50::IsNumber(L,3))range=(float)Lua50::ToNumber(L,3);
    if(!(range==range)||range<=0.0f||range>999.0f)range=140.0f;
    if(mode==TOTEM_OFF){Lua50::PushString(L,"S|0|0|OFF");return 1;}

    void* po=resolveUnit("player");if(!po){Lua50::PushString(L,"E|PLAYER_OBJECT_UNAVAILABLE|player object is not available");return 1;}
    float player[3]={0};if(!objectPosition(po,player)){Lua50::PushString(L,"E|PLAYER_POSITION_INVALID|player position is not readable");return 1;}
    unsigned long mgr=0,cur=0;long base=0;if(!objectManager(&mgr,&cur,&base)){Lua50::PushString(L,"E|OBJECT_MANAGER_UNAVAILABLE|object manager is not available");return 1;}

    GroupGuidCache groups={};buildGroupGuidCache(&groups);g_snapshotRows[0]=0;unsigned count=0,visited=0;float range2=range*range;
    for(;cur&&!(cur&1)&&visited<8192;){
        ++visited; void* o=(void*)cur;unsigned long type=0;
        if(!objectType(o,&type))break;
        if(type==WoW112::TYPE_UNIT){
            int creatureType=((GetCreatureTypeFn)WoW112::UNIT_GET_CREATURE_TYPE)(o);
            if(creatureType==CREATURE_TYPE_TOTEM){
                unsigned char* d=0;unsigned long long guid=0;if(descriptorPtr(o,&d)&&objectGuid(o,&guid)&&guid){
                    unsigned long long owner=totemOwnerGuid(d);const char* scope=totemScopeForOwner(owner,groups);
                    if(totemModeAllows(mode,scope)){
                        float p[3]={0};if(objectPosition(o,p)){float dx=p[0]-player[0],dy=p[1]-player[1];float d2=dx*dx+dy*dy;
                            if(d2<=range2){unsigned long entry=0,createdSpell=0;descriptorU32At(d,WoW112::OFF_OBJECT_FIELD_ENTRY/4UL,&entry);descriptorU32At(d,UNIT_FIELD_CREATED_BY_SPELL_INDEX,&createdSpell);
                                char hg[17]={0},ho[17]={0},auras[1024]={0};hex64(hg,sizeof(hg),guid);hex64(ho,sizeof(ho),owner);appendTotemAuraCsv(auras,sizeof(auras),d);
                                if(g_snapshotRows[0])appendChar(g_snapshotRows,sizeof(g_snapshotRows),'\n');cat(g_snapshotRows,sizeof(g_snapshotRows),"T|");cat(g_snapshotRows,sizeof(g_snapshotRows),hg);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendUInt(g_snapshotRows,sizeof(g_snapshotRows),entry);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');cat(g_snapshotRows,sizeof(g_snapshotRows),ho);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');cat(g_snapshotRows,sizeof(g_snapshotRows),scope);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendUInt(g_snapshotRows,sizeof(g_snapshotRows),createdSpell);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendFloat4(g_snapshotRows,sizeof(g_snapshotRows),p[0]);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendFloat4(g_snapshotRows,sizeof(g_snapshotRows),p[1]);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendFloat4(g_snapshotRows,sizeof(g_snapshotRows),p[2]);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendFloat4(g_snapshotRows,sizeof(g_snapshotRows),sqrtApprox(d2));appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendFloat4(g_snapshotRows,sizeof(g_snapshotRows),absf(player[2]-p[2]));appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');cat(g_snapshotRows,sizeof(g_snapshotRows),auras);++count;
                            }
                        }
                    }
                }
            }
        }
        unsigned long nxt=nextObject(cur,base);if(nxt==cur)break;cur=nxt;
    }
    if(g_snapshotRows[0])appendChar(g_snapshotRows,sizeof(g_snapshotRows),'\n');cat(g_snapshotRows,sizeof(g_snapshotRows),"S|");appendUInt(g_snapshotRows,sizeof(g_snapshotRows),count);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');appendUInt(g_snapshotRows,sizeof(g_snapshotRows),visited);appendChar(g_snapshotRows,sizeof(g_snapshotRows),'|');cat(g_snapshotRows,sizeof(g_snapshotRows),totemModeName(mode));Lua50::PushString(L,g_snapshotRows);return 1;
}
static int pushRelationRecord(Lua50::State L,void* target,const char* fail){
    if(!target){g_recordScratch[0]=0;cat(g_recordScratch,sizeof(g_recordScratch),"R|UNKNOWN|-1|-1|-1|0000000000000000|");cat(g_recordScratch,sizeof(g_recordScratch),fail?fail:"UNIT_UNRESOLVED");Lua50::PushString(L,g_recordScratch);return 1;}void* self=resolveUnit("player");if(!self){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|PLAYER_UNRESOLVED");return 1;}unsigned long t=0;unsigned long long g=0;if(!objectType(target,&t)||!objectGuid(target,&g)){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|DATA_UNAVAILABLE");return 1;}bool attack=((CanAttackFn)WoW112::CAN_ATTACK)(self,target);int reaction=((UnitReactionFn)WoW112::UNIT_REACTION)(self,target);const char* rel="UNKNOWN";if(target==self||reaction>=4)rel="FRIENDLY";else if(attack)rel="HOSTILE";char h[17]={0};hex64(h,sizeof(h),g);g_recordScratch[0]=0;cat(g_recordScratch,sizeof(g_recordScratch),"R|");cat(g_recordScratch,sizeof(g_recordScratch),rel);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendInt(g_recordScratch,sizeof(g_recordScratch),attack?1:0);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendInt(g_recordScratch,sizeof(g_recordScratch),reaction);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');appendUInt(g_recordScratch,sizeof(g_recordScratch),t);appendChar(g_recordScratch,sizeof(g_recordScratch),'|');cat(g_recordScratch,sizeof(g_recordScratch),h);cat(g_recordScratch,sizeof(g_recordScratch),"|OK");Lua50::PushString(L,g_recordScratch);return 1;
}
static int doRelationByUnit(Lua50::State L){ int e=requireFeature(L,&g_relationEnabled);if(e)return e;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|UNITID_INVALID");return 1;}const char* tok=Lua50::ToString(L,2);if(!validUnitToken(tok)){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|UNITID_INVALID");return 1;}return pushRelationRecord(L,resolveUnit(tok),"UNIT_UNRESOLVED"); }
static int doRelationByGuid(Lua50::State L){ int e=requireFeature(L,&g_relationEnabled);if(e)return e;unsigned long long g=0;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)||!parseGuid(Lua50::ToString(L,2),&g)){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|GUID_INVALID");return 1;}void* o=findObjectByGuid(g);if(!o){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|GUID_NOT_VISIBLE");return 1;}unsigned long t=0;if(!objectType(o,&t)|| (t!=WoW112::TYPE_UNIT&&t!=WoW112::TYPE_PLAYER)){Lua50::PushString(L,"R|UNKNOWN|-1|-1|-1|0000000000000000|GUID_NOT_UNIT");return 1;}return pushRelationRecord(L,o,"GUID_NOT_VISIBLE"); }

static int doVisualSet(Lua50::State L){
    int e=requireFeature(L,&g_visualEnabled);if(e)return e;
    if(Lua50::GetTop(L)<7||!Lua50::IsString(L,2)||!Lua50::IsString(L,3)||!Lua50::IsNumber(L,4)||!Lua50::IsNumber(L,5)||!Lua50::IsNumber(L,6)||!Lua50::IsNumber(L,7))return pushError(L,"BAD_ARGUMENT");
    float yaw=0.0f;if(Lua50::GetTop(L)>=8&&Lua50::IsNumber(L,8))yaw=(float)Lua50::ToNumber(L,8);
    char path[TysVisual::PATH_CAP]={0};bool ok=TysVisual::set(Lua50::ToString(L,2),Lua50::ToString(L,3),(float)Lua50::ToNumber(L,4),(float)Lua50::ToNumber(L,5),(float)Lua50::ToNumber(L,6),(float)Lua50::ToNumber(L,7),yaw,path,sizeof(path));
    Lua50::PushBool(L,ok);Lua50::PushString(L,path);Lua50::PushString(L,TysVisual::lastError());return 3;
}
static int doVisualMove(Lua50::State L){
    int e=requireFeature(L,&g_visualEnabled);if(e)return e;if(Lua50::GetTop(L)<6||!Lua50::IsString(L,2)||!Lua50::IsNumber(L,3)||!Lua50::IsNumber(L,4)||!Lua50::IsNumber(L,5)||!Lua50::IsNumber(L,6))return pushError(L,"BAD_ARGUMENT");
    float yaw=0.0f;if(Lua50::GetTop(L)>=7&&Lua50::IsNumber(L,7))yaw=(float)Lua50::ToNumber(L,7);Lua50::PushBool(L,TysVisual::move(Lua50::ToString(L,2),(float)Lua50::ToNumber(L,3),(float)Lua50::ToNumber(L,4),(float)Lua50::ToNumber(L,5),(float)Lua50::ToNumber(L,6),yaw));return 1;
}
static int doVisualBoolKey(Lua50::State L,int kind){
    int e=requireFeature(L,&g_visualEnabled);if(e)return e;if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");const char* k=Lua50::ToString(L,2);bool ok=false;if(kind==1)ok=TysVisual::hide(k);else if(kind==2)ok=TysVisual::show(k);else ok=TysVisual::restart(k);Lua50::PushBool(L,ok);return 1;
}
static int doVisualStats(Lua50::State L){int e=requireFeature(L,&g_visualEnabled);if(e)return e;char b[768]={0};TysVisual::stats(b,sizeof(b));Lua50::PushString(L,b);return 1;}

static int doApiCatalog(Lua50::State L){ Lua50::PushString(L,"FOUNDATION_F1|Foundation.Status; Fast GUID resolver stats + LOS cache counters + TysClock32 status; explicit query only; no polling\nCOOLDOWN_CD1R2|Cooldown.Status,Cooldown.Get,Cooldown.List,TYS_COOLDOWN_STARTED,TYS_COOLDOWN_CHANGED,TYS_COOLDOWN_READY; local-player; NativeBus dirty SpellIDs + engine query 0x006E2EA0 + active-deadline revalidation + SMSG_CLEAR_COOLDOWN/SMSG_COOLDOWN_CHEAT reset reconciliation; frozen after runtime PASS; no new hook/thread/spellbook scan\nUNITSTATE_US1R2|UnitState.Status,UnitState.Get,UnitState.Track,UnitState.Untrack,UnitState.List,UnitState.Clear,TYS_UNIT_HEALTH_CHANGED,TYS_UNIT_POWER_CHANGED,TYS_UNIT_COMBAT_CHANGED; explicit tracked GUIDs; UPDATE_OBJECT/COMPRESSED_UPDATE_OBJECT dirty gate; post-handler descriptor reconcile via fast GUID lookup; one object range + one descriptor range validation per snapshot; POWER event compares active power contract only; no packet body parsing/zlib/new hook/thread/ObjectManager scan; API37 CURRENT: Foundation F1 + AURA6-D4-R4 frozen + CAST1-R2 frozen + CD1-R2 frozen + LootFX Selector + AutoRange ARX1 independent runtime; NativeBus transport and dynamic custom-event slots\nSPATIAL_S5R1F1|Spatial.Status,Spatial.Get,Unit.Distance,Unit.Behind; explicit-query calibration core; Range unchanged; Behind uses live-calibrated rear-axis half-plane (positive dot=behind) validated against S5-R2 Backstab cast results; server Backstab still observed separately; LOS reuses frozen Unit.InSight LOS1; no hook/thread/timer/ObjectManager scan\nDRINKWALK_NATIVE|DrinkWalk.Status,DrinkWalk.ResolveItem,DrinkWalk.UseItem; restored from V0.4.x validated explicit native actuator; PackBagSlot->GetItemBySlot->CGItem::UseItem; no scheduler/hooks/packet forging\nAUTORANGE_ARX1_API33|Unit.Guid,Unit.InSight,GroundProbe.UnitStateByGuid; explicit-call-only; LOS=CWorld_Intersect 0x672170 flags 0x100111; symmetric native pair-cache TTL=50ms; no hook/thread/worker\nLOOTFX_API32|LootFX.Status,LootFX.Corpse.Get,LootFX.Corpse.Set,LootFX.Corpse.Default,LootFX.Corpse.Stock,LootFX.Gather.Get,LootFX.Gather.Set,LootFX.Gather.Stock; corpse=0x61FA6A; gather=0x61FC9F lazy-patched only when custom; no polling\nAURA_NATIVE|Aura.NativeBus.Status,Aura.Status,Aura.Diagnostics,Aura.Snapshot,Aura.Duration.Status,Aura.Duration.Snapshot,Aura.Source.Status,Aura.Source.Match,Aura.Source.Snapshot,Aura.Caster.Status,Aura.Caster.Match,Aura.Caster.Snapshot,Aura.State.Status,Aura.Get,Aura.List; CAST_NATIVE|Cast.Status,Cast.Get,Cast.State.Status,Cast.State.Get,Cast.State.GetByGuid,Cast.State.List; API33 CURRENT: AURA6-D4-R4 frozen + CAST1-R2 Unified CastState + LootFX Selector + AutoRange ARX1 independent runtime; NativeBus transport, dynamic custom-event slots, durable cast result, client-engine ms clock, no ObjectManager polling\nPORTED|facing,speed,GroundProbe.Status,GroundProbe.PlayerPosition,GroundProbe.Snapshot,GroundProbe.UnitByGuid,GroundProbe.DynamicByGuid,GroundProbe.ScopeByGuid,GroundProbe.TotemSnapshot,AutoRange.RelationByGuid,AutoRange.RelationByUnit\nINTERNAL_DBC|spell,spellradius,spellrange,spellduration,itemdisplayinfo,spellvisual,spellvisualkit,spellvisualeffect,spellsearch,creaturespelldata,creaturespellsearch,creaturebyspell,AutoRange.Status,AutoRange.Resolve,AutoRange.TotemInfo -> lazy internal MPQ/DBC\nPORTED_STATEFUL|AutoRange.VisualSet,AutoRange.VisualMove,AutoRange.VisualHide,AutoRange.VisualShow,AutoRange.VisualRestart,AutoRange.VisualStats,AutoRange.VisualClear,AutoRange.VisualClearAll,MoonMarker.Runtime.Status,MoonMarker.Place,MoonMarker.Remote,MoonMarker.Clear,MoonMarker.Targeting.Begin,MoonMarker.Targeting.Update,MoonMarker.Targeting.Cancel,MoonMarker.Targeting.Commit\nV1_SHARED_SCAN_MEDIA|AutoRange.M2Scan.Start,AutoRange.M2Scan.Step,AutoRange.M2Scan.Status,AutoRange.M2Scan.Get,AutoRange.M2Scan.ArchiveGet,AutoRange.Media.Scan,AutoRange.Media.Status,AutoRange.Media.Get; explicit calls only; no background worker\nV1_MOONMARKER|MMAuth,MoonMarker.Advanced.Ping,MoonMarker.Advanced.PreviewAt,MoonMarker.Advanced.PreviewAtPlayer,MoonMarker.Advanced.ClearPreview,MoonMarker.Advanced.SetPreviewTransform,MoonMarker.Advanced.PreviewStatus,MoonMarker.Advanced.ScanM2.*,MoonMarker.Advanced.Main.Seal,MoonMarker.Advanced.Main.Validate,MoonMarker.Advanced.Main.PlaceAt,MoonMarker.Main.Remote\nSHARED_VISUAL_CORE|AutoRange=16 slots; MoonMarker=64 fixed color/icon slots; AdvancedPreview=1 isolated slot; one native sceneEnd lifetime core\nV11_PROFILER|Profiler/profiler: wrap,start,stop,reset,hardreset,status,snapshot,files,entries,longframes,threshold; B1R4 Calibrated + B1R5R6/R7/R8 semantics\nV11_PROFILERDEEP|ProfilerDeep/profilerdeep: status,start,stop,reset,hardreset,deep,snapshot,functions,files,longframes,threshold; B1R5R4 Targeted Entry Sampling + B1R5R5 isolation\nDEVELOPER_DIAG|AutoRange.TriggerTrace -> explicit read-only trigger chain trace, max depth 8; never affects Resolve\nV12_DREAMAVATAR|DreamAvatar.* and MoonMarker.DreamAvatar.*: 34 commands; character/weapons/glows/mount + D1 signed Sync; explicit calls only, old addon AutoMaintain remains Lua-driven\nV13_DREAMWEAPON|DreamWeapon.* / MoonMarker.DreamWeapon.* plus legacy DreamAvatar weapon aliases; local weapon controls are public; SHARED_DAS1_SYNC=Sync.Build,BuildClear,Receive,Reapply,RestoreSender,RestoreAll; full character+weapons+glows+mount snapshot; no feature/auth gate; no duplicate remote manager; no second hook"); return 1; }

static int __fastcall scriptMain(Lua50::State _L){
    Lua50::State callerL=_L;
    Lua50::State L=Lua50::GetState(); if(!L)L=callerL; if(!L)return 0;
    // Profiler is stack-sensitive: wrap() receives a live Lua function in arg #3
    // and lua_getinfo(">S") must operate on the exact callback state/stack.
    // The known-good UnitXP B1R5R8 detour always used the caller-supplied L.
    // Route Profiler/ProfilerDeep before the legacy GetContext-based dispatch so
    // no alternate Lua state can replace the active callback stack.
    if(callerL && Lua50::GetTop(callerL)>=1 && Lua50::IsString(callerL,1)){
        const char* callerCmd=Lua50::ToString(callerL,1);
        if(callerCmd && (ieq(callerCmd,"profiler")||ieq(callerCmd,"profilerdeep")))
            return doProfilerDispatch(callerL,callerCmd);
    }
    if(Lua50::GetTop(L)<1||!Lua50::IsString(L,1))return pushError(L,"BAD_ARGUMENT");
    const char* cmd=Lua50::ToString(L,1); if(!cmd)return pushError(L,"BAD_ARGUMENT");
    if(eq(cmd,"Core.Version")){ Lua50::PushString(L,DLL_VERSION); Lua50::PushString(L,API_VERSION); Lua50::PushString(L,BUILD_ID); return 3; }
    if(eq(cmd,"Foundation.Status"))return TysFoundationF1::dispatchStatus(L);
    if(eq(cmd,"Cooldown.Status"))return TysCooldownCore::dispatchStatus(L);
    if(eq(cmd,"Cooldown.Get"))return TysCooldownCore::dispatchGet(L);
    if(eq(cmd,"Cooldown.List"))return TysCooldownCore::dispatchList(L);
    if(eq(cmd,"UnitState.Status"))return TysUnitStateCore::dispatchStatus(L);
    if(eq(cmd,"UnitState.Get"))return TysUnitStateCore::dispatchGet(L);
    if(eq(cmd,"UnitState.Track"))return TysUnitStateCore::dispatchTrack(L);
    if(eq(cmd,"UnitState.Untrack"))return TysUnitStateCore::dispatchUntrack(L);
    if(eq(cmd,"UnitState.List"))return TysUnitStateCore::dispatchList(L);
    if(eq(cmd,"UnitState.Clear"))return TysUnitStateCore::dispatchClear(L);
    if(eq(cmd,"Spatial.Status"))return TysSpatialCore::dispatchStatus(L);
    if(eq(cmd,"Spatial.Get"))return TysSpatialCore::dispatchGet(L);
    if(eq(cmd,"Unit.Distance"))return TysSpatialCore::dispatchDistance(L);
    if(eq(cmd,"Unit.Behind"))return TysSpatialCore::dispatchBehind(L);
    if(eq(cmd,"DrinkWalk.Status"))return TysDrinkWalkNative::dispatchStatus(callerL?callerL:L);
    if(eq(cmd,"DrinkWalk.ResolveItem"))return TysDrinkWalkNative::dispatchResolveItem(callerL?callerL:L);
    if(eq(cmd,"DrinkWalk.UseItem"))return TysDrinkWalkNative::dispatchUseItem(callerL?callerL:L);
    { int lootfx=TysCorpseMarker::dispatchSelector(L,cmd); if(lootfx>=0)return lootfx; }
    if(eq(cmd,"Aura.NativeBus.Status"))return TysNativeBus::dispatchStatus(L);
    if(eq(cmd,"Aura.Status"))return TysAuraNative::dispatchStatus(L);
    if(eq(cmd,"Aura.Diagnostics"))return TysAuraNative::dispatchDiagnostics(L);
    if(eq(cmd,"Aura.Snapshot"))return TysAuraNative::dispatchSnapshot(L);
    if(eq(cmd,"Aura.Duration.Status"))return TysAuraDuration::dispatchStatus(L);
    if(eq(cmd,"Aura.Duration.Snapshot"))return TysAuraDuration::dispatchSnapshot(L);
    if(eq(cmd,"Aura.Source.Status"))return TysAuraCaster::dispatchStatus(L);
    if(eq(cmd,"Aura.Source.Match"))return TysAuraCaster::dispatchMatch(L);
    if(eq(cmd,"Aura.Source.Snapshot"))return TysAuraCaster::dispatchSnapshot(L);
    // Existing AURA6 API names remain stable; both names read the same R2 core.
    if(eq(cmd,"Aura.Caster.Status"))return TysAuraCaster::dispatchStatus(L);
    if(eq(cmd,"Aura.Caster.Match"))return TysAuraCaster::dispatchMatch(L);
    if(eq(cmd,"Aura.Caster.Snapshot"))return TysAuraCaster::dispatchSnapshot(L);
    if(eq(cmd,"Aura.State.Status"))return TysAuraState::dispatchStatus(L);
    if(eq(cmd,"Aura.Get"))return TysAuraState::dispatchGet(L);
    if(eq(cmd,"Aura.List"))return TysAuraState::dispatchList(L);
    if(eq(cmd,"Cast.Status"))return TysSpellCast::dispatchStatus(L);
    if(eq(cmd,"Cast.Get"))return TysSpellCast::dispatchGet(L);
    if(eq(cmd,"Cast.State.Status"))return TysSpellCast::dispatchStateStatus(L);
    if(eq(cmd,"Cast.State.Get"))return TysSpellCast::dispatchStateGet(L);
    if(eq(cmd,"Cast.State.GetByGuid"))return TysSpellCast::dispatchStateGetByGuid(L);
    if(eq(cmd,"Cast.State.List"))return TysSpellCast::dispatchStateList(L);
    if(eq(cmd,"Runtime.WorldEvent")){
        // Compatibility-only API retained in PLR2. The DLL installs its own hidden event frame,
        // so addons no longer need to forward these events.
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");
        const char* ev=Lua50::ToString(L,2);
        if(!eq(ev,"PLAYER_LEAVING_WORLD")&&!eq(ev,"PLAYER_ENTERING_WORLD"))return pushError(L,"BAD_ARGUMENT");
        handleRuntimeWorldEvent(callerL?callerL:L,ev);Lua50::PushBool(L,true);return 1;
    }
    if(eq(cmd,"Core.Status")){
        BuildInfo b=detectBuild();
        Lua50::PushBool(L,g_loaded!=0); Lua50::PushString(L,DLL_VERSION); Lua50::PushString(L,b.ok?"SUPPORTED_BUILD":"UNSUPPORTED_BUILD"); Lua50::PushString(L,g_lastRegisterCode);
        Lua50::PushBool(L,g_playerEnabled); Lua50::PushString(L,g_playerHookMode); Lua50::PushString(L,g_playerObserve);
        Lua50::PushBool(L,g_facingEnabled!=0); Lua50::PushString(L,g_facingStatus);
        Lua50::PushBool(L,g_speedEnabled!=0); Lua50::PushString(L,g_speedStatus);
        Lua50::PushBool(L,g_groundProbeEnabled!=0); Lua50::PushString(L,g_groundProbeStatus);
        Lua50::PushBool(L,g_relationEnabled!=0); Lua50::PushString(L,g_relationStatus);
        Lua50::PushBool(L,g_dbcEnabled!=0); Lua50::PushString(L,g_dbcStatus);
        Lua50::PushBool(L,g_visualEnabled!=0); Lua50::PushString(L,g_visualStatus); return 19;
    }
    if(eq(cmd,"Feature.List")){
        char b[280]="facing=";cat(b,sizeof(b),g_facingEnabled?"1":"0");cat(b,sizeof(b),"|speed=");cat(b,sizeof(b),g_speedEnabled?"1":"0");cat(b,sizeof(b),"|groundprobe=");cat(b,sizeof(b),g_groundProbeEnabled?"1":"0");cat(b,sizeof(b),"|relation=");cat(b,sizeof(b),g_relationEnabled?"1":"0");cat(b,sizeof(b),"|dbc=");cat(b,sizeof(b),g_dbcEnabled?"1":"0");cat(b,sizeof(b),"|visual=");cat(b,sizeof(b),g_visualEnabled?"1":"0");cat(b,sizeof(b),"|dreamavatar=");cat(b,sizeof(b),g_dreamAvatarEnabled?"1":"0");cat(b,sizeof(b),"|dreamweapon=");cat(b,sizeof(b),g_dreamWeaponEnabled?"1":"0");Lua50::PushString(L,b);return 1;
    }
    if(eq(cmd,"Feature.Status")){
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");const char* n=Lua50::ToString(L,2);
        if(ieq(n,"facing")){pushBoolCode(L,g_facingEnabled!=0,g_facingStatus);return 2;} if(ieq(n,"speed")){pushBoolCode(L,g_speedEnabled!=0,g_speedStatus);return 2;} if(ieq(n,"groundprobe")){pushBoolCode(L,g_groundProbeEnabled!=0,g_groundProbeStatus);return 2;} if(ieq(n,"relation")){pushBoolCode(L,g_relationEnabled!=0,g_relationStatus);return 2;} if(ieq(n,"dbc")||ieq(n,"dbcbridge")){pushBoolCode(L,g_dbcEnabled!=0,g_dbcStatus);return 2;} if(ieq(n,"visual")||ieq(n,"autorange.visual")){pushBoolCode(L,g_visualEnabled!=0,g_visualStatus);return 2;} if(ieq(n,"dreamavatar")||ieq(n,"dreamavatar.native")){pushBoolCode(L,g_dreamAvatarEnabled!=0,g_dreamAvatarStatus);return 2;} if(ieq(n,"dreamweapon")||ieq(n,"dreamweapon.native")){pushBoolCode(L,g_dreamWeaponEnabled!=0,g_dreamWeaponStatus);return 2;} return pushError(L,"API_NOT_FOUND");
    }
    if(eq(cmd,"Feature.Enable")){
        // PLR2 compatibility API: every known feature is already available for
        // the full process. Actual heavy initialization remains first-use lazy.
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");
        const char* n=Lua50::ToString(L,2);
        if(ieq(n,"facing")||ieq(n,"speed")||ieq(n,"groundprobe")||ieq(n,"relation")||ieq(n,"dbc")||ieq(n,"dbcbridge")||ieq(n,"visual")||ieq(n,"autorange.visual")||ieq(n,"dreamavatar")||ieq(n,"dreamavatar.native")||ieq(n,"dreamweapon")||ieq(n,"dreamweapon.native")){
            pushBoolCode(L,true,"PROCESS_LIFETIME_AVAILABLE");return 2;
        }
        return pushError(L,"API_NOT_FOUND");
    }
    if(eq(cmd,"Feature.Disable")){
        // PLR2 never tears down a feature or hook during the WoW process. This
        // prevents one addon from invalidating another addon's shared runtime.
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushError(L,"BAD_ARGUMENT");
        const char* n=Lua50::ToString(L,2);
        if(ieq(n,"facing")||ieq(n,"speed")||ieq(n,"groundprobe")||ieq(n,"relation")||ieq(n,"dbc")||ieq(n,"dbcbridge")||ieq(n,"visual")||ieq(n,"autorange.visual")||ieq(n,"dreamavatar")||ieq(n,"dreamavatar.native")||ieq(n,"dreamweapon")||ieq(n,"dreamweapon.native")){
            pushBoolCode(L,false,"PROCESS_LIFETIME_RUNTIME");return 2;
        }
        return pushError(L,"API_NOT_FOUND");
    }
    if(eq(cmd,"MoonMarker.Runtime.Status"))return doMoonMarkerRuntimeStatus(L);
    if(isMoonMarkerGuardedCommand(cmd)&&!TysMoonRuntimeGuard::enabled())return pushMoonMarkerGuardFailure(L);
    if(eq(cmd,"facing"))return doFacing(L); if(eq(cmd,"speed"))return doSpeed(L);
    if(eq(cmd,"Unit.Guid"))return doUnitGuid(L); if(eq(cmd,"Unit.InSight"))return doUnitInSight(L);
    if(eq(cmd,"GroundProbe.Status"))return doGroundStatus(L); if(eq(cmd,"GroundProbe.PlayerPosition"))return doPlayerPosition(L); if(eq(cmd,"GroundProbe.Snapshot"))return doSnapshot(L); if(eq(cmd,"GroundProbe.UnitByGuid"))return doUnitByGuid(L); if(eq(cmd,"GroundProbe.UnitStateByGuid"))return doUnitStateByGuid(L); if(eq(cmd,"GroundProbe.DynamicByGuid"))return doDynamicByGuid(L); if(eq(cmd,"GroundProbe.ScopeByGuid"))return doScopeByGuid(L); if(eq(cmd,"GroundProbe.TotemSnapshot"))return doTotemSnapshot(L);
    if(eq(cmd,"AutoRange.RelationByUnit"))return doRelationByUnit(L); if(eq(cmd,"AutoRange.RelationByGuid"))return doRelationByGuid(L);
    // V1.3-DW5: authorization is classified by command type, not by the
    // DreamAvatar feature switch. DAS1 Sync.* is a shared/public explicit-call
    // transport and is dispatched before both DreamWeapon and DreamAvatar.
    // It carries character + weapons + glows + mount and never uses local-edit auth.
    if(TysDreamAvatar::isSharedSyncCommand(cmd)){
        if(!g_das1PreflightReady){
            const char* code="";
            if(!dreamAvatarPreflight(&code)){cpy(g_das1Status,sizeof(g_das1Status),code);pushBoolCode(L,false,code);return 2;}
            InterlockedExchange(&g_das1PreflightReady,1);cpy(g_das1Status,sizeof(g_das1Status),"READY");
        }
        int sync=TysDreamAvatar::dispatchSharedSync(L,cmd);if(sync>=0)return sync;
    }
    // Local weapon controls are public. Character/glow/mount local editing still
    // goes through DreamAvatar feature + authorization.
    const bool isPublicDw = (TysDreamWeapon::isCommand(cmd) && !TysDreamAvatar::isSharedSyncCommand(cmd))
        || TysDreamWeapon::isLegacyWeaponCommand(cmd);
    if(isPublicDw){
        const char* code="";
        if(!ensureDreamWeaponRuntime(&code)){pushBoolCode(L,false,code);return 2;}
        int dw=TysDreamWeapon::dispatchAnyPublic(L,cmd);
        if(dw>=0)return dw;
    }
    if((startsI(cmd,"DreamAvatar.")||startsI(cmd,"MoonMarker.DreamAvatar."))&&!TysDreamAvatar::isSharedSyncCommand(cmd)){
        const char* code="";if(!ensureDreamAvatarRuntime(&code)){pushBoolCode(L,false,code);return 2;}
    }
    if(commandNeedsVisualRuntime(cmd)){
        const char* code="";if(!ensureVisualRuntime(&code)){pushBoolCode(L,false,code);return 2;}
    }
    { int da=TysDreamAvatar::dispatch(L,cmd,true); if(da>=0)return da; }
    { int mm=TysMoonMarker::dispatch(L,cmd,true); if(mm>=0)return mm; }
    { int adv=TysMoonAdvanced::dispatch(L,cmd,true); if(adv>=0)return adv; }
    { int sm=TysScanMedia::dispatchAutoRange(L,cmd); if(sm>=0)return sm; }
    if(eq(cmd,"AutoRange.VisualSet"))return doVisualSet(L); if(eq(cmd,"AutoRange.VisualMove"))return doVisualMove(L); if(eq(cmd,"AutoRange.VisualHide"))return doVisualBoolKey(L,1); if(eq(cmd,"AutoRange.VisualShow"))return doVisualBoolKey(L,2); if(eq(cmd,"AutoRange.VisualRestart"))return doVisualBoolKey(L,3);
    if(eq(cmd,"AutoRange.VisualStats"))return doVisualStats(L); if(eq(cmd,"AutoRange.VisualClear")){int e=requireFeature(L,&g_visualEnabled);if(e)return e;if(Lua50::GetTop(L)>=2&&Lua50::IsString(L,2))TysVisual::clear(Lua50::ToString(L,2));return 0;} if(eq(cmd,"AutoRange.VisualClearAll")){int e=requireFeature(L,&g_visualEnabled);if(e)return e;TysVisual::clearAll();return 0;}
    if(eq(cmd,"AutoRange.Status")){const char* code="";if(!ensureDbcRuntime(&code))return pushError(L,code);return TysDbc::dispatchAutoRangeStatus(L);}
    if(eq(cmd,"DBC.Status")){const char* code="";if(!ensureDbcRuntime(&code))return pushError(L,code);Lua50::PushBool(L,true);Lua50::PushString(L,g_dbcStatus);Lua50::PushBool(L,TysDbc::archiveScanAttempted());Lua50::PushNumber(L,(double)TysDbc::archiveCount());Lua50::PushString(L,TysDbc::lastError());return 5;}
    if(TysDbc::isSearchCommand(cmd)){const char* code="";if(!ensureDbcRuntime(&code))return pushError(L,code);if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return TysDbc::dispatchSearch(L,cmd,"",100);unsigned long limit=100;if(Lua50::GetTop(L)>=3&&Lua50::IsNumber(L,3)){double n=Lua50::ToNumber(L,3);if(n>=1.0)limit=n>500.0?500UL:(unsigned long)n;}return TysDbc::dispatchSearch(L,cmd,Lua50::ToString(L,2),limit);}
    if(TysDbc::isCommand(cmd)){const char* code="";if(!ensureDbcRuntime(&code))return pushError(L,code);if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2))return pushError(L,"BAD_ARGUMENT");unsigned long id=(unsigned long)Lua50::ToNumber(L,2);if(!id)return pushError(L,"BAD_ARGUMENT");return TysDbc::dispatch(L,cmd,id);}
    if(eq(cmd,"API.Catalog"))return doApiCatalog(L);
    if(eq(cmd,"Conflict.Scan")){buildConflictReport();Lua50::PushString(L,g_conflictReport);return 1;} if(eq(cmd,"Conflict.Status")){Lua50::PushString(L,g_conflictReport);return 1;}
    return pushError(L,"API_NOT_FOUND");
}

static bool globalFunctionPresent(Lua50::State L){
    if(!L)return false; int top=Lua50::GetTop(L); Lua50::PushString(L,"TaiYangShenDian"); Lua50::GetTable(L,WoW112::LUA_GLOBALSINDEX); bool ok=Lua50::Type(L,-1)==WoW112::LUA_TFUNCTION; Lua50::SetTop(L,top); return ok;
}
static void registerAfterLifecycle(const char* why){
    Lua50::State L=Lua50::GetState();
    if(!L){ cpy(g_lastRegisterCode,sizeof(g_lastRegisterCode),"LUA_STATE_MISSING"); InterlockedExchange(&g_luaRegistered,0); logText("Lua post-register: state missing\r\n"); return; }
    if(L==g_lastRegisteredState && globalFunctionPresent(L)){
        installRuntimeLifecycleFrame(L);
        InterlockedExchange(&g_luaRegistered,1); cpy(g_lastRegisterCode,sizeof(g_lastRegisterCode),"OK_ALREADY_PRESENT"); logText("Lua post-register: same state + global present; runtime lifecycle ready\r\n"); return;
    }
    FrameRegisterFn fn=(FrameRegisterFn)WoW112::FRAME_SCRIPT_REGISTER_FUNCTION; fn("TaiYangShenDian",&scriptMain);
    g_lastRegisteredState=L;InterlockedExchange(&g_worldPhase,0);installRuntimeLifecycleFrame(L);
    InterlockedExchange(&g_luaRegistered,1); cpy(g_lastRegisterCode,sizeof(g_lastRegisterCode),why);
    logText("Lua post-register: TaiYangShenDian registered; native lifecycle frame installed ("); logText(why); logText(")\r\n");
}

static void __stdcall playerLifecycleHook(){ if(g_playerNext)g_playerNext(); registerAfterLifecycle("OK_POST_PLAYER_LOAD"); }

static bool installPlayerHookDirect(){
    const unsigned long target=WoW112::PLAYER_LOAD_SCRIPT_FUNCTIONS;
    cpy(g_playerHookMode,sizeof(g_playerHookMode),"DIRECT_MINHOOK_PLAYER_LOAD");
    if(!target||!executable(target)){ cpy(g_playerHookMode,sizeof(g_playerHookMode),"PLAYER_TARGET_NOT_EXECUTABLE"); return false; }
    g_playerHookTarget=target;
    MH_STATUS init=MH_Initialize(); if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){ cpy(g_playerHookMode,sizeof(g_playerHookMode),"MH_INIT_FAILED"); return false; }
    MH_STATUS cr=MH_CreateHook((LPVOID)target,(LPVOID)&playerLifecycleHook,(LPVOID*)&g_playerNext); if(cr!=MH_OK){ cpy(g_playerHookMode,sizeof(g_playerHookMode),"MH_CREATE_FAILED"); return false; }
    g_playerCreated=true; MH_STATUS en=MH_EnableHook((LPVOID)target); if(en==MH_OK||en==MH_ERROR_ENABLED){ g_playerEnabled=true; return true; }
    cpy(g_playerHookMode,sizeof(g_playerHookMode),"MH_ENABLE_FAILED"); return false;
}

static void buildConflictReport(){
    char* o=g_conflictReport; unsigned long cap=sizeof(g_conflictReport); o[0]=0;
    BuildInfo b=detectBuild(); cat(o,cap,"TYS_CONFLICT_V120\nBUILD|"); cat(o,cap,b.ok?"SUPPORTED":"UNSUPPORTED");
    cat(o,cap,"\nPLAYER_LOAD|OBS="); cat(o,cap,g_playerObserve);
    cat(o,cap,"\nPLAYER_HOOK|MODE="); cat(o,cap,g_playerHookMode); cat(o,cap,"|ENABLED="); cat(o,cap,g_playerEnabled?"1":"0"); cat(o,cap,"|TARGET="); char h[16]={0};hex32(h,sizeof(h),g_playerHookTarget);cat(o,cap,h);
    cat(o,cap,"\nAURA|BACKEND=");cat(o,cap,TysAuraNative::backend());cat(o,cap,"|STATUS=");cat(o,cap,TysAuraNative::status());cat(o,cap,"|NATIVE_HOOKS=");cat(o,cap,TysAuraNative::nativeHooksInstalled()?"1":"0");cat(o,cap,"|EVENTS_READY=");cat(o,cap,TysAuraNative::eventsReady()?"1":"0");cat(o,cap,"|NATIVE_BUS=");cat(o,cap,TysNativeBus::status());
    cat(o,cap,"\nGLUE_LOAD|OBS="); cat(o,cap,g_glueObserve);
    cat(o,cap,"\nFRAME_REGISTER|OBS="); cat(o,cap,g_frameObserve);
    const char* fc="";facingPreflight(&fc);
    const char* sc="";speedPreflight(&sc);
    const char* gpc="";groundProbePreflight(&gpc);
    const char* rc="";relationPreflight(&rc);
    cat(o,cap,"\nFACING|ENABLED=");cat(o,cap,g_facingEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_facingStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,fc);cat(o,cap,"|HOOKS_INSTALLED=0|CALLS=");cat(o,cap,g_facingResolveObserve);
    cat(o,cap,"\nSPEED|ENABLED=");cat(o,cap,g_speedEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_speedStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,sc);cat(o,cap,"|HOOKS_INSTALLED=0|CALLS=");cat(o,cap,g_speedResolveObserve);
    cat(o,cap,"\nGROUNDPROBE|ENABLED=");cat(o,cap,g_groundProbeEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_groundProbeStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,gpc);cat(o,cap,"|HOOKS_INSTALLED=0|CALLS=");cat(o,cap,g_groundProbeObserve);
    cat(o,cap,"\nRELATION|ENABLED=");cat(o,cap,g_relationEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_relationStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,rc);cat(o,cap,"|HOOKS_INSTALLED=0|CALLS=");cat(o,cap,g_relationObserve);
    const char* dc="";dbcPreflight(&dc);
    cat(o,cap,"\nDBC|ENABLED=");cat(o,cap,g_dbcEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_dbcStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,dc);cat(o,cap,"|HOOKS_INSTALLED=0|SFILE=");cat(o,cap,g_dbcObserve);cat(o,cap,"|DATA_ACCESS=LAZY_INTERNAL_MPQ|SCAN_ATTEMPTED=");cat(o,cap,TysDbc::archiveScanAttempted()?"1":"0");cat(o,cap,"|ARCHIVES=");appendUInt(o,cap,TysDbc::archiveCount());cat(o,cap,"|LAST_ERROR=");cat(o,cap,TysDbc::lastError());
    const char* vc="";visualPreflight(&vc);
    cat(o,cap,"\nVISUAL|ENABLED=");cat(o,cap,g_visualEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_visualStatus);cat(o,cap,"|PREFLIGHT=");cat(o,cap,vc);cat(o,cap,"|HOOKS_INSTALLED=");cat(o,cap,g_visualSceneEnabled?"1":"0");cat(o,cap,"|SCENE_MODE=");cat(o,cap,g_visualSceneMode);cat(o,cap,"|SCENE_TARGET=");char vh[16]={0};hex32(vh,sizeof(vh),g_visualSceneTarget);cat(o,cap,vh);cat(o,cap,"|M2=");cat(o,cap,g_visualObserve);cat(o,cap,"|ACTIVE_VISUALS=");appendUInt(o,cap,TysVisual::activeCount());cat(o,cap,"|HIDDEN_VISUALS=");appendUInt(o,cap,TysVisual::hiddenCount());cat(o,cap,"|MOONMARKER=");appendUInt(o,cap,TysVisual::moonActiveCount());cat(o,cap,"|MODELS=");appendUInt(o,cap,TysVisual::modelCount());cat(o,cap,"|MAINTENANCE=SHARED_NATIVE_SCENEEND_WHEN_VISUAL_ON|MM_TARGETING=");cat(o,cap,TysMoonMarker::status());cat(o,cap,"|LAST_STAGE=");cat(o,cap,TysVisual::lastError());
    cat(o,cap,"\nDREAMAVATAR|ENABLED=");cat(o,cap,g_dreamAvatarEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_dreamAvatarStatus);cat(o,cap,"|NATIVE=");cat(o,cap,TysDreamAvatar::status());
    cat(o,cap,"\nDREAMWEAPON|ENABLED=");cat(o,cap,g_dreamWeaponEnabled?"1":"0");cat(o,cap,"|STATUS=");cat(o,cap,g_dreamWeaponStatus);cat(o,cap,"|NATIVE=");cat(o,cap,TysDreamWeapon::status());
    cat(o,cap,"\nPROFILER|FRAME_HOOK=");cat(o,cap,g_visualSceneEnabled?"1":"0");cat(o,cap,"|BOUNDARY_NEEDED=");cat(o,cap,TysProfiler::needsFrameBoundary()?"1":"0");cat(o,cap,"|MODE=B1R4_LIGHT_PLUS_B1R5R4_TARGETED_DEEP");
    cat(o,cap,"\nLUA|REGISTER="); cat(o,cap,g_lastRegisterCode);cat(o,cap,"|RUNTIME_LIFECYCLE=");cat(o,cap,g_runtimeLifecycleStatus);cat(o,cap,"|PROCESS_LIFETIME=1");
}

static void doLoad(){
    logOpen(); logText("taiyangshendian "); logText(DLL_VERSION); logText(" Load() entered\r\n");
    // One-shot MoonMarker ABI validation/fingerprint, matching the original
    // runtime guard. No heavy feature hook is installed here. PLR2 exposes every
    // feature immediately while keeping DBC/visual/stateful initialization lazy.
    TysMoonRuntimeGuard::initialize();
    BuildInfo b=detectBuild();
    if(!b.ok){ cpy(g_playerHookMode,sizeof(g_playerHookMode),"UNSUPPORTED_BUILD"); buildConflictReport(); logText("Build check: UNSUPPORTED_BUILD; no hook installed\r\n"); InterlockedExchange(&g_loaded,1); return; }
    bool corpseReady=TysCorpseMarker::initialize();
    logText("Corpse LootFX routing: "); logText(TysCorpseMarker::status()); logText(corpseReady ? " / READY\r\n" : " / NOT_READY\r\n");
    bool nativeBusReady = TysNativeBus::initialize();
    logText("NativeBus: "); logText(TysNativeBus::status()); logText(nativeBusReady ? " / READY\r\n" : " / NOT_READY\r\n");
    TysAuraNative::initialize();
    TysAuraDuration::initialize();
    TysAuraCaster::initialize();
    TysAuraState::initialize();
    TysSpellCast::initialize();
    TysCooldownCore::initialize();
    TysUnitStateCore::initialize();
    logText("Aura backend: ");logText(TysAuraNative::backend());logText(" / ");logText(TysAuraNative::status());logText("\r\n");
    observeEntry(WoW112::PLAYER_LOAD_SCRIPT_FUNCTIONS,"PLAYER_LOAD_SCRIPT_FUNCTIONS observe-only",g_playerObserve,sizeof(g_playerObserve));
    observeEntry(WoW112::GLUE_LOAD_SCRIPT_FUNCTIONS,"GLUE_LOAD_SCRIPT_FUNCTIONS observe-only",g_glueObserve,sizeof(g_glueObserve));
    observeEntry(WoW112::FRAME_SCRIPT_REGISTER_FUNCTION,"FRAME_SCRIPT_REGISTER_FUNCTION observe-only",g_frameObserve,sizeof(g_frameObserve));
    if(installPlayerHookDirect()){ logText("PLAYER lifecycle post-hook enabled: "); logText(g_playerHookMode); logText("\r\n"); }
    else { logText("PLAYER lifecycle post-hook NOT enabled: "); logText(g_playerHookMode); logText("\r\n"); }
    registerAfterLifecycle("OK_DIRECT_LOAD"); buildConflictReport(); InterlockedExchange(&g_loaded,1); logText("Load complete\r\n");
}
}

extern "C" __declspec(dllexport) unsigned long __cdecl Load(){ if(InterlockedCompareExchange(&TYS::g_loadOnce,1,0)==0)TYS::doLoad(); return 0; }
extern "C" __declspec(dllexport) void __cdecl FirstEnterWorld(){ TYS::registerAfterLifecycle("OK_FIRST_ENTER_WORLD"); TYS::buildConflictReport(); }
extern "C" __declspec(dllexport) int __cdecl TaiYangShenDianNativeStatus(){ return TYS::g_loaded && TYS::g_luaRegistered ? 1:0; }
extern "C" __declspec(dllexport) BOOL __stdcall DllMain(HMODULE h,DWORD reason,LPVOID){ if(reason==TYS::DLL_PROCESS_ATTACH_)TYS::g_self=h; if(reason==TYS::DLL_PROCESS_DETACH_){ TysCorpseMarker::shutdown(); TysDreamWeapon::shutdown(0); TysDreamAvatar::shutdown(0); TysProfiler::shutdown(); if(TYS::g_log!=INVALID_HANDLE_VALUE){ CloseHandle(TYS::g_log); TYS::g_log=INVALID_HANDLE_VALUE; } } return TRUE; }
