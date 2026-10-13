#include <windows.h>
#include "visual_native.h"

namespace TysVisual {
namespace {

struct V3 { float x,y,z; };
using CreateModelProc = void* (__thiscall*)(void*,const char*,unsigned long);
using ReleaseModelProc = void (__thiscall*)(void*);
using EnsureRenderReadyProc = int (__thiscall*)(void*,int,int);
using SetWorldMatrixProc = void (__thiscall*)(void*,const float*);
using SetBooleanProc = void (__thiscall*)(void*,int);
using SetAlphaProc = void (__thiscall*)(void*,float);
using SetColorProc = void (__thiscall*)(void*,const V3*);
using SetSequenceProc = void (__thiscall*)(void*,int,int,int,int,float,int,int);
using WorldToScreenProc = bool (__thiscall*)(unsigned long,float*,float*);
using DdcToNdcProc = void (__fastcall*)(float*,float*,float,float);
using SFileOpenArchiveProc = BOOL (__stdcall*)(const char*,DWORD,DWORD,void**);
using SFileOpenFileExProc = BOOL (__stdcall*)(void*,const char*,DWORD,void**);
using SFileReadFileProc = BOOL (__stdcall*)(void*,void*,DWORD,DWORD*,void*,DWORD);
using SFileGetFileSizeProc = DWORD (__stdcall*)(void*,DWORD*);
using SFileCloseFileProc = BOOL (__stdcall*)(void*);
using SFileCloseArchiveProc = BOOL (__stdcall*)(void*);

constexpr unsigned long OWNER_NONE=0, OWNER_AUTORANGE=1, OWNER_MOON=2, OWNER_MOON_PREVIEW=3, OWNER_MOON_ADV_PREVIEW=4;
constexpr unsigned long AR_BEGIN=0;
constexpr unsigned long MOON_BEGIN=SLOT_COUNT;
constexpr unsigned long PREVIEW_INDEX=SLOT_COUNT+MOON_SLOT_COUNT;
constexpr unsigned long ADV_PREVIEW_INDEX=PREVIEW_INDEX+1UL;
constexpr unsigned long WORLD_FRAME_PTR=0x00B4B2BCUL;
constexpr unsigned long WORLD_TO_SCREEN=0x00483EE0UL;
constexpr unsigned long DDC_TO_NDC=0x0041ADE0UL;
constexpr unsigned long GX_D3D_DEVICE_OFFSET=0x38A8UL;
constexpr float ICON_WORLD_HEIGHT=8.5f;
constexpr float BASE_ICON_PIXELS=42.0f;
constexpr const char* RAID_ICON_TEXTURE_PATH="Interface\\TargetingFrame\\UI-RaidTargetingIcons.blp";
constexpr unsigned long SFILE_OPEN_ARCHIVE=0x00648DD0UL;
constexpr unsigned long SFILE_OPEN_FILE_EX=0x006477C0UL;
constexpr unsigned long SFILE_READ_FILE=0x00648460UL;
constexpr unsigned long SFILE_GET_FILE_SIZE=0x006487F0UL;
constexpr unsigned long SFILE_CLOSE_FILE=0x00648730UL;
constexpr unsigned long SFILE_CLOSE_ARCHIVE=0x00648EF0UL;
constexpr unsigned long INVALID_FILE_SIZE_=0xFFFFFFFFUL;
constexpr unsigned long MAX_RAID_TEXTURE_BYTES=16UL*1024UL*1024UL;
constexpr unsigned MAX_ARCHIVES=256;
constexpr const char* MOON_BEAM_PATH="Spells\\MoonBeam_Impact_Base.mdx";
constexpr const char* TARGET_CIRCLE_PATH="Spells\\TargetingCircle.mdx";

struct Slot {
    bool active;
    bool hidden;
    bool renderReady;
    unsigned long owner;
    void* context;
    void* model;
    char key[KEY_CAP];
    char path[PATH_CAP];
    float x,y,z,scale,yaw,alpha;
    V3 tint;
    int moonColor;
    int moonIcon;
};

struct ProjectedIcon {
    bool visible;
    float x,y,size;
    int colorIndex,iconIndex;
};

struct TexturedIconVertex { float x,y,z,rhw; unsigned long color; float u,v; };
struct IconVertex { float x,y,z,rhw; unsigned long color; };
struct D3DViewport { unsigned long X,Y,Width,Height; float MinZ,MaxZ; };
struct D3DLockedRect { long Pitch; void* pBits; };

static Slot g_slots[TOTAL_SLOT_COUNT] = {};
static ProjectedIcon g_projected[MOON_SLOT_COUNT] = {};
static TexturedIconVertex g_iconVertices[MOON_SLOT_COUNT*6] = {};
static unsigned long g_iconVertexCount=0;
// Original MoonMarker keeps vector-drawn raid markers as a safe fallback when
// UI-RaidTargetingIcons.blp cannot be loaded. A fixed buffer avoids CRT/STL
// dependencies while retaining enough room for all 64 skull-shaped markers.
static IconVertex g_fallbackVertices[MOON_SLOT_COUNT*512] = {};
static unsigned long g_fallbackVertexCount=0;
static void* g_raidIconTexture=0;
static void* g_raidIconTextureDevice=0;
static unsigned long g_raidIconTextureWidth=0,g_raidIconTextureHeight=0;
static float g_raidIconUCell=0.25f,g_raidIconVCell=0.5f;
static bool g_raidIconTextureLoadAttempted=false;
static char g_archives[MAX_ARCHIVES][MAX_PATH] = {};
static unsigned g_archiveCount=0;
static char g_lastError[96] = "NOT_STARTED";
static unsigned long g_setCalls=0,g_setOK=0,g_moveCalls=0,g_hideCalls=0,g_showCalls=0,g_restartCalls=0;
static unsigned long g_clearCalls=0,g_clearSafe=0,g_clearStale=0,g_reattachCalls=0,g_staleDrops=0;
static unsigned long g_nativeFrames=0,g_nativeRefreshes=0,g_nativeDrops=0,g_moonSetCalls=0,g_moonSetOK=0,g_worldLeaves=0,g_worldEnters=0,g_worldDropNoRelease=0;

// MoonMarker Present overlay is a MoonMarker-specific consumer. The M2 bodies
// themselves are still owned by the single shared slot/lifetime engine above.
using PresentProc = long (__stdcall*)(void*,const void*,const void*,void*,const void*);
static PresentProc g_presentNext=0;
static void** g_presentSlot=0;
static void* g_presentDevice=0;
static bool g_presentInstalled=false;

static unsigned long slen(const char* s){ unsigned long n=0;if(s)while(s[n])++n;return n; }
static void cpy(char* o,unsigned long cap,const char* s){ if(!cap)return;unsigned long i=0;if(s)for(;s[i]&&i+1<cap;++i)o[i]=s[i];o[i]=0; }
static void cat(char* o,unsigned long cap,const char* s){ unsigned long n=slen(o);if(n<cap)cpy(o+n,cap-n,s); }
static void appendChar(char* o,unsigned long cap,char c){ unsigned long n=slen(o);if(n+1>=cap)return;o[n]=c;o[n+1]=0; }
static void appendUInt(char* o,unsigned long cap,unsigned long v){ char t[16]={0};int n=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<15);while(n)appendChar(o,cap,t[--n]); }
static bool eq(const char* a,const char* b){ if(!a||!b)return false;while(*a&&*b){if(*a!=*b)return false;++a;++b;}return *a==*b; }
static bool ieq(const char* a,const char* b){ if(!a||!b)return false;while(*a&&*b){char x=*a,y=*b;if(x>='A'&&x<='Z')x=(char)(x+32);if(y>='A'&&y<='Z')y=(char)(y+32);if(x!=y)return false;++a;++b;}return *a==*b; }
static char lower(char c){return (c>='A'&&c<='Z')?(char)(c+32):c;}
static bool finitef(float v){ return v==v && v>-100000000.0f && v<100000000.0f; }
static float absf(float v){return v<0?-v:v;}
static float minf(float a,float b){return a<b?a:b;}
static float maxf(float a,float b){return a>b?a:b;}
static void setErr(const char* s){cpy(g_lastError,sizeof(g_lastError),s);}
static bool currentWorldFrame(unsigned long* frame);

static bool readableRange(const void* p,unsigned long n){
    if(!p||!n)return false; MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery(p,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;
    DWORD q=m.Protect&0xFF;if(q==PAGE_NOACCESS)return false;
    unsigned long a=(unsigned long)p,b=(unsigned long)m.BaseAddress+(unsigned long)m.RegionSize;
    return a+n>=a&&a+n<=b;
}

static void* currentContext(){
    void* c=*(void**)WORLD_M2_CONTEXT_PTR;
    if(!c||(((unsigned long)c)&1UL))return 0;
    return c;
}

static bool contextValid(const Slot& s){
    if(!s.model||!s.context)return false;
    void* live=currentContext();if(!live||live!=s.context)return false;
    return *(void**)((unsigned char*)s.model+0x2C)==s.context;
}

static void zeroSlot(Slot& s){
    s.active=false;s.hidden=false;s.renderReady=false;s.owner=OWNER_NONE;s.context=0;s.model=0;s.key[0]=0;s.path[0]=0;
    s.x=s.y=s.z=0.0f;s.scale=1.0f;s.yaw=0.0f;s.alpha=1.0f;s.tint={1.0f,1.0f,1.0f};s.moonColor=-1;s.moonIcon=-1;
}

static bool normalizePath(const char* in,char* out,unsigned long cap){
    if(!in||!*in||!out||cap<5)return false;unsigned long n=slen(in);if(n<4||n>=PATH_CAP||n+1>cap)return false;
    if(in[0]=='\\'||in[0]=='/')return false;bool dotdot=false;
    for(unsigned long i=0;i<n;++i){unsigned char c=(unsigned char)in[i];if(c<32||c==':')return false;if(i+1<n&&in[i]=='.'&&in[i+1]=='.')dotdot=true;out[i]=(in[i]=='/')?'\\':in[i];}
    out[n]=0;if(dotdot)return false;
    bool m2=(n>=3&&lower(out[n-3])=='.'&&lower(out[n-2])=='m'&&out[n-1]=='2');
    bool mdx=(n>=4&&lower(out[n-4])=='.'&&lower(out[n-3])=='m'&&lower(out[n-2])=='d'&&lower(out[n-1])=='x');
    return m2||mdx;
}

static bool validKey(const char* key){unsigned long n=slen(key);if(!key||n==0||n>=KEY_CAP)return false;for(unsigned long i=0;i<n;++i)if((unsigned char)key[i]<32)return false;return true;}
static bool validTransform(float x,float y,float z,float scale,float yaw){return finitef(x)&&finitef(y)&&finitef(z)&&finitef(scale)&&finitef(yaw)&&scale>=MIN_SCALE&&scale<=MAX_SCALE;}

static void sincosNative(float radians,float* sine,float* cosine){
    float s=0.0f,c=1.0f;
    __asm {
        fld radians
        fsincos
        fstp c
        fstp s
    }
    if(sine)*sine=s;if(cosine)*cosine=c;
}
static float cosNative(float a){float s=0,c=1;sincosNative(a,&s,&c);return c;}
static float sinNative(float a){float s=0,c=1;sincosNative(a,&s,&c);return s;}

static void matrixFor(const Slot& s,float (&m)[16]){
    for(int i=0;i<16;++i)m[i]=0.0f;float r=s.yaw*0.01745329251994329577f,c=1.0f,q=0.0f,z=s.scale;sincosNative(r,&q,&c);
    m[0]=c*z;m[1]=q*z;m[4]=-q*z;m[5]=c*z;m[10]=z;m[15]=1.0f;m[12]=s.x;m[13]=s.y;m[14]=s.z;
}
static void applyMatrix(Slot& s){float m[16]={0};matrixFor(s,m);((SetWorldMatrixProc)SET_WORLD_MATRIX)(s.model,m);}
static bool ensureReady(Slot& s,int requestLoad){if(!s.model)return false;int ready=((EnsureRenderReadyProc)ENSURE_RENDER_READY)(s.model,requestLoad,1);if(ready)s.renderReady=true;return ready!=0;}

static void releaseSlot(Slot& s,bool countClear){
    if(countClear)++g_clearCalls;
    if(!s.model){if(countClear)++g_clearSafe;zeroSlot(s);return;}
    void* live=currentContext();
    if(live&&live==s.context&&contextValid(s)){((SetBooleanProc)ATTACH_RENDER_LIST)(s.model,0);((ReleaseModelProc)RELEASE_MODEL)(s.model);if(countClear)++g_clearSafe;}
    else{if(countClear)++g_clearStale;++g_staleDrops;}
    zeroSlot(s);
}

static bool createAtIndex(unsigned long index,unsigned long owner,const char* normalized,float x,float y,float z,float scale,float yaw,float alpha,const V3& tint,const char* key,int moonColor,int moonIcon){
    if(index>=TOTAL_SLOT_COUNT||!validTransform(x,y,z,scale,yaw)||!finitef(alpha)||alpha<0.0f||alpha>1.0f){setErr("shared_transform_invalid");return false;}
    Slot& s=g_slots[index];releaseSlot(s,false);s.owner=owner;s.x=x;s.y=y;s.z=z;s.scale=scale;s.yaw=yaw;s.alpha=alpha;s.tint=tint;s.moonColor=moonColor;s.moonIcon=moonIcon;if(key)cpy(s.key,sizeof(s.key),key);cpy(s.path,sizeof(s.path),normalized);
    void* c=currentContext();if(!c){zeroSlot(s);setErr("shared_no_world_context");return false;}s.context=c;
    s.model=((CreateModelProc)CREATE_MODEL)(c,normalized,0);if(!s.model){zeroSlot(s);setErr("shared_create_failed");return false;}
    if(*(void**)((unsigned char*)s.model+0x2C)!=c){setErr("shared_context_mismatch");releaseSlot(s,false);return false;}
    applyMatrix(s);((SetAlphaProc)SET_ALPHA)(s.model,alpha);((SetColorProc)SET_COLOR)(s.model,&s.tint);((SetSequenceProc)SET_SEQUENCE)(s.model,-1,0,-1,0,1.0f,1,1);
    ((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s.model,1);((SetBooleanProc)ATTACH_RENDER_LIST)(s.model,1);s.active=true;s.hidden=false;
    if(!*(void**)((unsigned char*)s.model+0x44)){setErr("shared_render_list_missing");releaseSlot(s,false);return false;}
    if(ensureReady(s,1))setErr("shared_ready");else setErr("shared_waiting_resources");return true;
}

static Slot* findArSlot(const char* key){if(!key||!*key)return 0;for(unsigned long i=AR_BEGIN;i<AR_BEGIN+SLOT_COUNT;++i)if(g_slots[i].active&&g_slots[i].owner==OWNER_AUTORANGE&&eq(g_slots[i].key,key))return &g_slots[i];return 0;}
static unsigned long arIndexOf(const Slot* p){return p?(unsigned long)(p-g_slots):TOTAL_SLOT_COUNT;}
static unsigned long allocateArIndex(const char* key){Slot* e=findArSlot(key);if(e)return arIndexOf(e);for(unsigned long i=AR_BEGIN;i<AR_BEGIN+SLOT_COUNT;++i)if(!g_slots[i].active&&!g_slots[i].model)return i;return TOTAL_SLOT_COUNT;}

struct MoonColorDef { const char* name; V3 tint; int defaultIcon; };
static const MoonColorDef g_colors[8]={
    {"red",{1.00f,0.18f,0.18f},6},{"orange",{1.00f,0.45f,0.10f},1},{"yellow",{1.00f,0.82f,0.16f},0},{"green",{0.15f,0.92f,0.28f},3},
    {"cyan",{0.10f,0.90f,0.90f},4},{"blue",{0.20f,0.55f,1.00f},5},{"purple",{0.68f,0.28f,1.00f},2},{"white",{0.86f,0.92f,1.00f},7}
};
static const char* g_icons[8]={"star","circle","diamond","triangle","moon","square","cross","skull"};
static int moonColorIndex(const char* name){for(int i=0;i<8;++i)if(ieq(name,g_colors[i].name))return i;return -1;}
static int moonIconIndex(const char* name){if(name&&name[0]&&name[1]==0&&name[0]>='1'&&name[0]<='8')return name[0]-'1';for(int i=0;i<8;++i)if(ieq(name,g_icons[i]))return i;return -1;}
static unsigned long moonIndex(int c,int icon){return MOON_BEGIN+(unsigned long)c*8UL+(unsigned long)icon;}

static bool anyMoonActive(){for(unsigned long i=MOON_BEGIN;i<MOON_BEGIN+MOON_SLOT_COUNT;++i)if(g_slots[i].active&&g_slots[i].owner==OWNER_MOON)return true;return false;}
static void resetProjected(){for(unsigned long i=0;i<MOON_SLOT_COUNT;++i)g_projected[i].visible=false;}

// ---- MoonMarker original top-marker renderer: Blizzard raid-target atlas ----
static void* d3dMethod(void* obj,unsigned long index){if(!obj)return 0;void*** p=(void***)obj;if(!readableRange(p,sizeof(void*))||!*p||!readableRange(*p,(index+1)*sizeof(void*)))return 0;return (*p)[index];}
static unsigned long lowerCmpChar(char c){return (unsigned long)(unsigned char)lower(c);}
static int cmpI(const char* a,const char* b){if(!a)a="";if(!b)b="";while(*a&&*b){unsigned long x=lowerCmpChar(*a),y=lowerCmpChar(*b);if(x<y)return -1;if(x>y)return 1;++a;++b;}return *a?1:(*b?-1:0);}
static bool eqI(const char* a,const char* b){return cmpI(a,b)==0;}
static bool endsMpq(const char* n){unsigned long l=slen(n);return l>=4&&lower(n[l-4])=='.'&&lower(n[l-3])=='m'&&lower(n[l-2])=='p'&&lower(n[l-1])=='q';}
static void parentDir(char* p){unsigned long l=slen(p);while(l){char c=p[l-1];if(c=='\\'||c=='/'){p[l-1]=0;return;}--l;}cpy(p,MAX_PATH,".");}
static void pathJoin(char* out,unsigned long cap,const char* a,const char* b){cpy(out,cap,a);unsigned long n=slen(out);if(n&&out[n-1]!='\\'&&out[n-1]!='/')cat(out,cap,"\\");cat(out,cap,b);}
static void collectArchives(const char* dir,int depth){if(depth<0||g_archiveCount>=MAX_ARCHIVES)return;char pat[MAX_PATH]={0};pathJoin(pat,sizeof(pat),dir,"*");WIN32_FIND_DATAA fd={};HANDLE h=FindFirstFileA(pat,&fd);if(h==INVALID_HANDLE_VALUE)return;do{const char* n=fd.cFileName;if((n[0]=='.'&&n[1]==0)||(n[0]=='.'&&n[1]=='.'&&n[2]==0))continue;char full[MAX_PATH]={0};pathJoin(full,sizeof(full),dir,n);if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)collectArchives(full,depth-1);else if(endsMpq(n)&&g_archiveCount<MAX_ARCHIVES){cpy(g_archives[g_archiveCount],MAX_PATH,full);++g_archiveCount;}}while(FindNextFileA(h,&fd));FindClose(h);}
static void sortDedupeArchives(){for(unsigned i=1;i<g_archiveCount;++i){char tmp[MAX_PATH]={0};cpy(tmp,MAX_PATH,g_archives[i]);unsigned j=i;while(j>0&&cmpI(tmp,g_archives[j-1])>0){cpy(g_archives[j],MAX_PATH,g_archives[j-1]);--j;}cpy(g_archives[j],MAX_PATH,tmp);}unsigned w=0;for(unsigned i=0;i<g_archiveCount;++i){if(w==0||!eqI(g_archives[i],g_archives[w-1])){if(w!=i)cpy(g_archives[w],MAX_PATH,g_archives[i]);++w;}}g_archiveCount=w;}
static bool loadRaidTextureBytes(unsigned char** outBytes,unsigned long* outSize){if(!outBytes||!outSize)return false;*outBytes=0;*outSize=0;g_archiveCount=0;char exe[MAX_PATH]={0};DWORD n=GetModuleFileNameA(0,exe,MAX_PATH);if(!n||n>=MAX_PATH)return false;parentDir(exe);char data[MAX_PATH]={0};pathJoin(data,sizeof(data),exe,"Data");collectArchives(data,2);sortDedupeArchives();auto openArchive=(SFileOpenArchiveProc)SFILE_OPEN_ARCHIVE;auto openFile=(SFileOpenFileExProc)SFILE_OPEN_FILE_EX;auto readFile=(SFileReadFileProc)SFILE_READ_FILE;auto getSize=(SFileGetFileSizeProc)SFILE_GET_FILE_SIZE;auto closeFile=(SFileCloseFileProc)SFILE_CLOSE_FILE;auto closeArchive=(SFileCloseArchiveProc)SFILE_CLOSE_ARCHIVE;for(unsigned i=0;i<g_archiveCount;++i){void* ar=0;if(!openArchive(g_archives[i],0,0,&ar)||!ar)continue;void* f=0;if(!openFile(ar,RAID_ICON_TEXTURE_PATH,0,&f)||!f){closeArchive(ar);continue;}DWORD hi=0,sz=getSize(f,&hi);if(sz==INVALID_FILE_SIZE_||hi||sz<148||sz>MAX_RAID_TEXTURE_BYTES){closeFile(f);closeArchive(ar);continue;}unsigned char* bytes=(unsigned char*)VirtualAlloc(0,sz,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!bytes){closeFile(f);closeArchive(ar);return false;}DWORD rd=0;BOOL ok=readFile(f,bytes,sz,&rd,0,0);closeFile(f);closeArchive(ar);if(ok&&rd==sz){*outBytes=bytes;*outSize=sz;return true;}VirtualFree(bytes,0,MEM_RELEASE);}return false;}
static unsigned long le32(const unsigned char* p){return (unsigned long)p[0]|((unsigned long)p[1]<<8)|((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24);}
static void copyBytes(unsigned char* d,const unsigned char* s,unsigned long n){for(unsigned long i=0;i<n;++i)d[i]=s[i];}
static unsigned char raw1Alpha(const unsigned char* alpha,unsigned long index,unsigned char bits){if(bits==0)return 255;if(bits==1)return ((alpha[index/8]>>(index%8))&1)?255:0;if(bits==4){unsigned char packed=alpha[index/2],nib=(index&1)?(packed>>4):(packed&0x0F);return (unsigned char)(nib*17);}return alpha[index];}
static bool createTextureRaw1(void* device,const unsigned char* bytes,unsigned long bytesSize,unsigned long width,unsigned long height,unsigned char alphaBits,unsigned long mipOffset,unsigned long mipSize,void** out){constexpr unsigned long PAL=0x94UL,PALBYTES=1024UL;if(bytesSize<PAL+PALBYTES)return false;unsigned long pixels=width*height,ab=alphaBits==1?(pixels+7)/8:alphaBits==4?(pixels+1)/2:alphaBits==8?pixels:0;if(mipSize<pixels+ab||mipOffset>bytesSize||pixels+ab>bytesSize-mipOffset)return false;using CreateTex=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,void**,void*);CreateTex create=(CreateTex)d3dMethod(device,23);if(!create)return false;void* tex=0;if(create(device,width,height,1,0,21,1,&tex,0)<0||!tex)return false;using Lock=long (__stdcall*)(void*,unsigned long,D3DLockedRect*,const void*,unsigned long);using Unlock=long (__stdcall*)(void*,unsigned long);using Release=unsigned long (__stdcall*)(void*);Lock lock=(Lock)d3dMethod(tex,19);Unlock unlock=(Unlock)d3dMethod(tex,20);Release release=(Release)d3dMethod(tex,2);if(!lock||!unlock||!release){if(release)release(tex);return false;}D3DLockedRect lr={};if(lock(tex,0,&lr,0,0)<0){release(tex);return false;}const unsigned char* palette=bytes+PAL;const unsigned char* indices=bytes+mipOffset;const unsigned char* alpha=indices+pixels;for(unsigned long y=0;y<height;++y){unsigned long* row=(unsigned long*)((unsigned char*)lr.pBits+y*(unsigned long)lr.Pitch);for(unsigned long x=0;x<width;++x){unsigned long idx=y*width+x;unsigned char pi=indices[idx];const unsigned char* c=palette+(unsigned long)pi*4;unsigned char a=raw1Alpha(alpha,idx,alphaBits);row[x]=((unsigned long)a<<24)|((unsigned long)c[2]<<16)|((unsigned long)c[1]<<8)|(unsigned long)c[0];}}unlock(tex,0);*out=tex;return true;}
static bool createTextureRaw3(void* device,const unsigned char* bytes,unsigned long bytesSize,unsigned long width,unsigned long height,unsigned long mipOffset,unsigned long mipSize,void** out){unsigned long pitch=width*4,required=pitch*height;if(mipSize<required||mipOffset>bytesSize||required>bytesSize-mipOffset)return false;using CreateTex=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,void**,void*);CreateTex create=(CreateTex)d3dMethod(device,23);if(!create)return false;void* tex=0;if(create(device,width,height,1,0,21,1,&tex,0)<0||!tex)return false;using Lock=long (__stdcall*)(void*,unsigned long,D3DLockedRect*,const void*,unsigned long);using Unlock=long (__stdcall*)(void*,unsigned long);using Release=unsigned long (__stdcall*)(void*);Lock lock=(Lock)d3dMethod(tex,19);Unlock unlock=(Unlock)d3dMethod(tex,20);Release release=(Release)d3dMethod(tex,2);if(!lock||!unlock||!release){if(release)release(tex);return false;}D3DLockedRect lr={};if(lock(tex,0,&lr,0,0)<0){release(tex);return false;}const unsigned char* src=bytes+mipOffset;for(unsigned long y=0;y<height;++y)copyBytes((unsigned char*)lr.pBits+y*(unsigned long)lr.Pitch,src+y*pitch,pitch);unlock(tex,0);*out=tex;return true;}
static bool createTextureDxt(void* device,const unsigned char* bytes,unsigned long bytesSize,unsigned long width,unsigned long height,unsigned char alphaBits,unsigned char alphaType,unsigned long mipOffset,unsigned long mipSize,void** out){unsigned long format=0x31545844UL,blockBytes=8;if(alphaBits>1){format=alphaType==1?0x33545844UL:0x35545844UL;blockBytes=16;}unsigned long cols=(width+3)/4,rows=(height+3)/4,pitch=cols*blockBytes,required=pitch*rows;if(mipSize<required||mipOffset>bytesSize||required>bytesSize-mipOffset)return false;using CreateTex=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,void**,void*);CreateTex create=(CreateTex)d3dMethod(device,23);if(!create)return false;void* tex=0;if(create(device,width,height,1,0,format,1,&tex,0)<0||!tex)return false;using Lock=long (__stdcall*)(void*,unsigned long,D3DLockedRect*,const void*,unsigned long);using Unlock=long (__stdcall*)(void*,unsigned long);using Release=unsigned long (__stdcall*)(void*);Lock lock=(Lock)d3dMethod(tex,19);Unlock unlock=(Unlock)d3dMethod(tex,20);Release release=(Release)d3dMethod(tex,2);if(!lock||!unlock||!release){if(release)release(tex);return false;}D3DLockedRect lr={};if(lock(tex,0,&lr,0,0)<0){release(tex);return false;}const unsigned char* src=bytes+mipOffset;for(unsigned long y=0;y<rows;++y)copyBytes((unsigned char*)lr.pBits+y*(unsigned long)lr.Pitch,src+y*pitch,pitch);unlock(tex,0);*out=tex;return true;}
static bool createRaidIconTexture(void* device,const unsigned char* bytes,unsigned long bytesSize,void** out,unsigned long* width,unsigned long* height){if(!device||!bytes||bytesSize<0x94||bytes[0]!='B'||bytes[1]!='L'||bytes[2]!='P'||bytes[3]!='2'||le32(bytes+4)!=1)return false;unsigned char comp=bytes[8],alphaBits=bytes[9],alphaType=bytes[10];unsigned long w=le32(bytes+0x0C),h=le32(bytes+0x10),off=le32(bytes+0x14),sz=le32(bytes+0x54);if(!w||!h||w>4096||h>4096||!off||!sz)return false;bool ok=false;if(comp==1)ok=createTextureRaw1(device,bytes,bytesSize,w,h,alphaBits,off,sz,out);else if(comp==2)ok=createTextureDxt(device,bytes,bytesSize,w,h,alphaBits,alphaType,off,sz,out);else if(comp==3)ok=createTextureRaw3(device,bytes,bytesSize,w,h,off,sz,out);if(ok){*width=w;*height=h;}return ok;}
static void releaseRaidIconTexture(){if(g_raidIconTexture){using Release=unsigned long (__stdcall*)(void*);Release release=(Release)d3dMethod(g_raidIconTexture,2);if(release)release(g_raidIconTexture);}g_raidIconTexture=0;g_raidIconTextureDevice=0;g_raidIconTextureWidth=0;g_raidIconTextureHeight=0;g_raidIconUCell=0.25f;g_raidIconVCell=0.5f;}
static bool ensureRaidIconTexture(void* device){if(!device)return false;if(g_raidIconTextureDevice&&g_raidIconTextureDevice!=device){releaseRaidIconTexture();g_raidIconTextureLoadAttempted=false;}if(g_raidIconTexture)return true;if(g_raidIconTextureLoadAttempted)return false;g_raidIconTextureLoadAttempted=true;unsigned char* bytes=0;unsigned long bytesSize=0,w=0,h=0;void* tex=0;if(!loadRaidTextureBytes(&bytes,&bytesSize))return false;bool ok=createRaidIconTexture(device,bytes,bytesSize,&tex,&w,&h);VirtualFree(bytes,0,MEM_RELEASE);if(!ok||!tex){if(tex){using Release=unsigned long (__stdcall*)(void*);Release release=(Release)d3dMethod(tex,2);if(release)release(tex);}return false;}g_raidIconTexture=tex;g_raidIconTextureDevice=device;g_raidIconTextureWidth=w;g_raidIconTextureHeight=h;g_raidIconUCell=0.25f;float square=(float)w*g_raidIconUCell;g_raidIconVCell=square/(float)h;if(!finitef(g_raidIconVCell)||g_raidIconVCell<=0.0f||g_raidIconVCell*2.0f>1.001f){releaseRaidIconTexture();return false;}return true;}
static void appendTexturedIcon(const ProjectedIcon& p){if(g_iconVertexCount+6>MOON_SLOT_COUNT*6||p.iconIndex<0||p.iconIndex>=8||!g_raidIconTextureWidth||!g_raidIconTextureHeight)return;float half=p.size*0.5f,left=p.x-half,right=p.x+half,top=p.y-half,bottom=p.y+half;int col=p.iconIndex%4,row=p.iconIndex/4;float iu=0.5f/(float)g_raidIconTextureWidth,iv=0.5f/(float)g_raidIconTextureHeight;float u0=col*g_raidIconUCell+iu,u1=(col+1)*g_raidIconUCell-iu,v0=row*g_raidIconVCell+iv,v1=(row+1)*g_raidIconVCell-iv;unsigned long white=0xFFFFFFFFUL;TexturedIconVertex v[6]={{left,top,0,1,white,u0,v0},{right,top,0,1,white,u1,v0},{left,bottom,0,1,white,u0,v1},{left,bottom,0,1,white,u0,v1},{right,top,0,1,white,u1,v0},{right,bottom,0,1,white,u1,v1}};for(int i=0;i<6;++i)g_iconVertices[g_iconVertexCount++]=v[i];}
static long __stdcall hookedPresent(void* device,const void* a,const void* b,void* hwnd,const void* dirty);
static bool writePresentSlot(void** slot,void* value){if(!slot||!readableRange(slot,sizeof(void*)))return false;DWORD old=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&old))return false;*slot=value;DWORD ignored=0;VirtualProtect(slot,sizeof(void*),old,&ignored);FlushInstructionCache(GetCurrentProcess(),slot,sizeof(void*));return true;}
static void uninstallPresent(){if(g_presentInstalled&&g_presentSlot&&g_presentNext&&readableRange(g_presentSlot,sizeof(void*))&&*g_presentSlot==(void*)&hookedPresent)writePresentSlot(g_presentSlot,(void*)g_presentNext);g_presentInstalled=false;g_presentSlot=0;g_presentNext=0;g_presentDevice=0;releaseRaidIconTexture();g_raidIconTextureLoadAttempted=false;}
static bool installPresent(void* device){if(!device)return false;void*** object=(void***)device;if(!readableRange(object,sizeof(void*))||!*object||!readableRange(*object,18*sizeof(void*)))return false;void** slot=&(*object)[17];if(g_presentInstalled&&g_presentDevice==device&&g_presentSlot==slot&&*slot==(void*)&hookedPresent)return true;uninstallPresent();PresentProc next=(PresentProc)(*slot);if(!next||next==(PresentProc)&hookedPresent)return false;g_presentNext=next;g_presentSlot=slot;g_presentDevice=device;if(!writePresentSlot(slot,(void*)&hookedPresent)){g_presentNext=0;g_presentSlot=0;g_presentDevice=0;return false;}g_presentInstalled=true;return true;}
static bool d3dGetViewport(void* device,D3DViewport* vp){using Fn=long (__stdcall*)(void*,D3DViewport*);Fn f=(Fn)d3dMethod(device,48);return f&&f(device,vp)>=0;}
static unsigned long argb(int a,int r,int g,int b){return ((unsigned long)(a&255)<<24)|((unsigned long)(r&255)<<16)|((unsigned long)(g&255)<<8)|(unsigned long)(b&255);}
static unsigned long moonColorValue(int selectedColor,int alpha){if(selectedColor<0||selectedColor>=8)return argb(alpha,255,255,255);const V3& v=g_colors[selectedColor].tint;int r=(int)(maxf(0.0f,minf(1.0f,v.x))*255.0f),g=(int)(maxf(0.0f,minf(1.0f,v.y))*255.0f),b=(int)(maxf(0.0f,minf(1.0f,v.z))*255.0f);return argb(alpha,r,g,b);}
static void fallbackPush(float x,float y,unsigned long color){if(g_fallbackVertexCount>=MOON_SLOT_COUNT*512)return;g_fallbackVertices[g_fallbackVertexCount++]={x,y,0.0f,1.0f,color};}
static void fallbackTriangle(float ax,float ay,float bx,float by,float cx,float cy,unsigned long color){fallbackPush(ax,ay,color);fallbackPush(bx,by,color);fallbackPush(cx,cy,color);}
static void fallbackQuad(float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,unsigned long color){fallbackTriangle(ax,ay,bx,by,cx,cy,color);fallbackTriangle(cx,cy,bx,by,dx,dy,color);}
static void fallbackRect(float cx,float cy,float width,float height,float angle,unsigned long color){float cs=cosNative(angle),sn=sinNative(angle),hx=width*0.5f,hy=height*0.5f;float lx[4]={-hx,hx,-hx,hx},ly[4]={-hy,-hy,hy,hy},x[4]={0},y[4]={0};for(int i=0;i<4;++i){x[i]=cx+lx[i]*cs-ly[i]*sn;y[i]=cy+lx[i]*sn+ly[i]*cs;}fallbackQuad(x[0],y[0],x[1],y[1],x[2],y[2],x[3],y[3],color);}
static void fallbackDisc(float cx,float cy,float radius,unsigned long color,int segments){const float PI=3.14159265358979323846f,step=2.0f*PI/(float)segments;for(int i=0;i<segments;++i){float a=step*(float)i,b=step*(float)(i+1);fallbackTriangle(cx,cy,cx+cosNative(a)*radius,cy+sinNative(a)*radius,cx+cosNative(b)*radius,cy+sinNative(b)*radius,color);}}
static void fallbackRing(float cx,float cy,float outerRadius,float innerRadius,unsigned long color,float startAngle,float endAngle,int segments){float step=(endAngle-startAngle)/(float)segments;for(int i=0;i<segments;++i){float a=startAngle+step*(float)i,b=startAngle+step*(float)(i+1);float ax=cx+cosNative(a)*outerRadius,ay=cy+sinNative(a)*outerRadius,bx=cx+cosNative(b)*outerRadius,by=cy+sinNative(b)*outerRadius,cx0=cx+cosNative(a)*innerRadius,cy0=cy+sinNative(a)*innerRadius,dx=cx+cosNative(b)*innerRadius,dy=cy+sinNative(b)*innerRadius;fallbackQuad(ax,ay,bx,by,cx0,cy0,dx,dy,color);}}
static void fallbackStar(float cx,float cy,float radius,unsigned long color){const float PI=3.14159265358979323846f;float x[10]={0},y[10]={0};for(int i=0;i<10;++i){float angle=-PI*0.5f+(float)i*PI/5.0f,rr=(i%2==0)?radius:radius*0.42f;x[i]=cx+cosNative(angle)*rr;y[i]=cy+sinNative(angle)*rr;}for(int i=1;i<9;++i)fallbackTriangle(x[0],y[0],x[i],y[i],x[i+1],y[i+1],color);}
static void fallbackShape(int icon,float cx,float cy,float size,unsigned long color){const float PI=3.14159265358979323846f,r=size*0.5f;switch(icon){case 0:fallbackStar(cx,cy,r,color);break;case 1:fallbackRing(cx,cy,r,r*0.67f,color,0.0f,2.0f*PI,32);break;case 2:fallbackQuad(cx,cy-r,cx+r,cy,cx-r,cy,cx,cy+r,color);break;case 3:fallbackTriangle(cx,cy-r,cx-r*0.94f,cy+r*0.82f,cx+r*0.94f,cy+r*0.82f,color);break;case 4:fallbackRing(cx,cy,r,r*0.62f,color,-2.25f,2.25f,28);break;case 5:fallbackQuad(cx-r,cy-r,cx+r,cy-r,cx-r,cy+r,cx+r,cy+r,color);break;case 6:fallbackRect(cx,cy,size*0.28f,size*1.18f,PI*0.25f,color);fallbackRect(cx,cy,size*0.28f,size*1.18f,-PI*0.25f,color);break;case 7:fallbackDisc(cx,cy-size*0.10f,r*0.82f,color,24);fallbackRect(cx,cy+size*0.28f,size*0.58f,size*0.38f,0.0f,color);break;default:break;}}
static void fallbackSkullCutouts(float cx,float cy,float size,unsigned long color){fallbackDisc(cx-size*0.16f,cy-size*0.12f,size*0.09f,color,14);fallbackDisc(cx+size*0.16f,cy-size*0.12f,size*0.09f,color,14);fallbackTriangle(cx,cy+size*0.02f,cx-size*0.06f,cy+size*0.15f,cx+size*0.06f,cy+size*0.15f,color);fallbackRect(cx-size*0.11f,cy+size*0.33f,size*0.06f,size*0.22f,0.0f,color);fallbackRect(cx,cy+size*0.33f,size*0.06f,size*0.22f,0.0f,color);fallbackRect(cx+size*0.11f,cy+size*0.33f,size*0.06f,size*0.22f,0.0f,color);}
static bool d3dRenderFallbackIcons(void* device){if(!device)return false;g_fallbackVertexCount=0;unsigned long outline=argb(210,8,8,12),cutout=argb(235,10,10,14);for(unsigned long i=0;i<MOON_SLOT_COUNT;++i){const ProjectedIcon& p=g_projected[i];if(!p.visible)continue;fallbackShape(p.iconIndex,p.x,p.y,p.size+6.0f,outline);fallbackShape(p.iconIndex,p.x,p.y,p.size,moonColorValue(p.colorIndex,245));if(p.iconIndex==7)fallbackSkullCutouts(p.x,p.y,p.size,cutout);}if(!g_fallbackVertexCount)return true;using CreateStateBlockFn=long (__stdcall*)(void*,unsigned long,void**);using StateFn=long (__stdcall*)(void*);using ReleaseFn=unsigned long (__stdcall*)(void*);using SetTextureFn=long (__stdcall*)(void*,unsigned long,void*);using SetShaderFn=long (__stdcall*)(void*,void*);using SetRenderStateFn=long (__stdcall*)(void*,unsigned long,unsigned long);using SetStageFn=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long);using SetFVFFn=long (__stdcall*)(void*,unsigned long);using DrawUPFn=long (__stdcall*)(void*,unsigned long,unsigned long,const void*,unsigned long);CreateStateBlockFn create=(CreateStateBlockFn)d3dMethod(device,59);SetTextureFn setTex=(SetTextureFn)d3dMethod(device,65);SetShaderFn setVS=(SetShaderFn)d3dMethod(device,92);SetShaderFn setPS=(SetShaderFn)d3dMethod(device,107);SetRenderStateFn setRS=(SetRenderStateFn)d3dMethod(device,57);SetStageFn setTS=(SetStageFn)d3dMethod(device,67);SetFVFFn setFVF=(SetFVFFn)d3dMethod(device,89);DrawUPFn draw=(DrawUPFn)d3dMethod(device,83);if(!create||!setTex||!setVS||!setPS||!setRS||!setTS||!setFVF||!draw)return false;void* state=0;if(create(device,1,&state)<0||!state)return false;StateFn capture=(StateFn)d3dMethod(state,4),apply=(StateFn)d3dMethod(state,5);ReleaseFn release=(ReleaseFn)d3dMethod(state,2);if(!capture||!apply||!release){if(release)release(state);return false;}capture(state);setTex(device,0,0);setVS(device,0);setPS(device,0);setRS(device,7,0);setRS(device,14,0);setRS(device,137,0);setRS(device,22,1);setRS(device,15,0);setRS(device,27,1);setRS(device,19,5);setRS(device,20,6);setTS(device,0,1,2);setTS(device,0,2,0);setTS(device,0,4,2);setTS(device,0,5,0);setFVF(device,0x44UL);draw(device,4,g_fallbackVertexCount/3,g_fallbackVertices,sizeof(IconVertex));apply(state);release(state);return true;}
static bool d3dRenderIcons(void* device){if(!device)return false;if(!ensureRaidIconTexture(device))return d3dRenderFallbackIcons(device);g_iconVertexCount=0;for(unsigned long i=0;i<MOON_SLOT_COUNT;++i)if(g_projected[i].visible)appendTexturedIcon(g_projected[i]);if(!g_iconVertexCount)return true;using CreateStateBlockFn=long (__stdcall*)(void*,unsigned long,void**);using StateFn=long (__stdcall*)(void*);using ReleaseFn=unsigned long (__stdcall*)(void*);using SetTextureFn=long (__stdcall*)(void*,unsigned long,void*);using SetShaderFn=long (__stdcall*)(void*,void*);using SetRenderStateFn=long (__stdcall*)(void*,unsigned long,unsigned long);using SetStageFn=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long);using SetSamplerFn=long (__stdcall*)(void*,unsigned long,unsigned long,unsigned long);using SetFVFFn=long (__stdcall*)(void*,unsigned long);using DrawUPFn=long (__stdcall*)(void*,unsigned long,unsigned long,const void*,unsigned long);CreateStateBlockFn create=(CreateStateBlockFn)d3dMethod(device,59);SetTextureFn setTex=(SetTextureFn)d3dMethod(device,65);SetShaderFn setVS=(SetShaderFn)d3dMethod(device,92);SetShaderFn setPS=(SetShaderFn)d3dMethod(device,107);SetRenderStateFn setRS=(SetRenderStateFn)d3dMethod(device,57);SetStageFn setTS=(SetStageFn)d3dMethod(device,67);SetSamplerFn setSS=(SetSamplerFn)d3dMethod(device,69);SetFVFFn setFVF=(SetFVFFn)d3dMethod(device,89);DrawUPFn draw=(DrawUPFn)d3dMethod(device,83);if(!create||!setTex||!setVS||!setPS||!setRS||!setTS||!setSS||!setFVF||!draw)return false;void* state=0;if(create(device,1,&state)<0||!state)return false;StateFn capture=(StateFn)d3dMethod(state,4),apply=(StateFn)d3dMethod(state,5);ReleaseFn release=(ReleaseFn)d3dMethod(state,2);if(!capture||!apply||!release){if(release)release(state);return false;}capture(state);setTex(device,0,g_raidIconTexture);setVS(device,0);setPS(device,0);setRS(device,7,0);setRS(device,14,0);setRS(device,137,0);setRS(device,22,1);setRS(device,15,1);setRS(device,24,1);setRS(device,25,7);setRS(device,27,1);setRS(device,19,5);setRS(device,20,6);setTS(device,0,1,4);setTS(device,0,2,2);setTS(device,0,3,0);setTS(device,0,4,4);setTS(device,0,5,2);setTS(device,0,6,0);setTS(device,1,1,1);setTS(device,1,4,1);setSS(device,0,6,2);setSS(device,0,5,2);setSS(device,0,7,0);setSS(device,0,1,3);setSS(device,0,2,3);setFVF(device,0x144UL);draw(device,4,g_iconVertexCount/3,g_iconVertices,sizeof(TexturedIconVertex));apply(state);release(state);return true;}
static long __stdcall hookedPresent(void* device,const void* a,const void* b,void* hwnd,const void* dirty){d3dRenderIcons(device);PresentProc next=g_presentNext;return next?next(device,a,b,hwnd,dirty):-1;}

