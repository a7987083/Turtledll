#include <windows.h>
#include "corpse_marker.h"

namespace TysCorpseMarker {
namespace {

using CreateModelFn = void* (__thiscall*)(void*, const char*, unsigned long);

// Real-client A/B fingerprints, validated on Turtle WoW 1.12.1 build 5875:
// - corpse scene:    0x61FA6A + 0x61FC9F
// - gathering scene:             0x61FC9F
// The stable baseline already routes only 0x61FA6A for corpse visuals.
constexpr unsigned long CORPSE_CREATE_MODEL_CALLSITE = 0x0061FA6AUL;
constexpr unsigned long GATHER_CREATE_MODEL_CALLSITE = 0x0061FC9FUL;
constexpr unsigned long CREATE_MODEL_ENTRY = 0x00707350UL;
constexpr unsigned long PATH_CAP = 241UL;

constexpr const char* STOCK_LOOTFX_MDL = "Particles\\LootFX.mdl";
constexpr const char* DEFAULT_CORPSE_MDL = "Particles\\TaiYangCorpse\\LootFX.mdl";

struct CallPatch {
    unsigned long callsite;
    unsigned char original[5];
    bool patched;
    unsigned long originalDest;
    CreateModelFn next;
};

static CallPatch g_corpsePatch = {CORPSE_CREATE_MODEL_CALLSITE,{0,0,0,0,0},false,0,0};
static CallPatch g_gatherPatch = {GATHER_CREATE_MODEL_CALLSITE,{0,0,0,0,0},false,0,0};

static volatile LONG g_enabled = 1;
static volatile LONG g_initialized = 0;
static char g_status[96] = "NOT_INITIALIZED";
static char g_corpsePath[PATH_CAP] = "Particles\\TaiYangCorpse\\LootFX.mdl";
static char g_gatherPath[PATH_CAP] = "Particles\\LootFX.mdl";

static unsigned long g_corpseCalls = 0;
static unsigned long g_corpseMatch = 0;
static unsigned long g_corpseDiverted = 0;
static unsigned long g_corpseFail = 0;
static unsigned long g_corpseFallback = 0;
static unsigned long g_gatherCalls = 0;
static unsigned long g_gatherMatch = 0;
static unsigned long g_gatherDiverted = 0;
static unsigned long g_gatherFail = 0;
static unsigned long g_gatherFallback = 0;

static unsigned long slen(const char* s){unsigned long n=0;if(s)while(s[n])++n;return n;}
static void cpy(char* o,unsigned long cap,const char* s){if(!cap)return;unsigned long i=0;if(s)for(;s[i]&&i+1<cap;++i)o[i]=s[i];o[i]=0;}
static void cat(char* o,unsigned long cap,const char* s){unsigned long n=slen(o);if(n<cap)cpy(o+n,cap-n,s);}
static void appendChar(char* o,unsigned long cap,char c){unsigned long n=slen(o);if(n+1>=cap)return;o[n]=c;o[n+1]=0;}
static void appendUInt(char* o,unsigned long cap,unsigned long v){char t[16]={0};int n=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<15);while(n)appendChar(o,cap,t[--n]);}
static char lower(char c){return (c>='A'&&c<='Z')?(char)(c+32):c;}
static bool endsI(const char* s,const char* ext){unsigned long a=slen(s),b=slen(ext);if(a<b)return false;s+=a-b;while(*s&&*ext){if(lower(*s)!=lower(*ext))return false;++s;++ext;}return *s==0&&*ext==0;}

static bool readable(const void* p,unsigned long n){
    if(!p||!n)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery(p,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;
    DWORD q=m.Protect&0xFF;
    if(q==PAGE_NOACCESS||q==PAGE_EXECUTE)return false;
    unsigned long a=(unsigned long)p,b=(unsigned long)m.BaseAddress+(unsigned long)m.RegionSize;
    return a+n>=a&&a+n<=b;
}

static bool executable(unsigned long a){
    if(!a)return false;MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;
    DWORD p=m.Protect&0xFF;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool samePath(const char* a,const char* b){
    if(!a||!b)return false;
    unsigned long i=0;
    for(;;++i){
        if(!readable(a+i,1)||!readable(b+i,1))return false;
        char x=a[i],y=b[i];
        if(x=='/')x='\\';if(y=='/')y='\\';x=lower(x);y=lower(y);
        if(x!=y)return false;if(!x)return true;
    }
}

static bool normalizeModelPath(const char* in,char* out,unsigned long cap){
    if(!in||!out||cap<6)return false;
    unsigned long n=slen(in);if(n<4||n>236)return false;
    if(in[0]=='\\'||in[0]=='/')return false;
    bool dotdot=false;
    for(unsigned long i=0;i<n;++i){
        unsigned char c=(unsigned char)in[i];
        if(c<0x20||c>0x7E||c==':')return false;
        if(i+1<n&&in[i]=='.'&&in[i+1]=='.')dotdot=true;
    }
    if(dotdot)return false;
    if(endsI(in,".m2")){
        if(n+2>cap)return false;
        for(unsigned long i=0;i<n-2;++i)out[i]=(in[i]=='/')?'\\':in[i];
        out[n-2]='m';out[n-1]='d';out[n]='l';out[n+1]=0;
        return true;
    }
    if(!(endsI(in,".mdl")||endsI(in,".mdx")))return false;
    if(n+1>cap)return false;
    for(unsigned long i=0;i<n;++i)out[i]=(in[i]=='/')?'\\':in[i];out[n]=0;
    return true;
}

static bool patchCall(CallPatch& p,void* hook){
    if(p.patched)return true;
    unsigned char* at=(unsigned char*)p.callsite;
    if(!readable(at,5)||at[0]!=0xE8)return false;
    for(unsigned long i=0;i<5;++i)p.original[i]=at[i];
    long oldRel=*(const long*)(at+1);
    p.originalDest=(unsigned long)((long)(p.callsite+5)+(long)oldRel);
    if(p.originalDest!=CREATE_MODEL_ENTRY||!executable(p.originalDest))return false;
    p.next=(CreateModelFn)p.originalDest;
    long newRel=(long)((unsigned long)hook-(p.callsite+5));
    DWORD oldProt=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&oldProt)){p.next=0;p.originalDest=0;return false;}
    at[0]=0xE8;unsigned char* r=(unsigned char*)&newRel;for(unsigned long i=0;i<4;++i)at[1+i]=r[i];
    DWORD ignored=0;VirtualProtect(at,5,oldProt,&ignored);FlushInstructionCache(GetCurrentProcess(),at,5);
    p.patched=true;return true;
}

static void restoreCall(CallPatch& p){
    if(!p.patched)return;
    unsigned char* at=(unsigned char*)p.callsite;DWORD oldProt=0;
    if(VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&oldProt)){
        for(unsigned long i=0;i<5;++i)at[i]=p.original[i];
        DWORD ignored=0;VirtualProtect(at,5,oldProt,&ignored);FlushInstructionCache(GetCurrentProcess(),at,5);
    }
    p.patched=false;p.next=0;p.originalDest=0;
}

