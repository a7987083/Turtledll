#include <windows.h>
#include "profiler_handoff_b1r5r8.h"
#include "wow112_offsets.h"

// Profiler / ProfilerDeep full re-migration from the known-good UnitXP_SP3 B1R5R8
// handoff source.  The original implementation used STL/CRT containers; this TYS
// port intentionally keeps the V1.2 no-CRT/KERNEL32-only build contract while
// preserving the B1R4->B1R5R8 API and lifecycle semantics.
// Known-good reference DLL SHA-256:
// 915d9e98bf651fb466d1f52e4636094bb1afe0f2fa8687d094597d42c6492d8c
namespace TysProfiler {

static const char* LIGHT_VERSION="AddonProfiler B1R4 Calibrated";
static const char* LIGHT_STATUS_MODE="LIGHT_ENTRY_WRAPPER";
static const char* LIGHT_SNAPSHOT_MODE="LIGHT_ENTRY_WRAPPER_B1R5R6_LONGFRAME_CONTRIBUTION";
static const char* DEEP_VERSION="AddonProfiler Deep B1R5R4 Targeted";
static const char* DEEP_MODE="TARGETED_ENTRY_SAMPLING";
static const char* LONG_DETAILS_BUILD="B1R5R8_LONGFRAME_TOP5_DETAILS";
static const char* REMIGRATION_BUILD="TYS_V12_FULL_REMIGRATION_FROM_HANDOFF_B1R5R8";

static const unsigned MAX_HANDLERS=8192;
static const unsigned MAX_ADDONS=512;
static const unsigned MAX_FILES=4096;
static const unsigned ADDON_SAMPLES=256;
static const unsigned ENTRY_SAMPLES=128;
static const unsigned MAX_LONGFRAMES=64;
static const unsigned MAX_CONTRIB=5;
static const unsigned MAX_DEEP_FUNCS=4096;
static const unsigned MAX_DEEP_STACK=192;
static const double LONG_MIN_MS=2.0;
static const double LONG_MIN_PCT=10.0;

static unsigned slen(const char* s){unsigned n=0;if(s)while(s[n])++n;return n;}
static void cpy(char* d,unsigned cap,const char* s){if(!d||!cap)return;unsigned i=0;if(s)for(;s[i]&&i+1<cap;++i)d[i]=s[i];d[i]=0;}
static bool eq(const char* a,const char* b){if(!a||!b)return false;while(*a&&*b){if(*a!=*b)return false;++a;++b;}return *a==*b;}
static char lowerc(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool ieq(const char* a,const char* b){if(!a||!b)return false;while(*a&&*b){if(lowerc(*a)!=lowerc(*b))return false;++a;++b;}return *a==*b;}
static bool startsI(const char* s,const char* p){if(!s||!p)return false;while(*p){if(!*s||lowerc(*s)!=lowerc(*p))return false;++s;++p;}return true;}
static const char* findI(const char* s,const char* needle){if(!s||!needle||!*needle)return s;for(const char* p=s;*p;++p){const char* a=p;const char* b=needle;while(*a&&*b&&lowerc(*a)==lowerc(*b)){++a;++b;}if(!*b)return p;}return 0;}
static double dabs(double x){return x<0?-x:x;}

struct QpcValue { long long QuadPart; };
using QpcFn=BOOL (WINAPI *)(QpcValue*);
static QpcFn g_qpc=0;
static QpcFn g_qpf=0; // same physical signature: LARGE_INTEGER*
static long long g_freq=0;
static bool g_qpcInit=false;
static bool initQpc(){
    if(g_qpcInit)return g_qpc&&g_qpf&&g_freq>0;
    g_qpcInit=true;HMODULE k=GetModuleHandleA("kernel32.dll");if(!k)return false;
    g_qpc=(QpcFn)GetProcAddress(k,"QueryPerformanceCounter");g_qpf=(QpcFn)GetProcAddress(k,"QueryPerformanceFrequency");
    if(!g_qpc||!g_qpf)return false;QpcValue f={0};if(!g_qpf(&f)||f.QuadPart<=0)return false;g_freq=f.QuadPart;return true;
}
static long long nowTicks(){QpcValue q={0};if(!initQpc())return 0;if(!g_qpc(&q))return 0;return q.QuadPart;}
static double ticksMs(long long t){if(t<=0||g_freq<=0)return 0.0;return (double)t*1000.0/(double)g_freq;}
static double ticksSec(long long t){if(t<=0||g_freq<=0)return 0.0;return (double)t/(double)g_freq;}

static HANDLE g_heap=0;
static bool ensureHeap(){if(g_heap)return true;g_heap=HeapCreate(0,0,0);return g_heap!=0;}
static void* allocz(unsigned bytes){if(!ensureHeap())return 0;void* p=HeapAlloc(g_heap,0,bytes);if(p){unsigned char* q=(unsigned char*)p;for(unsigned i=0;i<bytes;++i)q[i]=0;}return p;}
static void freep(void*& p){if(p&&g_heap){HeapFree(g_heap,0,p);p=0;}}

struct Handler {
    unsigned generation;
    unsigned addonIndex;
    unsigned fileIndex;
    unsigned kind; // 0 other, 1 update, 2 event
    int line;
    long long currentTicks;
    unsigned currentCalls;
    long long totalTicks;
    unsigned long totalCalls;
    long long peakTicks;
    long long publishedTicks;
    unsigned long publishedCalls;
    long long publishedPeakTicks;
    double publishedP99Ms;
    double samples[ENTRY_SAMPLES];
    unsigned sampleCount;
    unsigned samplePos;
};
struct AddonStat {
    char name[96];
    long long currentTicks;
    long long currentKindTicks[3];
    unsigned currentCalls;
    unsigned currentKindCalls[3];
    long long totalTicks;
    long long totalKindTicks[3];
    unsigned long totalCalls;
    unsigned long totalKindCalls[3];
    long long peakFrameTicks;
    long long publishedTicks;
    long long publishedKindTicks[3];
    unsigned long publishedCalls;
    unsigned long publishedKindCalls[3];
    long long publishedPeakFrameTicks;
    double publishedP99Ms;
    unsigned long publishedLongFrameHits;
    unsigned long publishedLongFrameContributionHits;
    double samples[ADDON_SAMPLES];
    unsigned sampleCount;
    unsigned samplePos;
    unsigned long longFrameHits;
    unsigned long totalLongFrameHits;
    unsigned long longFrameContributionHits;
    unsigned long totalLongFrameContributionHits;
};
struct FileStat {
    char addon[96];
    char file[192];
    long long currentTicks;
    unsigned currentCalls;
    long long totalTicks;
    unsigned long totalCalls;
    long long peakFrameTicks;
    long long publishedTicks;
    unsigned long publishedCalls;
    long long publishedPeakFrameTicks;
};
struct Contributor {
    char addon[96];
    char file[192];
    double ms;
    double pct;
    int level;
};
struct LongFrame {
    char time[32];
    double frameMs;
    double luaMs;
    char addon[96];
    char file[192];
    double associatedAddonMs;
    double associatedAddonPct;
    double measuredLuaPct;
    double unmeasuredMs;
    int level;
    unsigned contributorCount;
    Contributor contributors[MAX_CONTRIB];
};

static Handler* g_handlers=0;static unsigned g_handlerCount=0;
static AddonStat* g_addons=0;static unsigned g_addonCount=0;
static FileStat* g_files=0;static unsigned g_fileCount=0;
static LongFrame g_longFrames[MAX_LONGFRAMES];static unsigned g_longCount=0;
static unsigned g_generation=1;
static bool g_running=false;
static bool g_deepIsolation=false;
static long long g_startTicks=0,g_windowStartTicks=0,g_lastFrameTicks=0;
static unsigned long g_frames=0,g_publishedFrames=0;
static long long g_totalFrameTicks=0,g_peakFrameTicks=0,g_totalMeasuredTicks=0;
static long long g_publishedFrameTicks=0,g_publishedPeakFrameTicks=0,g_publishedMeasuredTicks=0;
static long long g_wrapperOverheadTicks=0,g_boundaryOverheadTicks=0;
static long long g_publishedWrapperOverheadTicks=0,g_publishedBoundaryOverheadTicks=0;
static double g_publishedWindowSeconds=0.0;
static bool g_havePublishedWindow=false;
static unsigned long g_totalLongFrameCount=0;
static double g_lastSnapshotOverheadMs=0.0;
static double g_longThresholdMs=50.0;
static double g_lastLuaMemoryKb=0.0,g_allocRateKbSec=0.0;
static long long g_lastMemoryQpc=0,g_lastGcQpc=0;
static unsigned long g_gcDetected=0;

static bool ensureLightStorage(){
    if(!ensureHeap())return false;
    if(!g_handlers)g_handlers=(Handler*)allocz(sizeof(Handler)*MAX_HANDLERS);
    if(!g_addons)g_addons=(AddonStat*)allocz(sizeof(AddonStat)*MAX_ADDONS);
    if(!g_files)g_files=(FileStat*)allocz(sizeof(FileStat)*MAX_FILES);
    return g_handlers&&g_addons&&g_files;
}
static void zeroBytes(void* p,unsigned n){unsigned char* q=(unsigned char*)p;for(unsigned i=0;i<n;++i)q[i]=0;}
static void resetCurrent(){
    if(g_handlers)for(unsigned i=0;i<g_handlerCount;++i){g_handlers[i].currentTicks=0;g_handlers[i].currentCalls=0;}
    if(g_addons)for(unsigned i=0;i<g_addonCount;++i){g_addons[i].currentTicks=0;g_addons[i].currentCalls=0;for(int k=0;k<3;++k){g_addons[i].currentKindTicks[k]=0;g_addons[i].currentKindCalls[k]=0;}}
    if(g_files)for(unsigned i=0;i<g_fileCount;++i){g_files[i].currentTicks=0;g_files[i].currentCalls=0;}
}
static void resetStatsOnly(){
    g_frames=0;g_publishedFrames=0;g_totalFrameTicks=0;g_peakFrameTicks=0;g_totalMeasuredTicks=0;g_publishedFrameTicks=0;g_publishedPeakFrameTicks=0;g_publishedMeasuredTicks=0;g_wrapperOverheadTicks=0;g_boundaryOverheadTicks=0;g_publishedWrapperOverheadTicks=0;g_publishedBoundaryOverheadTicks=0;g_publishedWindowSeconds=0;g_havePublishedWindow=false;g_lastSnapshotOverheadMs=0;g_longCount=0;g_totalLongFrameCount=0;
    for(unsigned i=0;i<MAX_LONGFRAMES;++i)zeroBytes(&g_longFrames[i],sizeof(LongFrame));
    if(g_handlers)for(unsigned i=0;i<g_handlerCount;++i){Handler& h=g_handlers[i];h.currentTicks=0;h.currentCalls=0;h.totalTicks=0;h.totalCalls=0;h.peakTicks=0;h.publishedTicks=0;h.publishedCalls=0;h.publishedPeakTicks=0;h.publishedP99Ms=0;h.sampleCount=0;h.samplePos=0;for(unsigned j=0;j<ENTRY_SAMPLES;++j)h.samples[j]=0;}
    if(g_addons)for(unsigned i=0;i<g_addonCount;++i){AddonStat& a=g_addons[i];a.currentTicks=0;a.currentCalls=0;a.totalTicks=0;a.totalCalls=0;a.peakFrameTicks=0;a.publishedTicks=0;a.publishedCalls=0;a.publishedPeakFrameTicks=0;a.publishedP99Ms=0;a.publishedLongFrameHits=0;a.publishedLongFrameContributionHits=0;a.sampleCount=0;a.samplePos=0;a.longFrameHits=0;a.totalLongFrameHits=0;a.longFrameContributionHits=0;a.totalLongFrameContributionHits=0;for(int k=0;k<3;++k){a.currentKindTicks[k]=0;a.currentKindCalls[k]=0;a.totalKindTicks[k]=0;a.totalKindCalls[k]=0;a.publishedKindTicks[k]=0;a.publishedKindCalls[k]=0;}for(unsigned j=0;j<ADDON_SAMPLES;++j)a.samples[j]=0;}
    if(g_files)for(unsigned i=0;i<g_fileCount;++i){FileStat& f=g_files[i];f.currentTicks=0;f.currentCalls=0;f.totalTicks=0;f.totalCalls=0;f.peakFrameTicks=0;f.publishedTicks=0;f.publishedCalls=0;f.publishedPeakFrameTicks=0;}
    g_startTicks=nowTicks();g_windowStartTicks=g_startTicks;g_lastFrameTicks=0;g_lastLuaMemoryKb=0;g_allocRateKbSec=0;g_lastMemoryQpc=0;g_lastGcQpc=0;g_gcDetected=0;
}

static int findAddon(const char* name){if(!g_addons||!name)return -1;for(unsigned i=0;i<g_addonCount;++i)if(eq(g_addons[i].name,name))return (int)i;return -1;}
static int findOrAddAddon(const char* name){int x=findAddon(name);if(x>=0)return x;if(!g_addons||g_addonCount>=MAX_ADDONS)return -1;AddonStat& a=g_addons[g_addonCount];zeroBytes(&a,sizeof(a));cpy(a.name,sizeof(a.name),name);return (int)g_addonCount++;}
static int findOrAddFile(const char* addon,const char* file){if(!g_files)return -1;for(unsigned i=0;i<g_fileCount;++i)if(eq(g_files[i].addon,addon)&&eq(g_files[i].file,file))return (int)i;if(g_fileCount>=MAX_FILES)return -1;FileStat& f=g_files[g_fileCount];zeroBytes(&f,sizeof(f));cpy(f.addon,sizeof(f.addon),addon);cpy(f.file,sizeof(f.file),file);return (int)g_fileCount++;}

static void normalizeCopy(const char* raw,char* out,unsigned cap){
    if(!out||!cap)return;out[0]=0;if(!raw)return;if(*raw=='@')++raw;unsigned n=0;
    while(*raw&&n+1<cap){char c=*raw++;out[n++]=(c=='\\')?'/':c;}out[n]=0;
}
static bool sourceParts(const char* raw,char* addon,unsigned acap,char* file,unsigned fcap){
    if(addon&&acap)addon[0]=0;if(file&&fcap)file[0]=0;char src[512]={0};normalizeCopy(raw,src,sizeof(src));
    const char* marker=findI(src,"interface/addons/");
    if(marker){
        marker+=17;const char* p=marker;while(*p&&*p!='/')++p;
        if(p>marker){unsigned n=(unsigned)(p-marker);if(n>=acap)n=acap-1;for(unsigned i=0;i<n;++i)addon[i]=marker[i];addon[n]=0;if(*p=='/')++p;cpy(file,fcap,*p?p:addon);return addon[0]!=0;}
    }
    if(findI(src,"interface/framexml/")||findI(src,"interface/sharedxml/")){cpy(addon,acap,"Blizzard");cpy(file,fcap,src);return true;}
    cpy(file,fcap,src);return false;
}
static unsigned kindValue(const char* k){if(k&&eq(k,"OnUpdate"))return 1;if(k&&eq(k,"OnEvent"))return 2;return 0;}
static const char* kindName(unsigned k){return k==1?"OnUpdate":(k==2?"OnEvent":"Other");}

static void setFieldString(Lua50::State L,const char* k,const char* v){Lua50::PushString(L,k);Lua50::PushString(L,v?v:"");Lua50::SetTable(L,-3);}
static void setFieldNumber(Lua50::State L,const char* k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setFieldBool(Lua50::State L,const char* k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static void setArrayRow(Lua50::State L,unsigned idx){Lua50::SetTable(L,-3);(void)idx;}
static double percent(double a,double b){return b>0.0?a*100.0/b:0.0;}
static int contributionLevel(double ms,double pct){if(ms>=10.0&&pct>=50.0)return 3;if(ms>=5.0&&pct>=25.0)return 2;if(ms>=2.0&&pct>=10.0)return 1;return 0;}
static const char* contributionLabel(int l){return l>=3?"高度解释":(l==2?"主要贡献":(l==1?"有效贡献":"仅伴随"));}
static double p99(const double* src,unsigned count,unsigned cap){
    if(!src||!count)return 0;unsigned n=count<cap?count:cap;double tmp[ADDON_SAMPLES];if(n>ADDON_SAMPLES)n=ADDON_SAMPLES;for(unsigned i=0;i<n;++i)tmp[i]=src[i];
    for(unsigned i=1;i<n;++i){double x=tmp[i];unsigned j=i;while(j>0&&tmp[j-1]>x){tmp[j]=tmp[j-1];--j;}tmp[j]=x;}
    unsigned pos=(unsigned)((double)(n-1)*0.99+0.5);if(pos>=n)pos=n-1;return tmp[pos];
}
static double p99Entry(const Handler& h){
    unsigned n=h.sampleCount<ENTRY_SAMPLES?h.sampleCount:ENTRY_SAMPLES;if(!n)return 0;double tmp[ENTRY_SAMPLES];for(unsigned i=0;i<n;++i)tmp[i]=h.samples[i];for(unsigned i=1;i<n;++i){double x=tmp[i];unsigned j=i;while(j>0&&tmp[j-1]>x){tmp[j]=tmp[j-1];--j;}tmp[j]=x;}unsigned pos=(unsigned)((double)(n-1)*0.99+0.5);if(pos>=n)pos=n-1;return tmp[pos];
}

static void clearLightWindowAccumulators(){
    g_frames=0;g_totalFrameTicks=0;g_peakFrameTicks=0;g_totalMeasuredTicks=0;g_wrapperOverheadTicks=0;g_boundaryOverheadTicks=0;
    if(g_handlers)for(unsigned i=0;i<g_handlerCount;++i){Handler& h=g_handlers[i];h.totalTicks=0;h.totalCalls=0;h.peakTicks=0;h.sampleCount=0;h.samplePos=0;for(unsigned j=0;j<ENTRY_SAMPLES;++j)h.samples[j]=0;}
    if(g_addons)for(unsigned i=0;i<g_addonCount;++i){AddonStat& a=g_addons[i];a.totalTicks=0;a.totalCalls=0;a.peakFrameTicks=0;a.sampleCount=0;a.samplePos=0;a.longFrameHits=0;a.longFrameContributionHits=0;for(int k=0;k<3;++k){a.totalKindTicks[k]=0;a.totalKindCalls[k]=0;}for(unsigned j=0;j<ADDON_SAMPLES;++j)a.samples[j]=0;}
    if(g_files)for(unsigned i=0;i<g_fileCount;++i){FileStat& f=g_files[i];f.totalTicks=0;f.totalCalls=0;f.peakFrameTicks=0;}
    resetCurrent();
}
static bool publishLightWindow(long long now,bool allowPartial){
    if(!g_running||g_deepIsolation||!g_windowStartTicks||now<=g_windowStartTicks)return false;
    double sec=ticksSec(now-g_windowStartTicks);if(!allowPartial&&sec<1.0)return false;if(g_frames==0)return false;
    g_publishedFrames=g_frames;g_publishedFrameTicks=g_totalFrameTicks;g_publishedPeakFrameTicks=g_peakFrameTicks;g_publishedMeasuredTicks=g_totalMeasuredTicks;g_publishedWrapperOverheadTicks=g_wrapperOverheadTicks;g_publishedBoundaryOverheadTicks=g_boundaryOverheadTicks;g_publishedWindowSeconds=sec;g_havePublishedWindow=true;
    if(g_handlers)for(unsigned i=0;i<g_handlerCount;++i){Handler& h=g_handlers[i];h.publishedTicks=h.totalTicks;h.publishedCalls=h.totalCalls;h.publishedPeakTicks=h.peakTicks;h.publishedP99Ms=p99Entry(h);}
    if(g_addons)for(unsigned i=0;i<g_addonCount;++i){AddonStat& a=g_addons[i];a.publishedTicks=a.totalTicks;a.publishedCalls=a.totalCalls;a.publishedPeakFrameTicks=a.peakFrameTicks;a.publishedP99Ms=p99(a.samples,a.sampleCount,ADDON_SAMPLES);a.publishedLongFrameHits=a.longFrameHits;a.publishedLongFrameContributionHits=a.longFrameContributionHits;for(int k=0;k<3;++k){a.publishedKindTicks[k]=a.totalKindTicks[k];a.publishedKindCalls[k]=a.totalKindCalls[k];}}
    if(g_files)for(unsigned i=0;i<g_fileCount;++i){FileStat& f=g_files[i];f.publishedTicks=f.totalTicks;f.publishedCalls=f.totalCalls;f.publishedPeakFrameTicks=f.peakFrameTicks;}
    clearLightWindowAccumulators();g_windowStartTicks=now;return true;
}
static void discardUnfinishedLightWindow(long long now){clearLightWindowAccumulators();g_windowStartTicks=now;}
static double currentLightWindowSeconds(){long long n=nowTicks();if(!g_windowStartTicks||n<=g_windowStartTicks)return 0;return ticksSec(n-g_windowStartTicks);}
static double selectedLightWindowSeconds(){return g_havePublishedWindow?g_publishedWindowSeconds:currentLightWindowSeconds();}
static void stamp(char* out,unsigned cap){SYSTEMTIME t={};GetLocalTime(&t);if(cap<13){if(cap)out[0]=0;return;}unsigned h=t.wHour,m=t.wMinute,s=t.wSecond,ms=t.wMilliseconds;out[0]=(char)('0'+h/10);out[1]=(char)('0'+h%10);out[2]=':';out[3]=(char)('0'+m/10);out[4]=(char)('0'+m%10);out[5]=':';out[6]=(char)('0'+s/10);out[7]=(char)('0'+s%10);out[8]='.';out[9]=(char)('0'+(ms/100)%10);out[10]=(char)('0'+(ms/10)%10);out[11]=(char)('0'+ms%10);out[12]=0;}

// Lua 5.0.3 debug ABI used by the 1.12 client.
struct LuaDebug {
    int event;const char* name;const char* namewhat;const char* what;const char* source;int currentline;int nups;int linedefined;char short_src[60];int i_ci;
};
using HookFn=void (__fastcall *)(Lua50::State,LuaDebug*);
using LuaHook=HookFn;
using GetInfoFn=int (__fastcall *)(Lua50::State,const char*,LuaDebug*);
using SetHookFn=int (__fastcall *)(Lua50::State,LuaHook,int,int);
static const unsigned long PROFILER_LUA_GETINFO=0x006FBC70UL; // known-good LuaDebug.cpp
static bool getInfo(Lua50::State L,const char* what,LuaDebug* ar){return ((GetInfoFn)PROFILER_LUA_GETINFO)(L,what,ar)!=0;}
static int setHook(Lua50::State L,LuaHook h,int mask,int count){return ((SetHookFn)WoW112::LUA_SEThOOK)(L,h,mask,count);}
static void* stateHook(Lua50::State L){return L?*(void**)((unsigned char*)L+WoW112::OFF_LUA_HOOK_PTR):0;}
static unsigned stateHookMask(Lua50::State L){return L?(unsigned)*((unsigned char*)L+WoW112::OFF_LUA_HOOK_MASK):0;}

struct DeepAgg {
    long long totalTicks;unsigned long totalCalls;
    long long windowTicks;unsigned long windowCalls;
    long long lastTicks;unsigned long lastCalls;
    long long currentFrameTicks;
    long long windowPeakFrameTicks;long long lastPeakFrameTicks;
    long long windowKindTicks[3];long long lastKindTicks[3];
    unsigned long windowKindCalls[3];unsigned long lastKindCalls[3];
    double windowSamples[ADDON_SAMPLES];double lastSamples[ADDON_SAMPLES];
    unsigned windowSampleCount;unsigned windowSamplePos;unsigned lastSampleCount;
};
struct DeepAddon {char name[96];DeepAgg s;};
struct DeepFile {
    char addon[96];char file[192];
    long long totalTicks;unsigned long totalCalls;
    long long windowTicks;unsigned long windowCalls;
    long long lastTicks;unsigned long lastCalls;
    long long currentFrameTicks;long long windowPeakFrameTicks;long long lastPeakFrameTicks;
};
struct DeepFunc {
    char addon[96];char key[320];char file[192];char name[128];int line;bool cApi;
    long long totalTicks;unsigned long calls;long long currentFrameTicks;
};
struct DeepStack {
    long long started;long long hookTicksAtStart;long long childAdjustedTicks;
    char addon[96];char file[192];char name[128];int line;unsigned context;
    bool attributed;bool system;bool profilerApi;
};
struct DeepLong {char time[32];double frameMs;double luaMs;char addon[96];char file[192];};

static DeepAddon* g_deepAddons=0;static unsigned g_deepAddonCount=0;
static DeepFile* g_deepFiles=0;static unsigned g_deepFileCount=0;
static DeepFunc* g_deepFuncs=0;static unsigned g_deepFuncCount=0;
static DeepStack g_deepStack[MAX_DEEP_STACK];static unsigned g_deepDepth=0;
static DeepLong g_deepLong[MAX_LONGFRAMES];static unsigned g_deepLongCount=0;
static bool g_deepRunning=false,g_deepScopeActive=false,g_deepSuppressHook=false;
static char g_deepTarget[96]={0};static int g_deepTargetIndex=-1;
static double g_deepSampleIntervalMs=50.0,g_deepThresholdMs=50.0;
static long long g_deepStartTicks=0,g_deepNextTicks=0,g_deepScopeStart=0,g_deepSampledTicks=0;
static long long g_deepLastFrameBoundary=0,g_deepWindowStart=0;
static unsigned long g_deepWindowFrames=0,g_deepLastFrames=0;
static long long g_deepWindowFrameTicks=0,g_deepLastFrameTicks=0,g_deepWindowPeakFrameTicks=0,g_deepLastPeakFrameTicks=0;
static long long g_deepWindowLuaTicks=0,g_deepLastLuaTicks=0,g_deepWindowAttributedTicks=0,g_deepLastAttributedTicks=0;
static long long g_deepWindowSystemTicks=0,g_deepLastSystemTicks=0,g_deepWindowUnattributedTicks=0,g_deepLastUnattributedTicks=0;
static double g_deepLastWindowSeconds=0.0;
static long long g_deepHookOverheadTicks=0,g_deepExplicitOverheadTicks=0,g_deepWindowHookBase=0,g_deepWindowExplicitBase=0,g_deepLastHookOverheadTicks=0,g_deepLastExplicitOverheadTicks=0;
static unsigned long g_deepSampledScopes=0,g_deepSkippedScopes=0,g_deepHookBusySkips=0,g_deepAbortedScopes=0;
static bool g_deepFunctionOverflow=false;
static Lua50::State g_deepState=0;
static unsigned g_deepMaxDepth=0;
static double g_deepLuaMemoryKb=-1.0,g_deepLastLuaMemoryKb=-1.0,g_deepAllocRateKbSec=0.0;
static long long g_deepMemorySampleQpc=0,g_deepLastGcQpc=0;static unsigned long g_deepGcDetected=0;

static bool ensureDeepStorage(){
    if(!ensureHeap())return false;
    if(!g_deepAddons)g_deepAddons=(DeepAddon*)allocz(sizeof(DeepAddon)*MAX_ADDONS);
    if(!g_deepFiles)g_deepFiles=(DeepFile*)allocz(sizeof(DeepFile)*MAX_FILES);
    if(!g_deepFuncs)g_deepFuncs=(DeepFunc*)allocz(sizeof(DeepFunc)*MAX_DEEP_FUNCS);
    return g_deepAddons&&g_deepFiles&&g_deepFuncs;
}
static void freeDeepStorage(){void* p=g_deepAddons;freep(p);g_deepAddons=(DeepAddon*)p;p=g_deepFiles;freep(p);g_deepFiles=(DeepFile*)p;p=g_deepFuncs;freep(p);g_deepFuncs=(DeepFunc*)p;}
static void clearDeepWindow(){
    g_deepWindowFrames=0;g_deepWindowFrameTicks=0;g_deepWindowPeakFrameTicks=0;g_deepWindowLuaTicks=0;g_deepWindowAttributedTicks=0;g_deepWindowSystemTicks=0;g_deepWindowUnattributedTicks=0;
    if(g_deepAddons)for(unsigned i=0;i<g_deepAddonCount;++i){DeepAgg& a=g_deepAddons[i].s;a.windowTicks=0;a.windowCalls=0;a.currentFrameTicks=0;a.windowPeakFrameTicks=0;a.windowSampleCount=0;a.windowSamplePos=0;for(int k=0;k<3;++k){a.windowKindTicks[k]=0;a.windowKindCalls[k]=0;}for(unsigned j=0;j<ADDON_SAMPLES;++j)a.windowSamples[j]=0;}
    if(g_deepFiles)for(unsigned i=0;i<g_deepFileCount;++i){DeepFile& f=g_deepFiles[i];f.windowTicks=0;f.windowCalls=0;f.currentFrameTicks=0;f.windowPeakFrameTicks=0;}
    if(g_deepFuncs)for(unsigned i=0;i<g_deepFuncCount;++i)g_deepFuncs[i].currentFrameTicks=0;
}
static void resetDeepData(bool freeStorage){
    g_deepRunning=false;g_deepScopeActive=false;g_deepSuppressHook=false;g_deepState=0;g_deepTarget[0]=0;g_deepTargetIndex=-1;g_deepDepth=0;g_deepMaxDepth=0;
    g_deepAddonCount=0;g_deepFileCount=0;g_deepFuncCount=0;g_deepLongCount=0;g_deepStartTicks=0;g_deepNextTicks=0;g_deepScopeStart=0;g_deepSampledTicks=0;
    g_deepLastFrameBoundary=0;g_deepWindowStart=0;g_deepWindowFrames=0;g_deepLastFrames=0;g_deepWindowFrameTicks=0;g_deepLastFrameTicks=0;g_deepWindowPeakFrameTicks=0;g_deepLastPeakFrameTicks=0;
    g_deepWindowLuaTicks=0;g_deepLastLuaTicks=0;g_deepWindowAttributedTicks=0;g_deepLastAttributedTicks=0;g_deepWindowSystemTicks=0;g_deepLastSystemTicks=0;g_deepWindowUnattributedTicks=0;g_deepLastUnattributedTicks=0;g_deepLastWindowSeconds=0;
    g_deepHookOverheadTicks=0;g_deepExplicitOverheadTicks=0;g_deepWindowHookBase=0;g_deepWindowExplicitBase=0;g_deepLastHookOverheadTicks=0;g_deepLastExplicitOverheadTicks=0;
    g_deepSampledScopes=0;g_deepSkippedScopes=0;g_deepHookBusySkips=0;g_deepAbortedScopes=0;g_deepFunctionOverflow=false;
    g_deepLuaMemoryKb=-1.0;g_deepLastLuaMemoryKb=-1.0;g_deepAllocRateKbSec=0;g_deepMemorySampleQpc=0;g_deepLastGcQpc=0;g_deepGcDetected=0;
    for(unsigned i=0;i<MAX_DEEP_STACK;++i)zeroBytes(&g_deepStack[i],sizeof(DeepStack));for(unsigned i=0;i<MAX_LONGFRAMES;++i)zeroBytes(&g_deepLong[i],sizeof(DeepLong));
    if(g_deepAddons)zeroBytes(g_deepAddons,sizeof(DeepAddon)*MAX_ADDONS);if(g_deepFiles)zeroBytes(g_deepFiles,sizeof(DeepFile)*MAX_FILES);if(g_deepFuncs)zeroBytes(g_deepFuncs,sizeof(DeepFunc)*MAX_DEEP_FUNCS);
    if(freeStorage)freeDeepStorage();
}
static void setIsolation(bool on){long long n=nowTicks();g_deepIsolation=on;discardUnfinishedLightWindow(n);g_lastFrameTicks=0;}
static int findDeepAddon(const char* name){if(!g_deepAddons||!name)return -1;for(unsigned i=0;i<g_deepAddonCount;++i)if(eq(g_deepAddons[i].name,name))return (int)i;return -1;}
static int findOrAddDeepAddon(const char* name){int x=findDeepAddon(name);if(x>=0)return x;if(!g_deepAddons||g_deepAddonCount>=MAX_ADDONS)return -1;DeepAddon& a=g_deepAddons[g_deepAddonCount];zeroBytes(&a,sizeof(a));cpy(a.name,sizeof(a.name),name);return (int)g_deepAddonCount++;}
static int findOrAddDeepFile(const char* addon,const char* file){if(!g_deepFiles)return -1;for(unsigned i=0;i<g_deepFileCount;++i)if(eq(g_deepFiles[i].addon,addon)&&eq(g_deepFiles[i].file,file))return (int)i;if(g_deepFileCount>=MAX_FILES)return -1;DeepFile& f=g_deepFiles[g_deepFileCount];zeroBytes(&f,sizeof(f));cpy(f.addon,sizeof(f.addon),addon);cpy(f.file,sizeof(f.file),file);return (int)g_deepFileCount++;}
static int findDeepFunc(const char* addon,const char* file,int line,const char* name){for(unsigned i=0;i<g_deepFuncCount;++i){DeepFunc& f=g_deepFuncs[i];if(f.line==line&&eq(f.addon,addon)&&eq(f.file,file)&&eq(f.name,name))return (int)i;}return -1;}
static void appendInt(char* dst,unsigned cap,int line){unsigned n=slen(dst);if(n+1>=cap)return;char num[24]={0};int v=line,nn=0;bool neg=v<0;if(neg)v=-v;do{num[nn++]=(char)('0'+v%10);v/=10;}while(v&&nn<20);if(neg)num[nn++]='-';for(int i=nn-1;i>=0&&n+1<cap;--i)dst[n++]=num[i];dst[n]=0;}
static int findOrAddDeepFunc(const char* addon,const char* file,int line,const char* name){int x=findDeepFunc(addon,file,line,name);if(x>=0)return x;if(!g_deepFuncs)return -1;if(g_deepFuncCount>=MAX_DEEP_FUNCS){g_deepFunctionOverflow=true;return -1;}DeepFunc& f=g_deepFuncs[g_deepFuncCount];zeroBytes(&f,sizeof(f));cpy(f.addon,sizeof(f.addon),addon);cpy(f.file,sizeof(f.file),file&&*file?file:"[unknown]");cpy(f.name,sizeof(f.name),name&&*name?name:"<anonymous>");f.line=line;f.cApi=line<0;cpy(f.key,sizeof(f.key),f.file);unsigned n=slen(f.key);if(n+1<sizeof(f.key)){f.key[n++]='|';f.key[n]=0;}appendInt(f.key,sizeof(f.key),line);n=slen(f.key);if(n+1<sizeof(f.key)){f.key[n++]='|';f.key[n]=0;}for(unsigned i=0;f.name[i]&&n+1<sizeof(f.key);++i)f.key[n++]=f.name[i];f.key[n]=0;return (int)g_deepFuncCount++;}
static bool deepSourceIdentity(const char* raw,const DeepStack* parent,char* addon,unsigned acap,char* file,unsigned fcap,bool* system){
    if(addon&&acap)addon[0]=0;if(file&&fcap)file[0]=0;if(system)*system=false;char src[512]={0};normalizeCopy(raw,src,sizeof(src));
    if(sourceParts(raw,addon,acap,file,fcap)){if(system&&eq(addon,"Blizzard"))*system=true;return true;}
    if(parent&&parent->attributed){cpy(addon,acap,parent->addon);cpy(file,fcap,parent->file);if(system)*system=parent->system;return true;}
    if(src[0]&&!eq(src,"=[C]")&&!eq(src,"[C]")){cpy(addon,acap,"Blizzard");cpy(file,fcap,src);if(system)*system=true;return true;}
    return false;
}
static unsigned classifyRootContext(Lua50::State L){int top=Lua50::GetTop(L);unsigned result=0;Lua50::GetGlobal(L,"event");if(Lua50::Type(L,-1)==4){const char* e=Lua50::ToString(L,-1);if(e&&*e)result=2;}Lua50::SetTop(L,top);if(!result){Lua50::GetGlobal(L,"arg1");if(Lua50::IsNumber(L,-1))result=1;Lua50::SetTop(L,top);}return result;}
static void addDeepAgg(DeepAgg& a,long long ticks,unsigned context){a.totalTicks+=ticks;a.totalCalls++;a.windowTicks+=ticks;a.windowCalls++;a.currentFrameTicks+=ticks;unsigned k=context<3?context:0;a.windowKindTicks[k]+=ticks;a.windowKindCalls[k]++;}
static void addDeepFileStat(DeepFile& f,long long ticks){f.totalTicks+=ticks;f.totalCalls++;f.windowTicks+=ticks;f.windowCalls++;f.currentFrameTicks+=ticks;}
static void markDeepProfilerApi(){if(g_deepDepth)g_deepStack[g_deepDepth-1].profilerApi=true;}
static void __fastcall deepHook(Lua50::State L,LuaDebug* ar){
    const long long hookEntered=nowTicks();
    if(!g_deepRunning||g_deepSuppressHook||!g_deepScopeActive||L!=g_deepState||!ar)return;
    if(ar->event==0){
        if(g_deepDepth>=MAX_DEEP_STACK)return;const DeepStack* parent=g_deepDepth?&g_deepStack[g_deepDepth-1]:0;LuaDebug info=*ar;getInfo(L,"Snl",&info);
        DeepStack& st=g_deepStack[g_deepDepth++];zeroBytes(&st,sizeof(st));bool sys=false;st.attributed=deepSourceIdentity(info.source,parent,st.addon,sizeof(st.addon),st.file,sizeof(st.file),&sys);st.system=sys;cpy(st.name,sizeof(st.name),info.name&&*info.name?info.name:"<anonymous>");st.line=info.linedefined;st.context=parent?parent->context:classifyRootContext(L);
        const long long hookLeaving=nowTicks();const long long cost=hookLeaving>=hookEntered?hookLeaving-hookEntered:0;g_deepHookOverheadTicks+=cost;st.started=hookLeaving;st.hookTicksAtStart=g_deepHookOverheadTicks;if(g_deepDepth>g_deepMaxDepth)g_deepMaxDepth=g_deepDepth;return;
    }
    if(ar->event==1||ar->event==4){
        if(!g_deepDepth){long long leaving=nowTicks();if(leaving>=hookEntered)g_deepHookOverheadTicks+=leaving-hookEntered;return;}
        DeepStack st=g_deepStack[--g_deepDepth];const long long raw=hookEntered>st.started?hookEntered-st.started:0;const long long nested=g_deepHookOverheadTicks>=st.hookTicksAtStart?g_deepHookOverheadTicks-st.hookTicksAtStart:0;const long long inclusive=raw>nested?raw-nested:0;const long long exclusive=inclusive>st.childAdjustedTicks?inclusive-st.childAdjustedTicks:0;if(g_deepDepth)g_deepStack[g_deepDepth-1].childAdjustedTicks+=inclusive;
        if(st.profilerApi)g_deepExplicitOverheadTicks+=inclusive;else{g_deepWindowLuaTicks+=exclusive;if(st.attributed&&!st.system){g_deepWindowAttributedTicks+=exclusive;int ai=findOrAddDeepAddon(st.addon);int fi=findOrAddDeepFile(st.addon,st.file);if(ai>=0)addDeepAgg(g_deepAddons[ai].s,exclusive,st.context);if(fi>=0)addDeepFileStat(g_deepFiles[fi],exclusive);int fun=findOrAddDeepFunc(st.addon,st.file,st.line,st.name);if(fun>=0){g_deepFuncs[fun].totalTicks+=exclusive;g_deepFuncs[fun].calls++;g_deepFuncs[fun].currentFrameTicks+=exclusive;}}else if(st.attributed&&st.system)g_deepWindowSystemTicks+=exclusive;else g_deepWindowUnattributedTicks+=exclusive;}
        const long long hookLeaving=nowTicks();if(hookLeaving>=hookEntered)g_deepHookOverheadTicks+=hookLeaving-hookEntered;return;
    }
    const long long hookLeaving=nowTicks();if(hookLeaving>=hookEntered)g_deepHookOverheadTicks+=hookLeaving-hookEntered;
}
static bool currentHookBusy(Lua50::State L){void* h=stateHook(L);return h&&h!=(void*)&deepHook;}
static void forceUnhookCurrent(Lua50::State L){if(L&&stateHook(L)==(void*)&deepHook)setHook(L,0,0,0);}
static void forceEndDeepScope(Lua50::State L,bool aborted){if(!g_deepScopeActive)return;if(L&&stateHook(L)==(void*)&deepHook)setHook(L,0,0,0);long long n=nowTicks();if(g_deepScopeStart&&n>g_deepScopeStart)g_deepSampledTicks+=n-g_deepScopeStart;if(aborted)g_deepAbortedScopes++;g_deepScopeStart=0;g_deepScopeActive=false;g_deepDepth=0;g_deepState=0;}
static bool beginDeepScope(Lua50::State L,unsigned addonIndex){
    if(!g_deepRunning||g_deepTargetIndex<0||(int)addonIndex!=g_deepTargetIndex||!L)return false;if(g_deepScopeActive)return false;long long now=nowTicks();if(!now)return false;if(g_deepNextTicks>0&&now<g_deepNextTicks){g_deepSkippedScopes++;return false;}g_deepNextTicks=now+(long long)((g_deepSampleIntervalMs/1000.0)*(double)g_freq);
    if(currentHookBusy(L)){g_deepHookBusySkips++;return false;}g_deepDepth=0;const int wanted=3;setHook(L,&deepHook,wanted,0);if(stateHook(L)!=(void*)&deepHook||(stateHookMask(L)&wanted)!=wanted){if(stateHook(L)==(void*)&deepHook)setHook(L,0,0,0);g_deepHookBusySkips++;return false;}g_deepState=L;g_deepScopeActive=true;g_deepScopeStart=nowTicks();g_deepSampledScopes++;return true;
}
static void endDeepScope(Lua50::State L,bool wasActive){if(wasActive)forceEndDeepScope(L,false);}

static int __fastcall wrapper(Lua50::State L){
    const int argc=Lua50::GetTop(L);
    unsigned token=0;if(Lua50::IsNumber(L,WoW112::LUA_UPVALUE2))token=(unsigned)Lua50::ToNumber(L,WoW112::LUA_UPVALUE2);
    const unsigned handlerIndex=token>0?token-1:0xFFFFFFFFu;
    const bool valid=g_handlers&&token>0&&handlerIndex<g_handlerCount;
    if(!g_running||!valid){
        Lua50::PushValue(L,WoW112::LUA_UPVALUE1);Lua50::Insert(L,1);Lua50::Call(L,argc,-1);return Lua50::GetTop(L);
    }
    const unsigned generation=g_generation;
    const unsigned addonIndex=g_handlers[handlerIndex].addonIndex;
    const unsigned fileIndex=g_handlers[handlerIndex].fileIndex;
    const unsigned kind=g_handlers[handlerIndex].kind;
    const bool deepIsolation=g_deepIsolation;
    const bool deepCandidate=(g_deepRunning&&g_deepTargetIndex>=0&&(int)addonIndex==g_deepTargetIndex);
    const bool deepScope=deepCandidate?beginDeepScope(L,addonIndex):false;
    const long long bookkeepingStart=nowTicks();
    Lua50::PushValue(L,WoW112::LUA_UPVALUE1);Lua50::Insert(L,1);
    const long long callStart=nowTicks();
    Lua50::Call(L,argc,-1);
    const long long callEnd=nowTicks();
    if(deepScope)endDeepScope(L,true);
    long long elapsed=callEnd>=callStart?callEnd-callStart:0;
    const long long bookkeepingEnd=nowTicks();
    if(!deepIsolation&&generation==g_generation&&g_running&&g_handlers&&handlerIndex<g_handlerCount){
        Handler& h=g_handlers[handlerIndex];
        h.currentTicks+=elapsed;h.currentCalls++;
        if(addonIndex<g_addonCount){AddonStat& a=g_addons[addonIndex];a.currentTicks+=elapsed;a.currentCalls++;unsigned k=kind<3?kind:0;a.currentKindTicks[k]+=elapsed;a.currentKindCalls[k]++;}
        if(fileIndex<g_fileCount){g_files[fileIndex].currentTicks+=elapsed;g_files[fileIndex].currentCalls++;}
        const long long pre=callStart>=bookkeepingStart?callStart-bookkeepingStart:0;
        const long long post=bookkeepingEnd>=callEnd?bookkeepingEnd-callEnd:0;
        g_wrapperOverheadTicks+=pre+post;
    }
    return Lua50::GetTop(L);
}

static void recordLongFrame(double frameMs,double luaMs){
    g_totalLongFrameCount++;if(g_longCount<MAX_LONGFRAMES)g_longCount++;for(unsigned i=g_longCount-1;i>0;--i)g_longFrames[i]=g_longFrames[i-1];LongFrame& r=g_longFrames[0];zeroBytes(&r,sizeof(r));stamp(r.time,sizeof(r.time));r.frameMs=frameMs;r.luaMs=luaMs;r.measuredLuaPct=percent(luaMs,frameMs);r.unmeasuredMs=frameMs>luaMs?frameMs-luaMs:0;
    int chosen[MAX_CONTRIB]={-1,-1,-1,-1,-1};for(unsigned slot=0;slot<MAX_CONTRIB;++slot){long long best=0;int bi=-1;for(unsigned i=0;i<g_addonCount;++i){bool used=false;for(unsigned j=0;j<slot;++j)if(chosen[j]==(int)i)used=true;if(!used&&g_addons[i].currentTicks>best){best=g_addons[i].currentTicks;bi=(int)i;}}if(bi<0||best<=0)break;chosen[slot]=bi;AddonStat& a=g_addons[bi];Contributor& c=r.contributors[r.contributorCount++];cpy(c.addon,sizeof(c.addon),a.name);c.ms=ticksMs(a.currentTicks);c.pct=percent(c.ms,frameMs);c.level=contributionLevel(c.ms,c.pct);long long fb=0;const char* ff="";for(unsigned fi=0;fi<g_fileCount;++fi)if(eq(g_files[fi].addon,a.name)&&g_files[fi].currentTicks>fb){fb=g_files[fi].currentTicks;ff=g_files[fi].file;}cpy(c.file,sizeof(c.file),ff);}
    if(r.contributorCount){Contributor& c=r.contributors[0];cpy(r.addon,sizeof(r.addon),c.addon);cpy(r.file,sizeof(r.file),c.file);r.associatedAddonMs=c.ms;r.associatedAddonPct=c.pct;r.level=c.level;int ai=findAddon(c.addon);if(ai>=0){AddonStat& a=g_addons[ai];a.longFrameHits++;a.totalLongFrameHits++;if(c.level>=1){a.longFrameContributionHits++;a.totalLongFrameContributionHits++;}}}
}

static void finalizeDeepWindow(long long now){
    if(!g_deepWindowStart){g_deepWindowStart=now;return;}
    if(g_freq<=0||now<=g_deepWindowStart)return;long long elapsed=now-g_deepWindowStart;if(elapsed<g_freq)return;
    g_deepLastWindowSeconds=ticksSec(elapsed);g_deepLastFrames=g_deepWindowFrames;g_deepLastFrameTicks=g_deepWindowFrameTicks;g_deepLastPeakFrameTicks=g_deepWindowPeakFrameTicks;g_deepLastLuaTicks=g_deepWindowLuaTicks;g_deepLastAttributedTicks=g_deepWindowAttributedTicks;g_deepLastSystemTicks=g_deepWindowSystemTicks;g_deepLastUnattributedTicks=g_deepWindowUnattributedTicks;
    g_deepLastHookOverheadTicks=g_deepHookOverheadTicks-g_deepWindowHookBase;g_deepLastExplicitOverheadTicks=g_deepExplicitOverheadTicks-g_deepWindowExplicitBase;
    if(g_deepAddons)for(unsigned i=0;i<g_deepAddonCount;++i){DeepAgg& a=g_deepAddons[i].s;a.lastTicks=a.windowTicks;a.lastCalls=a.windowCalls;a.lastPeakFrameTicks=a.windowPeakFrameTicks;a.lastSampleCount=a.windowSampleCount;for(unsigned j=0;j<ADDON_SAMPLES;++j)a.lastSamples[j]=a.windowSamples[j];for(int k=0;k<3;++k){a.lastKindTicks[k]=a.windowKindTicks[k];a.lastKindCalls[k]=a.windowKindCalls[k];a.windowKindTicks[k]=0;a.windowKindCalls[k]=0;}a.windowTicks=0;a.windowCalls=0;a.windowPeakFrameTicks=0;a.windowSampleCount=0;a.windowSamplePos=0;for(unsigned j=0;j<ADDON_SAMPLES;++j)a.windowSamples[j]=0;}
    if(g_deepFiles)for(unsigned i=0;i<g_deepFileCount;++i){DeepFile& f=g_deepFiles[i];f.lastTicks=f.windowTicks;f.lastCalls=f.windowCalls;f.lastPeakFrameTicks=f.windowPeakFrameTicks;f.windowTicks=0;f.windowCalls=0;f.windowPeakFrameTicks=0;}
    g_deepWindowFrames=0;g_deepWindowFrameTicks=0;g_deepWindowPeakFrameTicks=0;g_deepWindowLuaTicks=0;g_deepWindowAttributedTicks=0;g_deepWindowSystemTicks=0;g_deepWindowUnattributedTicks=0;g_deepWindowHookBase=g_deepHookOverheadTicks;g_deepWindowExplicitBase=g_deepExplicitOverheadTicks;g_deepWindowStart=now;
}
static void recordDeepLongFrame(double frameMs,double luaMs,const char* addon,const char* file){if(g_deepLongCount<MAX_LONGFRAMES)g_deepLongCount++;for(unsigned i=g_deepLongCount-1;i>0;--i)g_deepLong[i]=g_deepLong[i-1];DeepLong& d=g_deepLong[0];zeroBytes(&d,sizeof(d));stamp(d.time,sizeof(d.time));d.frameMs=frameMs;d.luaMs=luaMs;cpy(d.addon,sizeof(d.addon),addon&&*addon?addon:"未归属/非Lua");cpy(d.file,sizeof(d.file),file?file:"");}
static void onDeepFrameBoundary(long long now){
    if(g_deepScopeActive)forceEndDeepScope(g_deepState,true);if(!g_deepRunning||g_freq<=0)return;
    const long long workStart=now;long long interval=0;if(g_deepLastFrameBoundary>0&&now>g_deepLastFrameBoundary)interval=now-g_deepLastFrameBoundary;g_deepLastFrameBoundary=now;g_deepWindowFrames++;if(interval>0){g_deepWindowFrameTicks+=interval;if(interval>g_deepWindowPeakFrameTicks)g_deepWindowPeakFrameTicks=interval;}
    long long frameLua=0,topTicks=0;const char* topAddon="";const char* topFile="";
    if(g_deepAddons)for(unsigned i=0;i<g_deepAddonCount;++i){DeepAgg& a=g_deepAddons[i].s;frameLua+=a.currentFrameTicks;if(a.currentFrameTicks>0){a.windowSamples[a.windowSamplePos++%ADDON_SAMPLES]=ticksMs(a.currentFrameTicks);if(a.windowSampleCount<ADDON_SAMPLES)a.windowSampleCount++;if(a.currentFrameTicks>a.windowPeakFrameTicks)a.windowPeakFrameTicks=a.currentFrameTicks;if(a.currentFrameTicks>topTicks){topTicks=a.currentFrameTicks;topAddon=g_deepAddons[i].name;}}}
    if(topAddon&&*topAddon&&g_deepFiles){long long best=0;for(unsigned i=0;i<g_deepFileCount;++i)if(eq(g_deepFiles[i].addon,topAddon)&&g_deepFiles[i].currentFrameTicks>best){best=g_deepFiles[i].currentFrameTicks;topFile=g_deepFiles[i].file;}}
    if(interval>0&&ticksMs(interval)>=g_deepThresholdMs)recordDeepLongFrame(ticksMs(interval),ticksMs(frameLua),topAddon,topFile);
    if(g_deepAddons)for(unsigned i=0;i<g_deepAddonCount;++i)g_deepAddons[i].s.currentFrameTicks=0;
    if(g_deepFiles)for(unsigned i=0;i<g_deepFileCount;++i){DeepFile& f=g_deepFiles[i];if(f.currentFrameTicks>f.windowPeakFrameTicks)f.windowPeakFrameTicks=f.currentFrameTicks;f.currentFrameTicks=0;}
    if(g_deepFuncs)for(unsigned i=0;i<g_deepFuncCount;++i)g_deepFuncs[i].currentFrameTicks=0;
    finalizeDeepWindow(now);long long done=nowTicks();if(done>workStart)g_deepExplicitOverheadTicks+=done-workStart;
}

void onFrameBoundary(){
    long long overheadStart=nowTicks();if(!overheadStart)return;onDeepFrameBoundary(overheadStart);
    long long now=overheadStart;
    if(!g_running){g_lastFrameTicks=now;return;}if(!g_lastFrameTicks){g_lastFrameTicks=now;resetCurrent();return;}long long frame=now-g_lastFrameTicks;g_lastFrameTicks=now;if(frame<0)frame=0;
    if(g_deepIsolation){resetCurrent();return;}
    long long measured=0;for(unsigned i=0;i<g_addonCount;++i)measured+=g_addons[i].currentTicks;g_frames++;g_totalFrameTicks+=frame;if(frame>g_peakFrameTicks)g_peakFrameTicks=frame;g_totalMeasuredTicks+=measured;
    double frameMs=ticksMs(frame);double measuredMs=ticksMs(measured);
    if(g_handlers)for(unsigned i=0;i<g_handlerCount;++i){Handler& h=g_handlers[i];h.totalTicks+=h.currentTicks;h.totalCalls+=h.currentCalls;h.peakTicks=h.currentTicks>h.peakTicks?h.currentTicks:h.peakTicks;if(h.currentTicks>0){h.samples[h.samplePos++%ENTRY_SAMPLES]=ticksMs(h.currentTicks);if(h.sampleCount<ENTRY_SAMPLES)h.sampleCount++;}}
    for(unsigned i=0;i<g_addonCount;++i){AddonStat& a=g_addons[i];a.totalTicks+=a.currentTicks;a.totalCalls+=a.currentCalls;if(a.currentTicks>a.peakFrameTicks)a.peakFrameTicks=a.currentTicks;for(int k=0;k<3;++k){a.totalKindTicks[k]+=a.currentKindTicks[k];a.totalKindCalls[k]+=a.currentKindCalls[k];}if(a.currentTicks>0){a.samples[a.samplePos++%ADDON_SAMPLES]=ticksMs(a.currentTicks);if(a.sampleCount<ADDON_SAMPLES)a.sampleCount++;}}
    if(g_files)for(unsigned i=0;i<g_fileCount;++i){FileStat& f=g_files[i];f.totalTicks+=f.currentTicks;f.totalCalls+=f.currentCalls;if(f.currentTicks>f.peakFrameTicks)f.peakFrameTicks=f.currentTicks;}
    if(frameMs>=g_longThresholdMs)recordLongFrame(frameMs,measuredMs);resetCurrent();long long done=nowTicks();if(done>overheadStart)g_boundaryOverheadTicks+=done-overheadStart;publishLightWindow(now,false);
}

bool needsFrameBoundary(){return g_running||g_deepRunning||g_deepScopeActive;}

static double queryLuaMemory(Lua50::State L){int top=Lua50::GetTop(L);Lua50::GetGlobal(L,"gcinfo");double v=0;if(Lua50::Type(L,-1)==WoW112::LUA_TFUNCTION){Lua50::Call(L,0,1);if(Lua50::IsNumber(L,-1))v=Lua50::ToNumber(L,-1);}Lua50::SetTop(L,top);return v;}
static void updateMemory(Lua50::State L,long long now,double* outMem){double mem=queryLuaMemory(L);if(mem<0)mem=0;if(g_lastMemoryQpc&&now>g_lastMemoryQpc){double sec=ticksSec(now-g_lastMemoryQpc);if(sec>0)g_allocRateKbSec=(mem-g_lastLuaMemoryKb)/sec;if(mem<g_lastLuaMemoryKb){g_gcDetected++;g_lastGcQpc=now;}}g_lastLuaMemoryKb=mem;g_lastMemoryQpc=now;if(outMem)*outMem=mem;}
static double sessionSeconds(){long long n=nowTicks();return g_startTicks&&n>g_startTicks?ticksSec(n-g_startTicks):0;}

static int doWrap(Lua50::State L){
    const int argc=Lua50::GetTop(L);
    if(argc<4||Lua50::Type(L,3)!=WoW112::LUA_TFUNCTION){Lua50::PushNil(L);Lua50::PushString(L,"NOT_FUNCTION");return 2;}
    if(!ensureLightStorage()||g_handlerCount>=MAX_HANDLERS){Lua50::PushNil(L);Lua50::PushString(L,"CAPACITY_EXCEEDED");return 2;}
    const char* kindText=Lua50::IsString(L,4)?Lua50::ToString(L,4):"";
    const int top=Lua50::GetTop(L);
    Lua50::PushValue(L,3);
    LuaDebug ar={};char addon[96]={0},file[192]={0};
    const bool got=getInfo(L,">S",&ar);
    const bool attributed=got?sourceParts(ar.source,addon,sizeof(addon),file,sizeof(file)):false;
    Lua50::SetTop(L,top);
    if(!attributed){Lua50::PushNil(L);Lua50::PushString(L,"UNATTRIBUTED_SOURCE");Lua50::PushString(L,file);return 3;}
    int ai=findOrAddAddon(addon),fi=findOrAddFile(addon,file);
    if(ai<0||fi<0){Lua50::PushNil(L);Lua50::PushString(L,"CAPACITY_EXCEEDED");return 2;}
    Handler& h=g_handlers[g_handlerCount];zeroBytes(&h,sizeof(h));h.generation=g_generation;h.addonIndex=(unsigned)ai;h.fileIndex=(unsigned)fi;h.kind=kindValue(kindText);h.line=ar.linedefined;
    const unsigned token=++g_handlerCount;
    Lua50::PushValue(L,3);Lua50::PushNumber(L,(double)token);Lua50::PushCClosure(L,&wrapper,2);
    Lua50::PushString(L,addon);Lua50::PushString(L,file);Lua50::PushNumber(L,(double)token);return 4;
}
static int lightStart(Lua50::State L){(void)L;if(!ensureLightStorage()||!initQpc()){Lua50::PushBool(L,false);Lua50::PushString(L,"QPC_UNAVAILABLE");return 2;}resetStatsOnly();g_running=true;Lua50::PushBool(L,true);Lua50::PushString(L,"OK");return 2;}
static int lightStop(Lua50::State L){g_running=false;Lua50::PushBool(L,true);return 1;}
static int lightReset(Lua50::State L){resetStatsOnly();Lua50::PushBool(L,true);return 1;}
static void hardResetLight(){g_running=false;g_deepIsolation=false;g_lastFrameTicks=0;g_generation++;if(!g_generation)g_generation=1;void* p=g_handlers;freep(p);g_handlers=(Handler*)p;p=g_addons;freep(p);g_addons=(AddonStat*)p;p=g_files;freep(p);g_files=(FileStat*)p;g_handlerCount=g_addonCount=g_fileCount=0;resetStatsOnly();}
static int lightHardReset(Lua50::State L){hardResetLight();Lua50::PushBool(L,true);return 1;}
static int lightStatus(Lua50::State L){Lua50::NewTable(L);setFieldString(L,"version",LIGHT_VERSION);setFieldString(L,"mode",LIGHT_STATUS_MODE);setFieldBool(L,"running",g_running);setFieldBool(L,"hookOwned",false);setFieldNumber(L,"hookMask",0);setFieldNumber(L,"wrappedHandlerCount",g_handlerCount);setFieldBool(L,"deepIsolation",g_deepIsolation);setFieldNumber(L,"sessionGeneration",g_generation);return 1;}
static int pushLightSnapshot(Lua50::State L){
    long long snapStart=nowTicks();bool pub=g_havePublishedWindow;double sec=selectedLightWindowSeconds();
    unsigned long frames=pub?g_publishedFrames:g_frames;
    long long frameTicks=pub?g_publishedFrameTicks:g_totalFrameTicks;
    long long peakFrameTicks=pub?g_publishedPeakFrameTicks:g_peakFrameTicks;
    long long measuredTicks=pub?g_publishedMeasuredTicks:g_totalMeasuredTicks;
    long long wrapperTicks=pub?g_publishedWrapperOverheadTicks:g_wrapperOverheadTicks;
    long long boundaryTicks=pub?g_publishedBoundaryOverheadTicks:g_boundaryOverheadTicks;
    double frameAvg=frames?ticksMs(frameTicks)/(double)frames:0;
    double measuredAvg=frames?ticksMs(measuredTicks)/(double)frames:0;
    double mem=0;updateMemory(L,snapStart,&mem);
    Lua50::NewTable(L);
    setFieldString(L,"version",LIGHT_VERSION);setFieldString(L,"remigrationBuild",REMIGRATION_BUILD);setFieldString(L,"mode",LIGHT_SNAPSHOT_MODE);setFieldString(L,"measurement","native-c-closure-entry-qpc");
    setFieldBool(L,"running",g_running);setFieldBool(L,"deepIsolation",g_deepIsolation);setFieldNumber(L,"sessionGeneration",g_generation);
    setFieldNumber(L,"windowSeconds",sec);setFieldNumber(L,"frames",frames);setFieldNumber(L,"frameAvgMs",frameAvg);setFieldNumber(L,"framePeakMs",ticksMs(peakFrameTicks));setFieldNumber(L,"fps",frameAvg>0?1000.0/frameAvg:0);
    setFieldNumber(L,"luaMsFrame",measuredAvg);setFieldNumber(L,"measuredLuaMsFrame",measuredAvg);setFieldNumber(L,"unattributedMsFrame",0.0);setFieldNumber(L,"coveragePct",100.0);
    setFieldNumber(L,"wrapperOverheadMsFrame",frames?ticksMs(wrapperTicks)/(double)frames:0);setFieldNumber(L,"boundaryOverheadMsFrame",frames?ticksMs(boundaryTicks)/(double)frames:0);setFieldNumber(L,"profilerOverheadMsFrame",frames?ticksMs(wrapperTicks+boundaryTicks)/(double)frames:0);setFieldNumber(L,"snapshotOverheadMs",g_lastSnapshotOverheadMs);
    setFieldNumber(L,"luaMemoryKb",mem);setFieldNumber(L,"allocationRateKbSec",g_allocRateKbSec);setFieldNumber(L,"gcDetectedCount",g_gcDetected);setFieldNumber(L,"lastGcAgeSec",g_lastGcQpc&&snapStart>=g_lastGcQpc?ticksSec(snapStart-g_lastGcQpc):-1);
    setFieldNumber(L,"longFrameThresholdMs",g_longThresholdMs);setFieldNumber(L,"longFrameRecordVersion",2);setFieldNumber(L,"longFrameContributorLimit",5);setFieldString(L,"longFrameDetailsBuild",LONG_DETAILS_BUILD);setFieldNumber(L,"longFrameContributionMinMs",LONG_MIN_MS);setFieldNumber(L,"longFrameContributionMinPct",LONG_MIN_PCT);setFieldNumber(L,"longFrameCount",g_totalLongFrameCount);setFieldNumber(L,"wrappedHandlerCount",g_handlerCount);
    Lua50::PushString(L,"addons");Lua50::NewTable(L);
    for(unsigned i=0;i<g_addonCount;++i){
        AddonStat& a=g_addons[i];long long at=pub?a.publishedTicks:a.totalTicks;long long* kt=pub?a.publishedKindTicks:a.totalKindTicks;unsigned long calls=pub?a.publishedCalls:a.totalCalls;unsigned long* kc=pub?a.publishedKindCalls:a.totalKindCalls;long long peak=pub?a.publishedPeakFrameTicks:a.peakFrameTicks;double p99ms=pub?a.publishedP99Ms:p99(a.samples,a.sampleCount,ADDON_SAMPLES);unsigned long lfh=pub?a.publishedLongFrameHits:a.longFrameHits;unsigned long lfc=pub?a.publishedLongFrameContributionHits:a.longFrameContributionHits;
        Lua50::PushNumber(L,(double)i+1);Lua50::NewTable(L);double ms=ticksMs(at),avg=frames?ms/(double)frames:0;
        setFieldString(L,"name",a.name);setFieldNumber(L,"avgMsFrame",avg);setFieldNumber(L,"peakMs",ticksMs(peak));setFieldNumber(L,"p99Ms",p99ms);setFieldNumber(L,"callsPerSec",sec>0?(double)calls/sec:0);setFieldNumber(L,"performanceShare",percent(ms,ticksMs(measuredTicks)));
        setFieldNumber(L,"onUpdatePct",percent(ticksMs(kt[1]),ms));setFieldNumber(L,"onEventPct",percent(ticksMs(kt[2]),ms));setFieldNumber(L,"otherPct",percent(ticksMs(kt[0]),ms));setFieldNumber(L,"onUpdateCallsPerSec",sec>0?(double)kc[1]/sec:0);setFieldNumber(L,"onEventCallsPerSec",sec>0?(double)kc[2]/sec:0);setFieldNumber(L,"otherCallsPerSec",sec>0?(double)kc[0]/sec:0);
        setFieldNumber(L,"longFrameHits",lfh);setFieldNumber(L,"totalLongFrameHits",a.totalLongFrameHits);setFieldNumber(L,"longFrameContributionHits",lfc);setFieldNumber(L,"totalLongFrameContributionHits",a.totalLongFrameContributionHits);Lua50::SetTable(L,-3);
    }
    Lua50::SetTable(L,-3);
    long long snapEnd=nowTicks();g_lastSnapshotOverheadMs=snapEnd>snapStart?ticksMs(snapEnd-snapStart):0;return 1;
}
static int lightFiles(Lua50::State L){
    const char* name=Lua50::GetTop(L)>=3&&Lua50::IsString(L,3)?Lua50::ToString(L,3):"";bool pub=g_havePublishedWindow;double sec=selectedLightWindowSeconds();unsigned long frames=pub?g_publishedFrames:g_frames;Lua50::NewTable(L);unsigned row=0;int ai=findAddon(name);long long addonTicks=0;if(ai>=0)addonTicks=pub?g_addons[ai].publishedTicks:g_addons[ai].totalTicks;
    for(unsigned i=0;i<g_fileCount;++i){FileStat& f=g_files[i];if(!eq(f.addon,name))continue;long long ft=pub?f.publishedTicks:f.totalTicks;unsigned long calls=pub?f.publishedCalls:f.totalCalls;long long peak=pub?f.publishedPeakFrameTicks:f.peakFrameTicks;Lua50::PushNumber(L,(double)++row);Lua50::NewTable(L);double ms=ticksMs(ft);setFieldString(L,"file",f.file);setFieldNumber(L,"avgMsFrame",frames?ms/(double)frames:0);setFieldNumber(L,"peakMs",ticksMs(peak));setFieldNumber(L,"callsPerSec",sec>0?(double)calls/sec:0);setFieldNumber(L,"sharePct",percent(ms,ticksMs(addonTicks)));Lua50::SetTable(L,-3);}
    return 1;
}
static int lightEntries(Lua50::State L){
    const char* name=Lua50::GetTop(L)>=3&&Lua50::IsString(L,3)?Lua50::ToString(L,3):"";const char* kind=Lua50::GetTop(L)>=4&&Lua50::IsString(L,4)?Lua50::ToString(L,4):"";bool pub=g_havePublishedWindow;double sec=selectedLightWindowSeconds();unsigned long frames=pub?g_publishedFrames:g_frames;Lua50::NewTable(L);unsigned row=0;
    for(unsigned i=0;i<g_handlerCount;++i){Handler& h=g_handlers[i];if(h.addonIndex>=g_addonCount||h.fileIndex>=g_fileCount)continue;if(!eq(g_addons[h.addonIndex].name,name))continue;if(*kind&&!eq(kindName(h.kind),kind))continue;long long ht=pub?h.publishedTicks:h.totalTicks;unsigned long calls=pub?h.publishedCalls:h.totalCalls;long long peak=pub?h.publishedPeakTicks:h.peakTicks;double p99ms=pub?h.publishedP99Ms:p99Entry(h);Lua50::PushNumber(L,(double)++row);Lua50::NewTable(L);double ms=ticksMs(ht);setFieldNumber(L,"token",i+1);setFieldString(L,"kind",kindName(h.kind));setFieldString(L,"file",g_files[h.fileIndex].file);setFieldNumber(L,"line",h.line);setFieldNumber(L,"avgMsFrame",frames?ms/(double)frames:0);setFieldNumber(L,"avgCallMs",calls?ms/(double)calls:0);setFieldNumber(L,"peakMs",ticksMs(peak));setFieldNumber(L,"p99Ms",p99ms);setFieldNumber(L,"callsPerSec",sec>0?(double)calls/sec:0);Lua50::SetTable(L,-3);}
    return 1;
}
static void pushContributor(Lua50::State L,const Contributor& c){Lua50::NewTable(L);setFieldString(L,"addon",c.addon);setFieldString(L,"file",c.file);setFieldNumber(L,"ms",c.ms);setFieldNumber(L,"pct",c.pct);setFieldNumber(L,"contributionLevel",c.level);setFieldString(L,"contributionLabel",contributionLabel(c.level));setFieldBool(L,"effectiveContribution",c.level>=1);}
static int lightLongFrames(Lua50::State L){Lua50::NewTable(L);for(unsigned i=0;i<g_longCount;++i){LongFrame& r=g_longFrames[i];Lua50::PushNumber(L,(double)i+1);Lua50::NewTable(L);setFieldString(L,"time",r.time);setFieldNumber(L,"frameMs",r.frameMs);setFieldNumber(L,"luaMs",r.luaMs);setFieldString(L,"addon",r.addon);setFieldString(L,"file",r.file);setFieldNumber(L,"associatedAddonMs",r.associatedAddonMs);setFieldNumber(L,"associatedAddonPct",r.associatedAddonPct);setFieldNumber(L,"measuredLuaPct",r.measuredLuaPct);setFieldNumber(L,"unmeasuredMs",r.unmeasuredMs);setFieldNumber(L,"contributionLevel",r.level);setFieldString(L,"contributionLabel",contributionLabel(r.level));setFieldBool(L,"effectiveContribution",r.level>=1);setFieldNumber(L,"contributorCount",r.contributorCount);Lua50::PushString(L,"contributors");Lua50::NewTable(L);for(unsigned j=0;j<r.contributorCount;++j){Lua50::PushNumber(L,(double)j+1);pushContributor(L,r.contributors[j]);Lua50::SetTable(L,-3);}Lua50::SetTable(L,-3);Lua50::SetTable(L,-3);}return 1;}

static double deepSessionSeconds(){long long n=nowTicks();return g_deepStartTicks&&n>g_deepStartTicks?ticksSec(n-g_deepStartTicks):0;}
static double queryDeepLuaMemory(Lua50::State L){if(!L)return -1.0;int top=Lua50::GetTop(L);double out=-1.0;g_deepSuppressHook=true;Lua50::GetGlobal(L,"gcinfo");if(Lua50::Type(L,-1)==WoW112::LUA_TFUNCTION){if(Lua50::PCall(L,0,1,0)==0&&Lua50::IsNumber(L,-1))out=Lua50::ToNumber(L,-1);}Lua50::SetTop(L,top);g_deepSuppressHook=false;return out;}
static void sampleDeepMemory(Lua50::State L){long long n=nowTicks();double kb=queryDeepLuaMemory(L);if(kb<0)return;if(g_deepLastLuaMemoryKb>=0&&g_deepMemorySampleQpc&&n>g_deepMemorySampleQpc){double sec=ticksSec(n-g_deepMemorySampleQpc);if(sec>0)g_deepAllocRateKbSec=(kb-g_deepLastLuaMemoryKb)/sec;if(kb+1.0<g_deepLastLuaMemoryKb){g_deepLastGcQpc=n;g_deepGcDetected++;}}g_deepLuaMemoryKb=kb;g_deepLastLuaMemoryKb=kb;g_deepMemorySampleQpc=n;}
static int deepStatus(Lua50::State L){markDeepProfilerApi();Lua50::NewTable(L);setFieldString(L,"version",DEEP_VERSION);setFieldBool(L,"running",g_deepRunning);setFieldBool(L,"deep",g_deepRunning);setFieldString(L,"mode",DEEP_MODE);setFieldString(L,"targetAddon",g_deepTarget);setFieldNumber(L,"sampleIntervalMs",g_deepSampleIntervalMs);setFieldNumber(L,"sessionSeconds",deepSessionSeconds());setFieldNumber(L,"sampledScopes",g_deepSampledScopes);setFieldNumber(L,"skippedScopes",g_deepSkippedScopes);setFieldNumber(L,"hookBusySkips",g_deepHookBusySkips);setFieldNumber(L,"abortedScopes",g_deepAbortedScopes);setFieldNumber(L,"sampledScopeMs",ticksMs(g_deepSampledTicks));setFieldBool(L,"scopeActive",g_deepScopeActive);setFieldBool(L,"hookOwned",stateHook(L)==(void*)&deepHook);setFieldBool(L,"hookBusy",currentHookBusy(L));setFieldNumber(L,"hookMask",stateHookMask(L));return 1;}
static int deepStart(Lua50::State L){
    if(g_deepRunning){Lua50::PushBool(L,true);Lua50::PushString(L,"ALREADY_RUNNING");return 2;}
    if(Lua50::GetTop(L)<3||!Lua50::IsString(L,3)||!Lua50::ToString(L,3)[0]){Lua50::PushBool(L,false);Lua50::PushString(L,"TARGET_REQUIRED");return 2;}
    const char* target=Lua50::ToString(L,3);int ai=findAddon(target);if(ai<0){Lua50::PushBool(L,false);Lua50::PushString(L,"TARGET_NOT_WRAPPED");return 2;}if(currentHookBusy(L)){Lua50::PushBool(L,false);Lua50::PushString(L,"HOOK_BUSY");return 2;}if(!initQpc()){Lua50::PushBool(L,false);Lua50::PushString(L,"QPC_UNAVAILABLE");return 2;}if(!ensureDeepStorage()){Lua50::PushBool(L,false);Lua50::PushString(L,"QPC_UNAVAILABLE");return 2;}
    double interval=50.0;if(Lua50::GetTop(L)>=4&&Lua50::IsNumber(L,4)){double v=Lua50::ToNumber(L,4);if(v>=10.0&&v<=500.0)interval=v;}
    resetDeepData(false);g_deepRunning=true;cpy(g_deepTarget,sizeof(g_deepTarget),target);g_deepTargetIndex=ai;g_deepSampleIntervalMs=interval;g_deepStartTicks=nowTicks();g_deepWindowStart=g_deepStartTicks;g_deepWindowHookBase=g_deepHookOverheadTicks;g_deepWindowExplicitBase=g_deepExplicitOverheadTicks;g_deepNextTicks=0;setIsolation(true);Lua50::PushBool(L,true);Lua50::PushString(L,"OK");return 2;
}
static void stopDeepInternal(Lua50::State current,bool hard){if(g_deepScopeActive)forceEndDeepScope(current,true);else if(current)forceUnhookCurrent(current);setIsolation(false);resetDeepData(true);(void)hard;}
static int deepStop(Lua50::State L){markDeepProfilerApi();stopDeepInternal(L,false);Lua50::PushBool(L,true);return 1;}
static int deepReset(Lua50::State L){markDeepProfilerApi();if(g_deepRunning){Lua50::PushBool(L,false);Lua50::PushString(L,"STOP_BEFORE_RESET");return 2;}resetDeepData(true);Lua50::PushBool(L,true);return 1;}
static int deepHardReset(Lua50::State L){if(L&&stateHook(L)==(void*)&deepHook)setHook(L,0,0,0);g_deepScopeActive=false;g_deepState=0;setIsolation(false);resetDeepData(true);Lua50::PushBool(L,true);return 1;}
static void swapu(unsigned* a,unsigned* b){unsigned x=*a;*a=*b;*b=x;}
static void sortDeepFuncOrder(unsigned* o,int lo,int hi){if(lo>=hi)return;long long pivot=g_deepFuncs[o[(lo+hi)/2]].totalTicks;int i=lo,j=hi;while(i<=j){while(g_deepFuncs[o[i]].totalTicks>pivot)++i;while(g_deepFuncs[o[j]].totalTicks<pivot)--j;if(i<=j){swapu(&o[i],&o[j]);++i;--j;}}if(lo<j)sortDeepFuncOrder(o,lo,j);if(i<hi)sortDeepFuncOrder(o,i,hi);}
static void sortDeepAddonOrder(unsigned* o,int lo,int hi){if(lo>=hi)return;long long pivot=g_deepAddons[o[(lo+hi)/2]].s.lastTicks;int i=lo,j=hi;while(i<=j){while(g_deepAddons[o[i]].s.lastTicks>pivot)++i;while(g_deepAddons[o[j]].s.lastTicks<pivot)--j;if(i<=j){swapu(&o[i],&o[j]);++i;--j;}}if(lo<j)sortDeepAddonOrder(o,lo,j);if(i<hi)sortDeepAddonOrder(o,i,hi);}
static void sortDeepFileOrder(unsigned* o,int lo,int hi){if(lo>=hi)return;long long pivot=g_deepFiles[o[(lo+hi)/2]].lastTicks;int i=lo,j=hi;while(i<=j){while(g_deepFiles[o[i]].lastTicks>pivot)++i;while(g_deepFiles[o[j]].lastTicks<pivot)--j;if(i<=j){swapu(&o[i],&o[j]);++i;--j;}}if(lo<j)sortDeepFileOrder(o,lo,j);if(i<hi)sortDeepFileOrder(o,i,hi);}
static void pushDeepAddonSnapshot(Lua50::State L){Lua50::NewTable(L);if(!g_deepAddons||!g_deepAddonCount)return;unsigned* order=(unsigned*)allocz(sizeof(unsigned)*g_deepAddonCount);if(!order)return;for(unsigned i=0;i<g_deepAddonCount;++i)order[i]=i;sortDeepAddonOrder(order,0,(int)g_deepAddonCount-1);unsigned row=0;for(unsigned r=0;r<g_deepAddonCount;++r){DeepAddon& a=g_deepAddons[order[r]];DeepAgg& s=a.s;if(!s.lastTicks&&!s.lastCalls)continue;Lua50::PushNumber(L,(double)++row);Lua50::NewTable(L);double ms=ticksMs(s.lastTicks);setFieldString(L,"name",a.name);setFieldNumber(L,"avgMsFrame",g_deepLastFrames?ms/(double)g_deepLastFrames:0);setFieldNumber(L,"peakMs",ticksMs(s.lastPeakFrameTicks));setFieldNumber(L,"p99Ms",p99(s.lastSamples,s.lastSampleCount,ADDON_SAMPLES));setFieldNumber(L,"callsPerSec",g_deepLastWindowSeconds>0?(double)s.lastCalls/g_deepLastWindowSeconds:0);setFieldNumber(L,"performanceShare",percent(ms,ticksMs(g_deepLastAttributedTicks)));long long classified=s.lastKindTicks[0]+s.lastKindTicks[1]+s.lastKindTicks[2];setFieldNumber(L,"onUpdatePct",classified?100.0*(double)s.lastKindTicks[1]/(double)classified:0);setFieldNumber(L,"onEventPct",classified?100.0*(double)s.lastKindTicks[2]/(double)classified:0);setFieldNumber(L,"otherPct",classified?100.0*(double)s.lastKindTicks[0]/(double)classified:0);setFieldNumber(L,"onUpdateCallsPerSec",g_deepLastWindowSeconds>0?(double)s.lastKindCalls[1]/g_deepLastWindowSeconds:0);setFieldNumber(L,"onEventCallsPerSec",g_deepLastWindowSeconds>0?(double)s.lastKindCalls[2]/g_deepLastWindowSeconds:0);setFieldNumber(L,"otherCallsPerSec",g_deepLastWindowSeconds>0?(double)s.lastKindCalls[0]/g_deepLastWindowSeconds:0);Lua50::SetTable(L,-3);}void* p=order;freep(p);}
static int deepSnapshot(Lua50::State L){markDeepProfilerApi();sampleDeepMemory(L);Lua50::NewTable(L);setFieldString(L,"version",DEEP_VERSION);setFieldBool(L,"running",g_deepRunning);setFieldBool(L,"deep",g_deepRunning);setFieldString(L,"measurement","native-call-ret-qpc");setFieldString(L,"contextClassifier","event-global/arg1-number heuristic v1");setFieldNumber(L,"windowSeconds",g_deepLastWindowSeconds);setFieldNumber(L,"frames",g_deepLastFrames);double frameAvg=g_deepLastFrames?ticksMs(g_deepLastFrameTicks)/(double)g_deepLastFrames:0;setFieldNumber(L,"frameAvgMs",frameAvg);setFieldNumber(L,"fps",frameAvg>0?1000.0/frameAvg:0);setFieldNumber(L,"framePeakMs",ticksMs(g_deepLastPeakFrameTicks));setFieldNumber(L,"luaMsFrame",g_deepLastFrames?ticksMs(g_deepLastLuaTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"systemLuaMsFrame",g_deepLastFrames?ticksMs(g_deepLastSystemTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"unattributedMsFrame",g_deepLastFrames?ticksMs(g_deepLastUnattributedTicks)/(double)g_deepLastFrames:0);double coverage=g_deepLastLuaTicks>0?100.0*(double)(g_deepLastAttributedTicks+g_deepLastSystemTicks)/(double)g_deepLastLuaTicks:100.0;setFieldNumber(L,"coveragePct",coverage);setFieldNumber(L,"profilerOverheadMsFrame",g_deepLastFrames?ticksMs(g_deepLastHookOverheadTicks+g_deepLastExplicitOverheadTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"hookOverheadMsFrame",g_deepLastFrames?ticksMs(g_deepLastHookOverheadTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"apiOverheadMsFrame",g_deepLastFrames?ticksMs(g_deepLastExplicitOverheadTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"luaMemoryKb",g_deepLuaMemoryKb);setFieldNumber(L,"allocationRateKbSec",g_deepAllocRateKbSec);setFieldNumber(L,"gcDetectedCount",g_deepGcDetected);setFieldNumber(L,"lastGcAgeSec",g_deepLastGcQpc&&nowTicks()>g_deepLastGcQpc?ticksSec(nowTicks()-g_deepLastGcQpc):-1);setFieldNumber(L,"longFrameThresholdMs",g_deepThresholdMs);setFieldNumber(L,"longFrameCount",g_deepLongCount);setFieldBool(L,"functionOverflow",g_deepFunctionOverflow);setFieldString(L,"mode",DEEP_MODE);setFieldString(L,"targetAddon",g_deepTarget);setFieldNumber(L,"sessionSeconds",deepSessionSeconds());setFieldNumber(L,"sampledScopes",g_deepSampledScopes);setFieldNumber(L,"skippedScopes",g_deepSkippedScopes);setFieldNumber(L,"hookBusySkips",g_deepHookBusySkips);setFieldNumber(L,"abortedScopes",g_deepAbortedScopes);setFieldNumber(L,"sampledScopeMs",ticksMs(g_deepSampledTicks));Lua50::PushString(L,"addons");pushDeepAddonSnapshot(L);Lua50::SetTable(L,-3);return 1;}
static int deepFunctions(Lua50::State L){markDeepProfilerApi();const char* req=Lua50::GetTop(L)>=3&&Lua50::IsString(L,3)?Lua50::ToString(L,3):"";Lua50::NewTable(L);if(!*req||!g_deepFuncs||!g_deepFuncCount)return 1;unsigned count=0;for(unsigned i=0;i<g_deepFuncCount;++i)if(eq(g_deepFuncs[i].addon,req)&&(g_deepFuncs[i].totalTicks>0||g_deepFuncs[i].calls>0))count++;if(!count)return 1;unsigned* order=(unsigned*)allocz(sizeof(unsigned)*count);if(!order)return 1;unsigned j=0;long long total=0;for(unsigned i=0;i<g_deepFuncCount;++i)if(eq(g_deepFuncs[i].addon,req)&&(g_deepFuncs[i].totalTicks>0||g_deepFuncs[i].calls>0)){order[j++]=i;total+=g_deepFuncs[i].totalTicks;}sortDeepFuncOrder(order,0,(int)count-1);double sec=deepSessionSeconds();for(unsigned r=0;r<count;++r){DeepFunc& f=g_deepFuncs[order[r]];Lua50::PushNumber(L,(double)r+1);Lua50::NewTable(L);double ms=ticksMs(f.totalTicks);setFieldString(L,"key",f.key);setFieldString(L,"file",f.file);setFieldNumber(L,"line",f.line);setFieldString(L,"name",f.name);setFieldBool(L,"cApi",f.cApi);setFieldNumber(L,"totalMs",ms);setFieldNumber(L,"calls",f.calls);setFieldNumber(L,"avgCallMs",f.calls?ms/(double)f.calls:0);setFieldNumber(L,"callsPerSec",sec>0?(double)f.calls/sec:0);setFieldNumber(L,"sharePct",percent(ms,ticksMs(total)));Lua50::SetTable(L,-3);}void* p=order;freep(p);return 1;}
static int deepFiles(Lua50::State L){markDeepProfilerApi();const char* req=Lua50::GetTop(L)>=3&&Lua50::IsString(L,3)?Lua50::ToString(L,3):"";Lua50::NewTable(L);if(!*req||!g_deepFiles||!g_deepFileCount)return 1;unsigned count=0;for(unsigned i=0;i<g_deepFileCount;++i)if(eq(g_deepFiles[i].addon,req)&&g_deepFiles[i].lastCalls>0)count++;if(!count)return 1;unsigned* order=(unsigned*)allocz(sizeof(unsigned)*count);if(!order)return 1;unsigned j=0;for(unsigned i=0;i<g_deepFileCount;++i)if(eq(g_deepFiles[i].addon,req)&&g_deepFiles[i].lastCalls>0)order[j++]=i;sortDeepFileOrder(order,0,(int)count-1);for(unsigned r=0;r<count;++r){DeepFile& f=g_deepFiles[order[r]];Lua50::PushNumber(L,(double)r+1);Lua50::NewTable(L);setFieldString(L,"file",f.file);setFieldNumber(L,"avgMsFrame",g_deepLastFrames?ticksMs(f.lastTicks)/(double)g_deepLastFrames:0);setFieldNumber(L,"callsPerSec",g_deepLastWindowSeconds>0?(double)f.lastCalls/g_deepLastWindowSeconds:0);setFieldNumber(L,"peakMs",ticksMs(f.lastPeakFrameTicks));Lua50::SetTable(L,-3);}void* p=order;freep(p);return 1;}
static int deepLongFrames(Lua50::State L){markDeepProfilerApi();Lua50::NewTable(L);for(unsigned i=0;i<g_deepLongCount;++i){Lua50::PushNumber(L,(double)i+1);Lua50::NewTable(L);setFieldString(L,"time",g_deepLong[i].time);setFieldNumber(L,"frameMs",g_deepLong[i].frameMs);setFieldNumber(L,"luaMs",g_deepLong[i].luaMs);setFieldString(L,"addon",g_deepLong[i].addon);setFieldString(L,"file",g_deepLong[i].file);Lua50::SetTable(L,-3);}return 1;}

static int dispatchLight(Lua50::State L){const char* sub=Lua50::GetTop(L)>=2&&Lua50::IsString(L,2)?Lua50::ToString(L,2):"status";if(eq(sub,"wrap"))return doWrap(L);if(eq(sub,"start"))return lightStart(L);if(eq(sub,"stop"))return lightStop(L);if(eq(sub,"reset"))return lightReset(L);if(eq(sub,"hardreset"))return lightHardReset(L);if(eq(sub,"status"))return lightStatus(L);if(eq(sub,"snapshot"))return pushLightSnapshot(L);if(eq(sub,"files"))return lightFiles(L);if(eq(sub,"entries"))return lightEntries(L);if(eq(sub,"longframes"))return lightLongFrames(L);if(eq(sub,"threshold")){if(Lua50::GetTop(L)>=3&&Lua50::IsNumber(L,3)){double v=Lua50::ToNumber(L,3);if(v>=5.0&&v<=1000.0)g_longThresholdMs=v;}Lua50::PushNumber(L,g_longThresholdMs);return 1;}if(eq(sub,"deep")||eq(sub,"functions")){Lua50::PushBool(L,false);Lua50::PushString(L,"DISABLED_IN_B1R4_CALIBRATED");return 2;}Lua50::PushNil(L);Lua50::PushString(L,"API_NOT_FOUND");return 2;}
static int dispatchDeep(Lua50::State L){const char* sub=Lua50::GetTop(L)>=2&&Lua50::IsString(L,2)?Lua50::ToString(L,2):"status";if(eq(sub,"status"))return deepStatus(L);if(eq(sub,"start"))return deepStart(L);if(eq(sub,"stop"))return deepStop(L);if(eq(sub,"reset"))return deepReset(L);if(eq(sub,"hardreset"))return deepHardReset(L);if(eq(sub,"deep")){Lua50::PushBool(L,g_deepRunning);return 1;}if(eq(sub,"snapshot"))return deepSnapshot(L);if(eq(sub,"functions"))return deepFunctions(L);if(eq(sub,"files"))return deepFiles(L);if(eq(sub,"longframes"))return deepLongFrames(L);if(eq(sub,"threshold")){markDeepProfilerApi();if(Lua50::GetTop(L)>=3&&Lua50::IsNumber(L,3)){double v=Lua50::ToNumber(L,3);if(v>=5.0&&v<=1000.0)g_deepThresholdMs=v;}Lua50::PushNumber(L,g_deepThresholdMs);return 1;}Lua50::PushNil(L);Lua50::PushString(L,"API_NOT_FOUND");return 2;}

int dispatch(Lua50::State L,const char* command){
    if(!command)return -1;
    // TaiYangShenDian direct API accepts Profiler/ProfilerDeep while the legacy
    // UnitXP bridge preserves the original lowercase profiler/profilerdeep names.
    if(ieq(command,"profiler"))return dispatchLight(L);
    if(ieq(command,"profilerdeep"))return dispatchDeep(L);
    return -1;
}
void onWorldLeaving(Lua50::State currentState){forceUnhookCurrent(currentState);stopDeepInternal(currentState,true);hardResetLight();}
void onWorldEntering(Lua50::State currentState){forceUnhookCurrent(currentState);stopDeepInternal(currentState,true);hardResetLight();}
void shutdown(){g_running=false;g_deepRunning=false;g_deepIsolation=false;g_deepScopeActive=false;g_deepState=0;void* p=g_handlers;freep(p);g_handlers=(Handler*)p;p=g_addons;freep(p);g_addons=(AddonStat*)p;p=g_files;freep(p);g_files=(FileStat*)p;p=g_deepFuncs;freep(p);g_deepFuncs=(DeepFunc*)p;if(g_heap){HeapDestroy(g_heap);g_heap=0;}}

} // namespace