static bool currentWorldFrame(unsigned long* frame){if(!frame)return false;unsigned long f=*(unsigned long*)WORLD_FRAME_PTR;if((f&3UL)!=0||!readableRange((void*)f,0x3B0UL))return false;*frame=f;return true;}
static bool projectWorld(void* device,const V3& in,float* ox,float* oy){
    if(!device||!ox||!oy)return false;unsigned long frame=0;if(!currentWorldFrame(&frame))return false;V3 world=in,ddc={0,0,0};if(!((WorldToScreenProc)WORLD_TO_SCREEN)(frame,&world.x,&ddc.x))return false;unsigned long frame2=0;if(!currentWorldFrame(&frame2)||frame2!=frame)return false;float nx=-1,ny=-1;((DdcToNdcProc)DDC_TO_NDC)(&nx,&ny,ddc.x,ddc.y);D3DViewport vp={};if(!d3dGetViewport(device,&vp)||vp.Width<2||vp.Height<2)return false;float bottom=*(float*)(frame+0x3A0UL),left=*(float*)(frame+0x3A4UL);if(!finitef(bottom)||!finitef(left))return false;float w=(float)vp.Width,h=(float)vp.Height,x=left*w+nx*w,y=h-ny*h-bottom*h;if(!finitef(x)||!finitef(y)||x<0||y<0||x>w||y>h)return false;*ox=x;*oy=y;return true;
}