static void* createSelected(CreateModelFn next,void* self,const char* path,unsigned long flags,const char* selected,
                            unsigned long& match,unsigned long& diverted,unsigned long& fail,unsigned long& fallback){
    if(!next)return 0;
    if(InterlockedCompareExchange(&g_enabled,0,0)==0||!samePath(path,STOCK_LOOTFX_MDL))return next(self,path,flags);
    ++match;
    if(samePath(selected,STOCK_LOOTFX_MDL))return next(self,path,flags);
    void* replacement=next(self,selected,flags);
    if(replacement){++diverted;return replacement;}
    ++fail;void* stock=next(self,path,flags);if(stock)++fallback;return stock;
}

static void* __fastcall corpseCreateModelHook(void* self,void*,const char* path,unsigned long flags){
    ++g_corpseCalls;
    return createSelected(g_corpsePatch.next,self,path,flags,g_corpsePath,g_corpseMatch,g_corpseDiverted,g_corpseFail,g_corpseFallback);
}

static void* __fastcall gatherCreateModelHook(void* self,void*,const char* path,unsigned long flags){
    ++g_gatherCalls;
    return createSelected(g_gatherPatch.next,self,path,flags,g_gatherPath,g_gatherMatch,g_gatherDiverted,g_gatherFail,g_gatherFallback);
}

static bool setCorpsePath(const char* path,char* normalized,unsigned long cap){
    char p[PATH_CAP]={0};if(!normalizeModelPath(path,p,sizeof(p)))return false;
    cpy(g_corpsePath,sizeof(g_corpsePath),p);if(normalized&&cap)cpy(normalized,cap,p);return true;
}

static bool setGatherPath(const char* path,char* normalized,unsigned long cap){
    char p[PATH_CAP]={0};if(!normalizeModelPath(path,p,sizeof(p)))return false;
    if(samePath(p,STOCK_LOOTFX_MDL)){
        cpy(g_gatherPath,sizeof(g_gatherPath),STOCK_LOOTFX_MDL);restoreCall(g_gatherPatch);
        if(normalized&&cap)cpy(normalized,cap,g_gatherPath);return true;
    }
    if(!patchCall(g_gatherPatch,(void*)&gatherCreateModelHook))return false;
    cpy(g_gatherPath,sizeof(g_gatherPath),p);if(normalized&&cap)cpy(normalized,cap,p);return true;
}

static bool cmdEq(const char* a,const char* b){return samePath(a,b);}
static int pushSetResult(Lua50::State L,bool ok,const char* code,const char* path){Lua50::PushBool(L,ok);Lua50::PushString(L,code);Lua50::PushString(L,path?path:"");return 3;}

} // namespace

bool initialize(){
    if(InterlockedCompareExchange(&g_initialized,0,0)!=0)return true;
    cpy(g_corpsePath,sizeof(g_corpsePath),DEFAULT_CORPSE_MDL);
    cpy(g_gatherPath,sizeof(g_gatherPath),STOCK_LOOTFX_MDL);
    if(!patchCall(g_corpsePatch,(void*)&corpseCreateModelHook)){
        cpy(g_status,sizeof(g_status),"CORPSE_CALLSITE_PATCH_FAILED");restoreCall(g_corpsePatch);return false;
    }
    InterlockedExchange(&g_initialized,1);cpy(g_status,sizeof(g_status),"READY_LOOTFX_SELECTOR_API32");return true;
}

