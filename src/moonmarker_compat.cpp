#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "moonmarker_compat.h"
#include "moonmarker_advanced.h"
#include "moonmarker_runtime_guard.h"
#include "visual_native.h"

namespace TysMoonMarker {
namespace {

struct V3 { float x,y,z; };
using GroundCursorProc = int (__fastcall*)(V3*,void*);
using CursorModeProc = void (__fastcall*)(int,void*);
using CursorObjectActionProc = void (__thiscall*)(unsigned long,int);
using CursorObjectTargetProc = void (__thiscall*)(unsigned long,int,int,void*,int);

constexpr unsigned long GROUND_CURSOR=0x006E60F0UL;
constexpr unsigned long CURSOR_MODE_A=0x00523D20UL;
constexpr unsigned long CURSOR_MODE_B=0x00523C20UL;
constexpr unsigned long CURSOR_ACTION=0x00514810UL;
constexpr unsigned long CURSOR_TARGET=0x00515090UL;
constexpr unsigned long CURSOR_MODE_STATE=0x00CECAC0UL;
constexpr unsigned long CURSOR_OBJECT_PTR=0x00BE1148UL;
constexpr unsigned long CURSOR_TARGET_DATA=0x00CF0BC8UL;
constexpr unsigned long WORLD_FRAME_PTR=0x00B4B2BCUL;
constexpr unsigned long WORLD_TO_SCREEN=0x00483EE0UL;
constexpr unsigned long DDC_TO_NDC=0x0041ADE0UL;

static GroundCursorProc g_groundNext=0;
static unsigned long g_groundTarget=0;
static bool g_groundCreated=false;
static bool g_groundEnabled=false;
static bool g_captureActive=false;
static bool g_captureReceived=false;
static V3 g_captured={0,0,0};
static V3 g_target={0,0,0};
static bool g_targetValid=false;
static char g_status[96]="READY_LAZY";

static unsigned long slen(const char* s){unsigned long n=0;if(s)while(s[n])++n;return n;}
static bool eq(const char* a,const char* b){if(!a||!b)return false;while(*a&&*b){if(*a!=*b)return false;++a;++b;}return *a==*b;}
static void cpy(char* o,unsigned long cap,const char* s){if(!cap)return;unsigned long i=0;if(s)for(;s[i]&&i+1<cap;++i)o[i]=s[i];o[i]=0;}
static bool finitef(float v){return v==v&&v>-100000000.0f&&v<100000000.0f;}
static bool executable(unsigned long a){MEMORY_BASIC_INFORMATION m={};if(VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;DWORD p=m.Protect&0xFF;return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool readable(const void* p,unsigned long n){if(!p||!n)return false;MEMORY_BASIC_INFORMATION m={};if(VirtualQuery(p,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;DWORD q=m.Protect&0xFF;if(q==PAGE_NOACCESS)return false;unsigned long a=(unsigned long)p,b=(unsigned long)m.BaseAddress+(unsigned long)m.RegionSize;return a+n>=a&&a+n<=b;}

static unsigned long chainedTarget(unsigned long entry){
    if(!executable(entry))return 0;unsigned char* p=(unsigned char*)entry;
    if(readable(p,5)&&p[0]==0xE9){long rel=*(long*)(p+1);unsigned long d=entry+5+(unsigned long)rel;if(executable(d))return d;}
    if(readable(p,6)&&p[0]==0xFF&&p[1]==0x25){unsigned long addr=*(unsigned long*)(p+2);if(readable((void*)addr,4)){unsigned long d=*(unsigned long*)addr;if(executable(d))return d;}}
    return entry;
}

static int __fastcall detouredGroundCursor(V3* position,void*){
    if(!g_captureActive)return g_groundNext?g_groundNext(position,0):0;
    if(position){g_captured=*position;g_captureReceived=finitef(position->x)&&finitef(position->y)&&finitef(position->z);}
    *(unsigned long*)CURSOR_MODE_STATE=0;((CursorModeProc)CURSOR_MODE_A)(1,0);((CursorModeProc)CURSOR_MODE_B)(1,0);g_captureActive=false;return 0;
}

static bool ensureGroundHook(){
    if(g_groundCreated){if(!g_groundEnabled){MH_STATUS e=MH_EnableHook((LPVOID)g_groundTarget);if(e==MH_OK||e==MH_ERROR_ENABLED){g_groundEnabled=true;return true;}cpy(g_status,sizeof(g_status),"GROUND_REENABLE_FAILED");return false;}return true;}
    if(!executable(GROUND_CURSOR)||!executable(CURSOR_MODE_A)||!executable(CURSOR_MODE_B)||!executable(CURSOR_ACTION)||!executable(CURSOR_TARGET)||!readable((void*)CURSOR_MODE_STATE,4)||!readable((void*)CURSOR_OBJECT_PTR,4)||!readable((void*)CURSOR_TARGET_DATA,4)){cpy(g_status,sizeof(g_status),"GROUND_PREFLIGHT_FAILED");return false;}
    unsigned long target=chainedTarget(GROUND_CURSOR);if(!target){cpy(g_status,sizeof(g_status),"GROUND_TARGET_INVALID");return false;}MH_STATUS init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){cpy(g_status,sizeof(g_status),"GROUND_MH_INIT_FAILED");return false;}g_groundTarget=target;MH_STATUS c=MH_CreateHook((LPVOID)target,(LPVOID)&detouredGroundCursor,(LPVOID*)&g_groundNext);if(c!=MH_OK){g_groundTarget=0;cpy(g_status,sizeof(g_status),"GROUND_MH_CREATE_FAILED");return false;}g_groundCreated=true;MH_STATUS e=MH_EnableHook((LPVOID)target);if(e==MH_OK||e==MH_ERROR_ENABLED){g_groundEnabled=true;cpy(g_status,sizeof(g_status),target==GROUND_CURSOR?"GROUND_WOW_CHAIN":"GROUND_EXISTING_CHAIN");return true;}cpy(g_status,sizeof(g_status),"GROUND_MH_ENABLE_FAILED");return false;
}

static void cancelCapture(){g_captureActive=false;g_captureReceived=false;*(unsigned long*)CURSOR_MODE_STATE=0;((CursorModeProc)CURSOR_MODE_A)(1,0);((CursorModeProc)CURSOR_MODE_B)(1,0);}

static bool cursorGround(V3* out){
    if(!out)return false;
    if(!ensureGroundHook()){TysMoonRuntimeGuard::markHookInstallFailed("GROUND_CURSOR_HOOK_INSTALL_FAILED");return false;}g_captured={0,0,0};g_captureReceived=false;g_captureActive=true;*(unsigned long*)CURSOR_MODE_STATE=0x40;((CursorModeProc)CURSOR_MODE_A)(2,0);((CursorModeProc)CURSOR_MODE_B)(2,0);
    unsigned long obj=*(unsigned long*)CURSOR_OBJECT_PTR;if(!obj||(obj&1UL)){cancelCapture();return false;}((CursorObjectActionProc)CURSOR_ACTION)(obj,1);CursorObjectTargetProc target=(CursorObjectTargetProc)CURSOR_TARGET;target(obj,2,1,(void*)CURSOR_TARGET_DATA,0);target(obj,2,0,(void*)CURSOR_TARGET_DATA,0);if(!g_captureReceived){g_captureActive=false;return false;}*out=g_captured;return true;
}

static bool updateTarget(bool create){V3 p={0,0,0};if(!cursorGround(&p))return g_targetValid;g_target=p;g_targetValid=true;if(create)return TysVisual::moonPreviewSet(p.x,p.y,p.z);if(!TysVisual::moonPreviewMove(p.x,p.y,p.z))return TysVisual::moonPreviewSet(p.x,p.y,p.z);return true;}
static int pushTarget(Lua50::State L,bool valid){Lua50::PushBool(L,valid);if(!valid)return 1;Lua50::PushNumber(L,g_target.x);Lua50::PushNumber(L,g_target.y);Lua50::PushNumber(L,g_target.z);return 4;}
static int pushDisabled(Lua50::State L){Lua50::PushBool(L,false);Lua50::PushString(L,"FEATURE_DISABLED");return 2;}

} // namespace

bool preflight(const char** code){
    if(!code)return false;
    if(!executable(GROUND_CURSOR)||!executable(CURSOR_MODE_A)||!executable(CURSOR_MODE_B)
        ||!executable(CURSOR_ACTION)||!executable(CURSOR_TARGET)
        ||!executable(WORLD_TO_SCREEN)||!executable(DDC_TO_NDC)
        ||!readable((void*)CURSOR_MODE_STATE,4)||!readable((void*)CURSOR_OBJECT_PTR,4)
        ||!readable((void*)CURSOR_TARGET_DATA,4)||!readable((void*)WORLD_FRAME_PTR,4)){
        *code="ADDRESS_VALIDATION_FAILED";return false;
    }
    if(g_groundCreated&&!g_groundEnabled){*code=g_status;return false;}
    *code=g_groundCreated?"READY_GROUND_HOOK":"READY_LAZY_GROUND";return true;
}

int dispatch(Lua50::State L,const char* cmd,bool visualEnabled){
    if(!cmd)return -1;
    const bool standard=eq(cmd,"MoonMarker.Place")||eq(cmd,"MoonMarker.Remote")||eq(cmd,"MoonMarker.Clear")||eq(cmd,"MoonMarker.Targeting.Begin")||eq(cmd,"MoonMarker.Targeting.Update")||eq(cmd,"MoonMarker.Targeting.Cancel")||eq(cmd,"MoonMarker.Targeting.Commit");
    if(!standard)return -1;if(!visualEnabled)return pushDisabled(L);
    if(eq(cmd,"MoonMarker.Clear")){g_targetValid=false;TysMoonAdvanced::clearLocalDraft();TysVisual::moonClearAll();TysVisual::moonPreviewClear();TysVisual::moonAdvancedPreviewClear();Lua50::PushBool(L,true);return 1;}
    if(eq(cmd,"MoonMarker.Remote")){
        if(Lua50::GetTop(L)<6||!Lua50::IsString(L,2)||!Lua50::IsNumber(L,3)||!Lua50::IsNumber(L,4)||!Lua50::IsNumber(L,5)||!Lua50::IsString(L,6)){Lua50::PushBool(L,false);return 1;}
        char c[16]={0},ic[16]={0};bool ok=TysVisual::moonSet(Lua50::ToString(L,2),Lua50::ToString(L,6),(float)Lua50::ToNumber(L,3),(float)Lua50::ToNumber(L,4),(float)Lua50::ToNumber(L,5),c,sizeof(c),ic,sizeof(ic));Lua50::PushBool(L,ok);return 1;
    }
    if(eq(cmd,"MoonMarker.Place")){
        if(Lua50::GetTop(L)<3||!Lua50::IsString(L,2)||!Lua50::IsString(L,3))return 0;V3 p={0,0,0};if(!cursorGround(&p))return 0;char c[16]={0},ic[16]={0};if(!TysVisual::moonSet(Lua50::ToString(L,2),Lua50::ToString(L,3),p.x,p.y,p.z,c,sizeof(c),ic,sizeof(ic)))return 0;Lua50::PushNumber(L,p.x);Lua50::PushNumber(L,p.y);Lua50::PushNumber(L,p.z);Lua50::PushString(L,c);Lua50::PushString(L,ic);return 5;
    }
    if(eq(cmd,"MoonMarker.Targeting.Begin")){TysVisual::moonPreviewClear();g_targetValid=false;updateTarget(true);return pushTarget(L,g_targetValid);}
    if(eq(cmd,"MoonMarker.Targeting.Update")){updateTarget(false);return pushTarget(L,g_targetValid);}
    if(eq(cmd,"MoonMarker.Targeting.Cancel")){TysVisual::moonPreviewClear();g_targetValid=false;Lua50::PushBool(L,true);return 1;}
    if(eq(cmd,"MoonMarker.Targeting.Commit")){updateTarget(false);TysVisual::moonPreviewClear();bool valid=g_targetValid;int n=pushTarget(L,valid);g_targetValid=false;return n;}
    return -1;
}

void shutdown(){g_captureActive=false;g_captureReceived=false;g_targetValid=false;TysVisual::shutdownMoonPresentation();if(g_groundCreated&&g_groundTarget){MH_DisableHook((LPVOID)g_groundTarget);MH_RemoveHook((LPVOID)g_groundTarget);}g_groundNext=0;g_groundTarget=0;g_groundCreated=false;g_groundEnabled=false;cpy(g_status,sizeof(g_status),"SHUTDOWN");}
const char* status(){return g_status;}

} // namespace TysMoonMarker