static void updateMoonProjection(void* device){
    resetProjected();if(!device)return;D3DViewport vp={};if(!d3dGetViewport(device,&vp))return;float h=(float)vp.Height;
    for(unsigned long j=0;j<MOON_SLOT_COUNT;++j){Slot& s=g_slots[MOON_BEGIN+j];if(!s.active||s.owner!=OWNER_MOON||!s.model)continue;V3 world={s.x,s.y,s.z+ICON_WORLD_HEIGHT};float x=0,y=0;if(!projectWorld(device,world,&x,&y))continue;ProjectedIcon& p=g_projected[j];p.visible=true;p.x=x;p.y=y;p.size=maxf(30.0f,minf(58.0f,BASE_ICON_PIXELS*h/768.0f));p.colorIndex=s.moonColor;p.iconIndex=s.moonIcon;}
}

static void* d3dDeviceFromGx(unsigned long gx){if(!gx||!readableRange((void*)(gx+GX_D3D_DEVICE_OFFSET),sizeof(void*)))return 0;void* d=*(void**)(gx+GX_D3D_DEVICE_OFFSET);if(!d||((unsigned long)d&1UL))return 0;return d;}

} // namespace

void resetAllStateNoRelease(){for(unsigned long i=0;i<TOTAL_SLOT_COUNT;++i)zeroSlot(g_slots[i]);resetProjected();uninstallPresent();setErr("RESET");}