void shutdown(){restoreCall(g_gatherPatch);restoreCall(g_corpsePatch);InterlockedExchange(&g_initialized,0);cpy(g_status,sizeof(g_status),"SHUTDOWN");}
bool setEnabled(bool v){InterlockedExchange(&g_enabled,v?1:0);return true;}
bool enabled(){return InterlockedCompareExchange(&g_enabled,0,0)!=0;}
const char* status(){return g_status;}
const char* modelPath(){return g_corpsePath;}

void diagnostics(char* out,unsigned long cap){
    if(!out||!cap)return;out[0]=0;
    cat(out,cap,"status=");cat(out,cap,g_status);cat(out,cap,"|enabled=");cat(out,cap,enabled()?"1":"0");
    cat(out,cap,"|corpsePatched=");cat(out,cap,g_corpsePatch.patched?"1":"0");cat(out,cap,"|corpsePath=");cat(out,cap,g_corpsePath);
    cat(out,cap,"|corpseCalls=");appendUInt(out,cap,g_corpseCalls);cat(out,cap,"|corpseMatch=");appendUInt(out,cap,g_corpseMatch);cat(out,cap,"|corpseDiverted=");appendUInt(out,cap,g_corpseDiverted);cat(out,cap,"|corpseFail=");appendUInt(out,cap,g_corpseFail);cat(out,cap,"|corpseFallback=");appendUInt(out,cap,g_corpseFallback);
    cat(out,cap,"|gatherPatched=");cat(out,cap,g_gatherPatch.patched?"1":"0");cat(out,cap,"|gatherPath=");cat(out,cap,g_gatherPath);
    cat(out,cap,"|gatherCalls=");appendUInt(out,cap,g_gatherCalls);cat(out,cap,"|gatherMatch=");appendUInt(out,cap,g_gatherMatch);cat(out,cap,"|gatherDiverted=");appendUInt(out,cap,g_gatherDiverted);cat(out,cap,"|gatherFail=");appendUInt(out,cap,g_gatherFail);cat(out,cap,"|gatherFallback=");appendUInt(out,cap,g_gatherFallback);
}

int dispatchSelector(Lua50::State L,const char* cmd){
    if(!cmd)return -1;
    if(cmdEq(cmd,"LootFX.Status")){
        char b[1024]={0};diagnostics(b,sizeof(b));Lua50::PushString(L,b);return 1;
    }
    if(cmdEq(cmd,"LootFX.Corpse.Get")){Lua50::PushString(L,g_corpsePath);Lua50::PushBool(L,!samePath(g_corpsePath,STOCK_LOOTFX_MDL));return 2;}
    if(cmdEq(cmd,"LootFX.Gather.Get")){Lua50::PushString(L,g_gatherPath);Lua50::PushBool(L,g_gatherPatch.patched);return 2;}
    if(cmdEq(cmd,"LootFX.Corpse.Default")){cpy(g_corpsePath,sizeof(g_corpsePath),DEFAULT_CORPSE_MDL);return pushSetResult(L,true,"OK_DEFAULT_CORPSE",g_corpsePath);}
    if(cmdEq(cmd,"LootFX.Corpse.Stock")){cpy(g_corpsePath,sizeof(g_corpsePath),STOCK_LOOTFX_MDL);return pushSetResult(L,true,"OK_STOCK_CORPSE",g_corpsePath);}
    if(cmdEq(cmd,"LootFX.Gather.Stock")){cpy(g_gatherPath,sizeof(g_gatherPath),STOCK_LOOTFX_MDL);restoreCall(g_gatherPatch);return pushSetResult(L,true,"OK_STOCK_GATHER",g_gatherPath);}
    if(cmdEq(cmd,"LootFX.Corpse.Set")){
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushSetResult(L,false,"BAD_ARGUMENT",g_corpsePath);
        char p[PATH_CAP]={0};if(!setCorpsePath(Lua50::ToString(L,2),p,sizeof(p)))return pushSetResult(L,false,"INVALID_MODEL_PATH",g_corpsePath);
        return pushSetResult(L,true,"OK",p);
    }
    if(cmdEq(cmd,"LootFX.Gather.Set")){
        if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2))return pushSetResult(L,false,"BAD_ARGUMENT",g_gatherPath);
        char p[PATH_CAP]={0};if(!normalizeModelPath(Lua50::ToString(L,2),p,sizeof(p)))return pushSetResult(L,false,"INVALID_MODEL_PATH",g_gatherPath);
        if(!setGatherPath(p,p,sizeof(p)))return pushSetResult(L,false,"GATHER_CALLSITE_PATCH_FAILED",g_gatherPath);
        return pushSetResult(L,true,"OK",p);
    }
    return -1;
}

} // namespace TysCorpseMarker