bool set(const char* key,const char* modelPath,float x,float y,float z,float scale,float yawDegrees,char* normalized,unsigned long normalizedCap){
    ++g_setCalls;if(normalized&&normalizedCap)normalized[0]=0;if(!validKey(key)){setErr("autorange_key_invalid");return false;}char path[PATH_CAP]={0};if(!normalizePath(modelPath,path,sizeof(path))){setErr("autorange_path_invalid");return false;}if(normalized&&normalizedCap)cpy(normalized,normalizedCap,path);if(!validTransform(x,y,z,scale,yawDegrees)){setErr("autorange_transform_invalid");return false;}
    if(Slot* existing=findArSlot(key)){existing->x=x;existing->y=y;existing->z=z;existing->scale=scale;existing->yaw=yawDegrees;if(!contextValid(*existing)){++g_staleDrops;zeroSlot(*existing);}else{applyMatrix(*existing);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(existing->model,1);if(!existing->hidden&&!*(void**)((unsigned char*)existing->model+0x44)){((SetBooleanProc)ATTACH_RENDER_LIST)(existing->model,1);++g_reattachCalls;}if(!existing->renderReady)ensureReady(*existing,0);++g_setOK;setErr("autorange_set_existing");return true;}}
    unsigned long index=allocateArIndex(key);if(index>=TOTAL_SLOT_COUNT){setErr("autorange_slots_full");return false;}V3 white={1,1,1};if(!createAtIndex(index,OWNER_AUTORANGE,path,x,y,z,scale,yawDegrees,1.0f,white,key,-1,-1))return false;++g_setOK;setErr("autorange_ready");return true;
}

bool move(const char* key,float x,float y,float z,float scale,float yawDegrees){++g_moveCalls;Slot* s=findArSlot(key);if(!s||!validTransform(x,y,z,scale,yawDegrees)){setErr("autorange_move_invalid");return false;}if(!contextValid(*s)){++g_staleDrops;zeroSlot(*s);setErr("autorange_move_context_invalid");return false;}s->x=x;s->y=y;s->z=z;s->scale=scale;s->yaw=yawDegrees;applyMatrix(*s);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s->model,1);if(!s->hidden&&!*(void**)((unsigned char*)s->model+0x44)){((SetBooleanProc)ATTACH_RENDER_LIST)(s->model,1);++g_reattachCalls;}if(!s->renderReady)ensureReady(*s,0);setErr("autorange_moved");return true;}
bool hide(const char* key){++g_hideCalls;Slot* s=findArSlot(key);if(!s){setErr("autorange_hide_missing");return false;}if(s->hidden)return true;if(!contextValid(*s)){++g_staleDrops;zeroSlot(*s);return false;}((SetBooleanProc)ATTACH_RENDER_LIST)(s->model,0);s->hidden=true;setErr("autorange_hidden");return true;}
bool show(const char* key){++g_showCalls;Slot* s=findArSlot(key);if(!s||!contextValid(*s)){if(s){++g_staleDrops;zeroSlot(*s);}return false;}s->hidden=false;applyMatrix(*s);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s->model,1);((SetBooleanProc)ATTACH_RENDER_LIST)(s->model,1);if(!s->renderReady)ensureReady(*s,0);setErr("autorange_shown");return true;}
bool restart(const char* key){++g_restartCalls;Slot* s=findArSlot(key);if(!s||!contextValid(*s)){if(s){++g_staleDrops;zeroSlot(*s);}return false;}((SetSequenceProc)SET_SEQUENCE)(s->model,-1,0,-1,0,1.0f,1,1);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s->model,1);if(!s->hidden&&!*(void**)((unsigned char*)s->model+0x44)){((SetBooleanProc)ATTACH_RENDER_LIST)(s->model,1);++g_reattachCalls;}if(!s->renderReady)ensureReady(*s,0);setErr("autorange_restarted");return true;}
void clear(const char* key){Slot* s=findArSlot(key);if(s){releaseSlot(*s,true);setErr("autorange_cleared");}else setErr("autorange_clear_missing");}
void clearAll(){for(unsigned long i=AR_BEGIN;i<AR_BEGIN+SLOT_COUNT;++i)if(g_slots[i].active||g_slots[i].model)releaseSlot(g_slots[i],true);setErr("autorange_cleared_all");}

bool moonSet(const char* color,const char* icon,float x,float y,float z,char* normalizedColor,unsigned long colorCap,char* normalizedIcon,unsigned long iconCap){
    ++g_moonSetCalls;if(normalizedColor&&colorCap)normalizedColor[0]=0;if(normalizedIcon&&iconCap)normalizedIcon[0]=0;int c=moonColorIndex(color);if(c<0){setErr("moon_unknown_color");return false;}int ic=moonIconIndex(icon);if(ic<0)ic=g_colors[c].defaultIcon;if(normalizedColor&&colorCap)cpy(normalizedColor,colorCap,g_colors[c].name);if(normalizedIcon&&iconCap)cpy(normalizedIcon,iconCap,g_icons[ic]);if(!finitef(x)||!finitef(y)||!finitef(z)){setErr("moon_invalid_position");return false;}unsigned long idx=moonIndex(c,ic);if(!createAtIndex(idx,OWNER_MOON,MOON_BEAM_PATH,x,y,z+0.05f,1.0f,0.0f,1.0f,g_colors[c].tint,0,c,ic))return false;++g_moonSetOK;setErr("moon_active_ready");return true;
}
void moonClearAll(){unsigned long worldFrame=0;if(currentContext()==0||!currentWorldFrame(&worldFrame)){for(unsigned long i=MOON_BEGIN;i<MOON_BEGIN+MOON_SLOT_COUNT;++i)if(g_slots[i].active||g_slots[i].model)zeroSlot(g_slots[i]);resetProjected();setErr("moon_cleared_all_world_not_ready");return;}for(unsigned long i=MOON_BEGIN;i<MOON_BEGIN+MOON_SLOT_COUNT;++i)if(g_slots[i].active||g_slots[i].model)releaseSlot(g_slots[i],false);resetProjected();setErr("moon_cleared_all");}
unsigned long moonActiveCount(){unsigned long n=0;for(unsigned long i=MOON_BEGIN;i<MOON_BEGIN+MOON_SLOT_COUNT;++i)if(g_slots[i].active&&g_slots[i].owner==OWNER_MOON)++n;return n;}

bool moonSetCustom(const char* color,const char* icon,float x,float y,float z,const char* modelPath,float scale,float yawDegrees,char* normalizedPath,unsigned long pathCap,char* normalizedColor,unsigned long colorCap,char* normalizedIcon,unsigned long iconCap){
    if(normalizedPath&&pathCap)normalizedPath[0]=0;if(normalizedColor&&colorCap)normalizedColor[0]=0;if(normalizedIcon&&iconCap)normalizedIcon[0]=0;
    int c=moonColorIndex(color);if(c<0){setErr("custom_unknown_color");return false;}int ic=moonIconIndex(icon);if(ic<0)ic=g_colors[c].defaultIcon;
    char path[PATH_CAP]={0};if(!normalizePath(modelPath,path,sizeof(path))||!finitef(scale)||!finitef(yawDegrees)||scale<0.10f||scale>5.00f){setErr("custom_model_invalid");return false;}
    if(normalizedPath&&pathCap)cpy(normalizedPath,pathCap,path);if(normalizedColor&&colorCap)cpy(normalizedColor,colorCap,g_colors[c].name);if(normalizedIcon&&iconCap)cpy(normalizedIcon,iconCap,g_icons[ic]);
    V3 white={1.0f,1.0f,1.0f};unsigned long idx=moonIndex(c,ic);if(!createAtIndex(idx,OWNER_MOON,path,x,y,z+0.05f,scale,yawDegrees,1.0f,white,0,c,ic))return false;setErr("custom_active_ready");return true;
}

bool moonAdvancedPreviewSet(const char* modelPath,float x,float y,float z,float scale,float yawDegrees,char* normalizedPath,unsigned long pathCap){
    if(normalizedPath&&pathCap)normalizedPath[0]=0;char path[PATH_CAP]={0};if(!normalizePath(modelPath,path,sizeof(path))||!finitef(x)||!finitef(y)||!finitef(z)||!finitef(scale)||!finitef(yawDegrees)||scale<0.10f||scale>5.00f){setErr("advanced_validate");return false;}if(normalizedPath&&pathCap)cpy(normalizedPath,pathCap,path);V3 white={1,1,1};return createAtIndex(ADV_PREVIEW_INDEX,OWNER_MOON_ADV_PREVIEW,path,x,y,z+0.05f,scale,yawDegrees,1.0f,white,0,-1,-1);
}
bool moonAdvancedPreviewTransform(float scale,float yawDegrees){Slot& s=g_slots[ADV_PREVIEW_INDEX];if(!s.active||s.owner!=OWNER_MOON_ADV_PREVIEW||!s.model||!finitef(scale)||!finitef(yawDegrees)||scale<0.10f||scale>5.00f){setErr("advanced_transform_invalid");return false;}unsigned long wf=0;void* live=currentContext();if(!live||!currentWorldFrame(&wf)||s.context!=live){++g_staleDrops;++g_nativeDrops;zeroSlot(s);setErr("advanced_transform_invalid");return false;}s.scale=scale;s.yaw=yawDegrees;applyMatrix(s);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s.model,1);if(!*(void**)((unsigned char*)s.model+0x44)){((SetBooleanProc)ATTACH_RENDER_LIST)(s.model,1);++g_reattachCalls;}if(!s.renderReady)ensureReady(s,0);setErr("advanced_transform_updated");return true;}
void moonAdvancedPreviewClear(){Slot& s=g_slots[ADV_PREVIEW_INDEX];if(!s.active&&!s.model)return;unsigned long wf=0;if(currentContext()==0||!currentWorldFrame(&wf)){zeroSlot(s);setErr("advanced_cleared_stale_context");return;}releaseSlot(s,false);setErr("advanced_cleared");}
bool moonAdvancedPreviewStatus(bool* active,bool* renderReady,char* path,unsigned long pathCap,float* scale,float* yawDegrees){Slot& s=g_slots[ADV_PREVIEW_INDEX];if(active)*active=s.active&&s.owner==OWNER_MOON_ADV_PREVIEW&&s.model;if(renderReady)*renderReady=s.renderReady;if(path&&pathCap)cpy(path,pathCap,s.path);if(scale)*scale=s.scale;if(yawDegrees)*yawDegrees=s.yaw;return true;}
unsigned long worldContextToken(){unsigned long wf=0;void* live=currentContext();return live&&currentWorldFrame(&wf)?(unsigned long)live:0UL;}

bool moonPreviewSet(float x,float y,float z){if(!finitef(x)||!finitef(y)||!finitef(z))return false;V3 green={0.12f,1.0f,0.30f};return createAtIndex(PREVIEW_INDEX,OWNER_MOON_PREVIEW,TARGET_CIRCLE_PATH,x,y,z+0.03f,1.0f,0.0f,0.78f,green,0,-1,-1);}
bool moonPreviewMove(float x,float y,float z){Slot& s=g_slots[PREVIEW_INDEX];if(!s.active||s.owner!=OWNER_MOON_PREVIEW||!contextValid(s)||!finitef(x)||!finitef(y)||!finitef(z))return false;s.x=x;s.y=y;s.z=z+0.03f;applyMatrix(s);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s.model,1);if(!*(void**)((unsigned char*)s.model+0x44)){((SetBooleanProc)ATTACH_RENDER_LIST)(s.model,1);++g_reattachCalls;}if(!s.renderReady)ensureReady(s,0);return true;}
void moonPreviewClear(){Slot& s=g_slots[PREVIEW_INDEX];if(!s.active&&!s.model)return;unsigned long worldFrame=0;if(currentContext()==0||!currentWorldFrame(&worldFrame)){zeroSlot(s);setErr("moon_preview_cleared_world_not_ready");return;}releaseSlot(s,false);}

static void dropAllSlotsWithoutRelease(const char* stage){
    bool dropped=false;
    for(unsigned long i=0;i<TOTAL_SLOT_COUNT;++i){Slot& s=g_slots[i];if(!s.active&&!s.model)continue;++g_staleDrops;++g_nativeDrops;zeroSlot(s);dropped=true;}
    resetProjected();
    if(dropped)++g_worldDropNoRelease;
    setErr(stage);
}

void onWorldLeaving(){
    ++g_worldLeaves;
    unsigned long worldFrame=0;
    if(currentContext()==0||!currentWorldFrame(&worldFrame)){dropAllSlotsWithoutRelease("world_leave_drop_without_release");return;}
    for(unsigned long i=0;i<TOTAL_SLOT_COUNT;++i){Slot& s=g_slots[i];if(s.active||s.model)releaseSlot(s,false);}
    resetProjected();
    setErr("world_leave_cleared");
}

void onWorldEntering(){++g_worldEnters;resetProjected();setErr("world_entering");}

void updateNativeFrame(unsigned long gxDevice){
    ++g_nativeFrames;
    void* live=currentContext();
    unsigned long worldFrame=0;
    const bool worldReady=live!=0&&currentWorldFrame(&worldFrame);
    // Exact nativeM2Test rule: do not touch stale model pointers once either
    // WorldContext or WorldFrame is no longer safe. Drop references only.
    if(!worldReady){dropAllSlotsWithoutRelease("shared_native_world_not_ready_drop_without_release");return;}
    for(unsigned long i=0;i<TOTAL_SLOT_COUNT;++i){Slot& s=g_slots[i];if(!s.active||!s.model)continue;if(s.context!=live){++g_staleDrops;++g_nativeDrops;zeroSlot(s);setErr("shared_native_context_changed");continue;}if(s.hidden)continue;applyMatrix(s);((SetBooleanProc)SET_ACTIVE_TIMESTAMP)(s.model,1);void* link=*(void**)((unsigned char*)s.model+0x44);if(!link){((SetBooleanProc)ATTACH_RENDER_LIST)(s.model,1);++g_reattachCalls;link=*(void**)((unsigned char*)s.model+0x44);if(!link){setErr("shared_native_reattach_failed");continue;}}ensureReady(s,0);++g_nativeRefreshes;}
    // Original MoonMarker keeps its Present hook installed after Clear/world
    // transition. Only projected data is cleared when there are no markers.
    if(anyMoonActive()){void* d=d3dDeviceFromGx(gxDevice);updateMoonProjection(d);if(d)installPresent(d);}else resetProjected();
    setErr("shared_native_active_ready");
}
void shutdownMoonPresentation(){moonPreviewClear();moonClearAll();uninstallPresent();}

unsigned long activeCount(){unsigned long n=0;for(unsigned long i=AR_BEGIN;i<AR_BEGIN+SLOT_COUNT;++i)if(g_slots[i].active&&g_slots[i].owner==OWNER_AUTORANGE)++n;return n;}
unsigned long hiddenCount(){unsigned long n=0;for(unsigned long i=AR_BEGIN;i<AR_BEGIN+SLOT_COUNT;++i)if(g_slots[i].active&&g_slots[i].owner==OWNER_AUTORANGE&&g_slots[i].hidden)++n;return n;}
unsigned long modelCount(){unsigned long n=0;for(unsigned long i=0;i<TOTAL_SLOT_COUNT;++i)if(g_slots[i].model)++n;return n;}
const char* lastError(){return g_lastError;}
void stats(char* out,unsigned long cap){if(!out||!cap)return;out[0]=0;cat(out,cap,"V|active=");appendUInt(out,cap,activeCount());cat(out,cap,"|hidden=");appendUInt(out,cap,hiddenCount());cat(out,cap,"|models=");appendUInt(out,cap,modelCount());cat(out,cap,"|keys=");appendUInt(out,cap,activeCount());cat(out,cap,"|moon=");appendUInt(out,cap,moonActiveCount());cat(out,cap,"|set=");appendUInt(out,cap,g_setCalls);cat(out,cap,"|setOK=");appendUInt(out,cap,g_setOK);cat(out,cap,"|setFail=");appendUInt(out,cap,g_setCalls-g_setOK);cat(out,cap,"|move=");appendUInt(out,cap,g_moveCalls);cat(out,cap,"|hide=");appendUInt(out,cap,g_hideCalls);cat(out,cap,"|show=");appendUInt(out,cap,g_showCalls);cat(out,cap,"|restart=");appendUInt(out,cap,g_restartCalls);cat(out,cap,"|clear=");appendUInt(out,cap,g_clearCalls);cat(out,cap,"|clearSafe=");appendUInt(out,cap,g_clearSafe);cat(out,cap,"|clearStale=");appendUInt(out,cap,g_clearStale);cat(out,cap,"|reattach=");appendUInt(out,cap,g_reattachCalls);cat(out,cap,"|staleDrop=");appendUInt(out,cap,g_staleDrops);cat(out,cap,"|nativeFrame=");appendUInt(out,cap,g_nativeFrames);cat(out,cap,"|nativeRefresh=");appendUInt(out,cap,g_nativeRefreshes);cat(out,cap,"|nativeDrop=");appendUInt(out,cap,g_nativeDrops);cat(out,cap,"|moonSet=");appendUInt(out,cap,g_moonSetCalls);cat(out,cap,"|moonSetOK=");appendUInt(out,cap,g_moonSetOK);cat(out,cap,"|present=");appendUInt(out,cap,g_presentInstalled?1:0);cat(out,cap,"|worldLeave=");appendUInt(out,cap,g_worldLeaves);cat(out,cap,"|worldEnter=");appendUInt(out,cap,g_worldEnters);cat(out,cap,"|worldDropNoRelease=");appendUInt(out,cap,g_worldDropNoRelease);cat(out,cap,"|last=");cat(out,cap,g_lastError);}

} // namespace TysVisual
