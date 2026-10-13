#include <windows.h>
#include "m2scan_media.h"

namespace TysScanMedia {
namespace {

constexpr unsigned long SFILE_OPEN_ARCHIVE=0x00648DD0UL;
constexpr unsigned long SFILE_OPEN_FILE_EX=0x006477C0UL;
constexpr unsigned long SFILE_READ_FILE=0x00648460UL;
constexpr unsigned long SFILE_GET_FILE_SIZE=0x006487F0UL;
constexpr unsigned long SFILE_CLOSE_FILE=0x00648730UL;
constexpr unsigned long SFILE_CLOSE_ARCHIVE=0x00648EF0UL;
constexpr unsigned long MAX_LISTFILE=64UL*1024UL*1024UL;
constexpr unsigned long MAX_RESULTS=120000UL;
constexpr unsigned long MAX_ARCHIVES=256UL;
constexpr unsigned long HASH_SLOTS=262144UL;
constexpr unsigned long MEDIA_MAX=1024UL;
constexpr unsigned short SRC_BUILTIN=0xFFFEu;
constexpr unsigned short SRC_LOOSE=0xFFFFu;
constexpr unsigned long MPQ_MAGIC=0x1A51504DUL;
constexpr unsigned long MPQ_FILE_IMPLODE=0x00000100UL;
constexpr unsigned long MPQ_FILE_COMPRESS=0x00000200UL;
constexpr unsigned long MPQ_FILE_ENCRYPTED=0x00010000UL;
constexpr unsigned long MPQ_FILE_SINGLE_UNIT=0x01000000UL;
constexpr unsigned long MPQ_FILE_EXISTS=0x80000000UL;

using SFileOpenArchiveFn=BOOL (__stdcall*)(const char*,DWORD,DWORD,void**);
using SFileOpenFileExFn=BOOL (__stdcall*)(void*,const char*,DWORD,void**);
using SFileReadFileFn=BOOL (__stdcall*)(void*,void*,DWORD,DWORD*,void*,DWORD);
using SFileGetFileSizeFn=DWORD (__stdcall*)(void*,DWORD*);
using SFileCloseFileFn=BOOL (__stdcall*)(void*);
using SFileCloseArchiveFn=BOOL (__stdcall*)(void*);

struct Archive { char full[MAX_PATH]; char name[MAX_PATH]; };
struct Result { char path[241]; unsigned short source; };
struct MpqHash { unsigned long a,b,localePlatform,blockIndex; };
struct MpqBlock { unsigned long filePos,compressedSize,fileSize,flags; };
#pragma pack(push,1)
struct MpqHeader { unsigned long magic,headerSize,archiveSize; unsigned short version,blockShift; unsigned long hashOff,blockOff,hashCount,blockCount; };
#pragma pack(pop)

static HANDLE g_heap=0;
static Archive g_archives[MAX_ARCHIVES];
static unsigned long g_archiveCount=0,g_archiveIndex=0;
static Result* g_results=0; static unsigned long g_resultCount=0,g_resultCap=0;
static unsigned long* g_hash=0;
static unsigned long* g_legacyHash=0;
static unsigned long* g_legacyIndices=0; static unsigned long g_legacyCount=0;
static bool g_running=false,g_complete=false;
static char g_current[MAX_PATH]={0};
static char g_error[160]={0};
static char g_root[MAX_PATH]={0};
static char g_media[MEDIA_MAX][MAX_PATH];
static unsigned long g_mediaCount=0;
static char g_mediaStatus[48]="NOT_SCANNED";
static unsigned long g_crypt[0x500]; static bool g_cryptReady=false;

static unsigned long slen(const char* s){unsigned long n=0;if(s)while(s[n])++n;return n;}
static char lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static char upper(char c){return c>='a'&&c<='z'?(char)(c-32):c;}
static bool eqI(const char* a,const char* b){if(!a||!b)return false;while(*a&&*b){if(lower(*a)!=lower(*b))return false;++a;++b;}return *a==*b;}
static bool endsI(const char* s,const char* e){unsigned long a=slen(s),b=slen(e);if(a<b)return false;return eqI(s+a-b,e);}
static void cpy(char* d,unsigned long cap,const char* s){if(!cap)return;unsigned long i=0;if(s)for(;s[i]&&i+1<cap;++i)d[i]=s[i];d[i]=0;}
static void cat(char* d,unsigned long cap,const char* s){unsigned long n=slen(d);if(n<cap)cpy(d+n,cap-n,s);}
static const char* basename(const char* p){const char* r=p?p:"";if(p)for(;*p;++p)if(*p=='\\'||*p=='/')r=p+1;return r;}
static bool isSpace(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c==0;}
static void normalizePath(char* out,unsigned long cap,const char* in,unsigned long n){
    if(!cap)return; unsigned long begin=0,end=n; while(begin<end&&isSpace(in[begin]))++begin; while(end>begin&&isSpace(in[end-1]))--end;
    if(end-begin>=2&&in[begin]=='"'&&in[end-1]=='"'){++begin;--end;}
    unsigned long j=0;for(unsigned long i=begin;i<end&&j+1<cap;++i){char c=in[i];out[j++]=(c=='/')?'\\':c;}out[j]=0;
}
static bool safeModelPath(const char* p){
    unsigned long n=slen(p);if(!n||n>240||p[0]=='\\'||p[0]=='/'||!((n>=3&&endsI(p,".m2"))||(n>=4&&endsI(p,".mdx"))))return false;
    for(unsigned long i=0;i<n;++i){unsigned char c=(unsigned char)p[i];if(c<0x20||c>0x7E||c==':')return false;if(c=='.'&&i+1<n&&p[i+1]=='.')return false;}return true;
}
static unsigned long hashPath(const char* path,unsigned short src){unsigned long h=2166136261UL;h^=(unsigned char)(src&255);h*=16777619UL;h^=(unsigned char)(src>>8);h*=16777619UL;for(;*path;++path){h^=(unsigned char)lower(*path);h*=16777619UL;}return h?h:1;}
static unsigned long hashLegacyPath(const char* path){unsigned long h=2166136261UL;for(;*path;++path){h^=(unsigned char)lower(*path);h*=16777619UL;}return h?h:1;}
static bool sourceEq(unsigned short a,unsigned short b){return a==b;}
static bool ensureHeap(){if(g_heap)return true;g_heap=HeapCreate(0,0,0);return g_heap!=0;}
static void freeScan(){if(g_heap){if(g_results)HeapFree(g_heap,0,g_results);if(g_hash)HeapFree(g_heap,0,g_hash);if(g_legacyHash)HeapFree(g_heap,0,g_legacyHash);if(g_legacyIndices)HeapFree(g_heap,0,g_legacyIndices);}g_results=0;g_hash=0;g_legacyHash=0;g_legacyIndices=0;g_resultCount=0;g_legacyCount=0;g_resultCap=0;}
static bool ensureResults(){
    if(g_results&&g_hash&&g_legacyHash&&g_legacyIndices)return true;if(!ensureHeap())return false;
    g_resultCap=4096;g_results=(Result*)HeapAlloc(g_heap,8,sizeof(Result)*g_resultCap);g_hash=(unsigned long*)HeapAlloc(g_heap,8,sizeof(unsigned long)*HASH_SLOTS);g_legacyHash=(unsigned long*)HeapAlloc(g_heap,8,sizeof(unsigned long)*HASH_SLOTS);g_legacyIndices=(unsigned long*)HeapAlloc(g_heap,8,sizeof(unsigned long)*g_resultCap);
    if(!g_results||!g_hash||!g_legacyHash||!g_legacyIndices){freeScan();return false;}for(unsigned long i=0;i<HASH_SLOTS;++i){g_hash[i]=0;g_legacyHash[i]=0;}return true;
}
static bool growResults(){
    if(g_resultCount<g_resultCap)return true;if(g_resultCap>=MAX_RESULTS)return false;unsigned long nc=g_resultCap*2;if(nc>MAX_RESULTS)nc=MAX_RESULTS;
    Result* nr=(Result*)HeapAlloc(g_heap,8,sizeof(Result)*nc);unsigned long* nl=(unsigned long*)HeapAlloc(g_heap,8,sizeof(unsigned long)*nc);if(!nr||!nl){if(nr)HeapFree(g_heap,0,nr);if(nl)HeapFree(g_heap,0,nl);return false;}
    for(unsigned long i=0;i<g_resultCount;++i)nr[i]=g_results[i];for(unsigned long i=0;i<g_legacyCount;++i)nl[i]=g_legacyIndices[i];HeapFree(g_heap,0,g_results);HeapFree(g_heap,0,g_legacyIndices);g_results=nr;g_legacyIndices=nl;g_resultCap=nc;return true;
}
static void setErr(const char* s){cpy(g_error,sizeof(g_error),s?s:"");}
static bool addResult(const char* raw,unsigned long rawLen,unsigned short source){
    if(g_resultCount>=MAX_RESULTS||!ensureResults())return false;char p[241]={0};normalizePath(p,sizeof(p),raw,rawLen);if(!safeModelPath(p))return false;
    unsigned long h=hashPath(p,source),slot=h&(HASH_SLOTS-1);for(unsigned long probe=0;probe<HASH_SLOTS;++probe){unsigned long v=g_hash[slot];if(!v)break;Result& r=g_results[v-1];if(sourceEq(r.source,source)&&eqI(r.path,p))return false;slot=(slot+1)&(HASH_SLOTS-1);}if(!growResults())return false;
    Result& r=g_results[g_resultCount];cpy(r.path,sizeof(r.path),p);r.source=source;unsigned long resultIndex=g_resultCount;unsigned long idx=g_resultCount+1;g_resultCount++;slot=h&(HASH_SLOTS-1);while(g_hash[slot])slot=(slot+1)&(HASH_SLOTS-1);g_hash[slot]=idx;
    unsigned long lh=hashLegacyPath(p),ls=lh&(HASH_SLOTS-1);bool legacyNew=true;for(unsigned long probe=0;probe<HASH_SLOTS;++probe){unsigned long v=g_legacyHash[ls];if(!v)break;if(eqI(g_results[v-1].path,p)){legacyNew=false;break;}ls=(ls+1)&(HASH_SLOTS-1);}if(legacyNew){g_legacyIndices[g_legacyCount++]=resultIndex;ls=lh&(HASH_SLOTS-1);while(g_legacyHash[ls])ls=(ls+1)&(HASH_SLOTS-1);g_legacyHash[ls]=resultIndex+1;}return true;
}
static void parseList(const char* data,unsigned long size,unsigned short source){unsigned long st=0;for(unsigned long i=0;i<=size;++i){if(i==size||data[i]=='\n'||data[i]==0){if(i>st)addResult(data+st,i-st,source);st=i+1;}}}
static bool getRoot(){char exe[MAX_PATH]={0};DWORD n=GetModuleFileNameA(0,exe,MAX_PATH);if(!n||n>=MAX_PATH)return false;char* cut=0;for(char* p=exe;*p;++p)if(*p=='\\'||*p=='/')cut=p;if(!cut)return false;*cut=0;cpy(g_root,sizeof(g_root),exe);return true;}
static bool archiveExists(const char* full){for(unsigned long i=0;i<g_archiveCount;++i)if(eqI(g_archives[i].full,full))return true;return false;}
static void addArchive(const char* full){if(g_archiveCount>=MAX_ARCHIVES||archiveExists(full))return;cpy(g_archives[g_archiveCount].full,MAX_PATH,full);cpy(g_archives[g_archiveCount].name,MAX_PATH,basename(full));g_archiveCount++;}
static bool readTextSidecar(const char* path,unsigned short source){HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return false;DWORD hi=0,sz=GetFileSize(h,&hi);if(hi||sz==0||sz==0xFFFFFFFFUL||sz>MAX_LISTFILE){CloseHandle(h);return false;}char* b=(char*)HeapAlloc(g_heap,0,sz);if(!b){CloseHandle(h);return false;}DWORD got=0;BOOL ok=ReadFile(h,b,sz,&got,0);CloseHandle(h);if(ok&&got)parseList(b,got,source);HeapFree(g_heap,0,b);return ok&&got;}
static void enumerate(const char* dir,int depth,bool archives,bool loose){if(depth<0)return;char pat[MAX_PATH]={0};cpy(pat,sizeof(pat),dir);cat(pat,sizeof(pat),"\\*");WIN32_FIND_DATAA fd={0};HANDLE f=FindFirstFileA(pat,&fd);if(f==INVALID_HANDLE_VALUE)return;do{if(eqI(fd.cFileName,".")||eqI(fd.cFileName,".."))continue;char full[MAX_PATH]={0};cpy(full,sizeof(full),dir);cat(full,sizeof(full),"\\");cat(full,sizeof(full),fd.cFileName);if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){enumerate(full,depth-1,archives,loose);continue;}if(archives&&endsI(fd.cFileName,".mpq"))addArchive(full);if(loose&&(endsI(fd.cFileName,".m2")||endsI(fd.cFileName,".mdx"))){const char* rel=full;unsigned long rootLen=slen(g_root);if(rootLen&&slen(full)>rootLen+1)rel=full+rootLen+1;addResult(rel,slen(rel),SRC_LOOSE);}if(loose&&eqI(fd.cFileName,"MoonMarkerM2List.txt"))readTextSidecar(full,SRC_LOOSE); }while(FindNextFileA(f,&fd));FindClose(f);}
static int cmpI(const char* a,const char* b){while(*a&&*b){char x=lower(*a),y=lower(*b);if(x<y)return -1;if(x>y)return 1;++a;++b;}return *a?1:(*b?-1:0);}
static void sortArchives(){for(unsigned long i=1;i<g_archiveCount;++i){Archive tmp=g_archives[i];unsigned long j=i;while(j>0&&cmpI(tmp.name,g_archives[j-1].name)<0){g_archives[j]=g_archives[j-1];--j;}g_archives[j]=tmp;}}
static const char* sourceName(unsigned short s){if(s==SRC_BUILTIN)return "BUILTIN";if(s==SRC_LOOSE)return "LOOSE";if(s<g_archiveCount)return g_archives[s].name;return "UNKNOWN";}

// Storm/MPQ classic v0 fallback ------------------------------------------------
static void initCrypt(){if(g_cryptReady)return;unsigned long seed=0x00100001UL;for(unsigned long i=0;i<0x100;++i){unsigned long j=i;for(unsigned long k=0;k<5;++k){seed=(seed*125UL+3UL)%0x2AAAABUL;unsigned long a=(seed&0xFFFFUL)<<16;seed=(seed*125UL+3UL)%0x2AAAABUL;unsigned long b=seed&0xFFFFUL;g_crypt[j]=a|b;j+=0x100;}}g_cryptReady=true;}
static unsigned long mpqHash(const char* s,unsigned long type){initCrypt();unsigned long s1=0x7FED7FEDUL,s2=0xEEEEEEEEUL;for(;*s;++s){unsigned char ch=(unsigned char)upper(*s);s1=g_crypt[(type<<8)+ch]^(s1+s2);s2=(unsigned long)ch+s1+s2+(s2<<5)+3UL;}return s1;}
static void decryptTable(unsigned long* data,unsigned long bytes,unsigned long key){initCrypt();unsigned long s1=key,s2=0xEEEEEEEEUL;for(unsigned long i=0;i<bytes/4;++i){s2+=g_crypt[0x400+(s1&0xFF)];unsigned long v=data[i]^(s1+s2);s1=((~s1<<21)+0x11111111UL)|(s1>>11);s2=v+s2+(s2<<5)+3UL;data[i]=v;}}
static bool readAt(HANDLE h,unsigned long off,void* buf,unsigned long bytes){DWORD hi=0;DWORD r=SetFilePointer(h,(LONG)off,(LONG*)&hi,0);if(r==0xFFFFFFFFUL&&GetLastError()!=0)return false;DWORD got=0;return ReadFile(h,buf,bytes,&got,0)&&got==bytes;}
static bool directMpqList(const char* path,unsigned short source){
    HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return false;MpqHeader hd={0};bool ok=readAt(h,0,&hd,sizeof(hd));if(!ok||hd.magic!=MPQ_MAGIC||hd.version!=0||!hd.hashCount||!hd.blockCount||hd.hashCount>0x200000||hd.blockCount>0x200000){CloseHandle(h);return false;}
    if(!ensureHeap()){CloseHandle(h);return false;}unsigned long hbytes=hd.hashCount*sizeof(MpqHash),bbytes=hd.blockCount*sizeof(MpqBlock);MpqHash* ht=(MpqHash*)HeapAlloc(g_heap,0,hbytes);MpqBlock* bt=(MpqBlock*)HeapAlloc(g_heap,0,bbytes);if(!ht||!bt){if(ht)HeapFree(g_heap,0,ht);if(bt)HeapFree(g_heap,0,bt);CloseHandle(h);return false;}
    ok=readAt(h,hd.hashOff,ht,hbytes)&&readAt(h,hd.blockOff,bt,bbytes);if(ok){decryptTable((unsigned long*)ht,hbytes,mpqHash("(hash table)",3));decryptTable((unsigned long*)bt,bbytes,mpqHash("(block table)",3));unsigned long start=mpqHash("(listfile)",0)%hd.hashCount,ha=mpqHash("(listfile)",1),hb=mpqHash("(listfile)",2),bi=0xFFFFFFFFUL;for(unsigned long i=0;i<hd.hashCount;++i){MpqHash& e=ht[(start+i)%hd.hashCount];if(e.blockIndex==0xFFFFFFFFUL)break;if(e.blockIndex<hd.blockCount&&e.a==ha&&e.b==hb){bi=e.blockIndex;break;}}if(bi!=0xFFFFFFFFUL){MpqBlock& b=bt[bi];if((b.flags&MPQ_FILE_EXISTS)&&(b.flags&MPQ_FILE_SINGLE_UNIT)&&!(b.flags&(MPQ_FILE_COMPRESS|MPQ_FILE_IMPLODE|MPQ_FILE_ENCRYPTED))&&b.fileSize>0&&b.fileSize<=MAX_LISTFILE&&b.compressedSize>=b.fileSize){char* data=(char*)HeapAlloc(g_heap,0,b.fileSize);if(data){ok=readAt(h,b.filePos,data,b.fileSize);if(ok)parseList(data,b.fileSize,source);HeapFree(g_heap,0,data);}else ok=false;}else ok=false;}else ok=false;}
    HeapFree(g_heap,0,ht);HeapFree(g_heap,0,bt);CloseHandle(h);return ok;
}
static bool execAt(unsigned long a){MEMORY_BASIC_INFORMATION m={0};if(VirtualQuery((void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT)return false;DWORD p=m.Protect&0xFF;return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool sfileAvailable(){return execAt(SFILE_OPEN_ARCHIVE)&&execAt(SFILE_OPEN_FILE_EX)&&execAt(SFILE_READ_FILE)&&execAt(SFILE_GET_FILE_SIZE)&&execAt(SFILE_CLOSE_FILE)&&execAt(SFILE_CLOSE_ARCHIVE);}
static bool sfileList(const char* path,unsigned short source){if(!sfileAvailable())return false;auto oa=(SFileOpenArchiveFn)SFILE_OPEN_ARCHIVE;auto of=(SFileOpenFileExFn)SFILE_OPEN_FILE_EX;auto rf=(SFileReadFileFn)SFILE_READ_FILE;auto gs=(SFileGetFileSizeFn)SFILE_GET_FILE_SIZE;auto cf=(SFileCloseFileFn)SFILE_CLOSE_FILE;auto ca=(SFileCloseArchiveFn)SFILE_CLOSE_ARCHIVE;void* ar=0;if(!oa(path,0,0,&ar)||!ar)return false;void* fi=0;if(!of(ar,"(listfile)",0,&fi)||!fi){ca(ar);return false;}DWORD hi=0,sz=gs(fi,&hi);if(hi||sz==0||sz==0xFFFFFFFFUL||sz>MAX_LISTFILE){cf(fi);ca(ar);return false;}char* b=(char*)HeapAlloc(g_heap,0,sz);if(!b){cf(fi);ca(ar);return false;}DWORD got=0;BOOL ok=rf(fi,b,sz,&got,0,0);cf(fi);ca(ar);if(ok&&got)parseList(b,got,source);HeapFree(g_heap,0,b);return ok&&got;}

static void legacyHeapDown(unsigned long root,unsigned long count){for(;;){unsigned long child=root*2+1;if(child>=count)return;if(child+1<count&&cmpI(g_results[g_legacyIndices[child]].path,g_results[g_legacyIndices[child+1]].path)<0)child++;if(cmpI(g_results[g_legacyIndices[root]].path,g_results[g_legacyIndices[child]].path)>=0)return;unsigned long t=g_legacyIndices[root];g_legacyIndices[root]=g_legacyIndices[child];g_legacyIndices[child]=t;root=child;}}
static void sortLegacy(){if(g_legacyCount<2)return;for(long i=(long)(g_legacyCount/2);i>0;--i)legacyHeapDown((unsigned long)(i-1),g_legacyCount);for(unsigned long n=g_legacyCount;n>1;--n){unsigned long t=g_legacyIndices[0];g_legacyIndices[0]=g_legacyIndices[n-1];g_legacyIndices[n-1]=t;legacyHeapDown(0,n-1);}}

static void scannerStart(){freeScan();g_archiveCount=g_archiveIndex=0;g_running=false;g_complete=false;g_current[0]=0;g_error[0]=0;if(!ensureResults()){setErr("MEMORY_ALLOC_FAILED");g_complete=true;return;}if(!getRoot()){setErr("GAME_PATH_UNAVAILABLE");g_complete=true;return;}addResult("Spells\\MoonBeam_Impact_Base.mdx",slen("Spells\\MoonBeam_Impact_Base.mdx"),SRC_BUILTIN);addResult("Spells\\TargetingCircle.mdx",slen("Spells\\TargetingCircle.mdx"),SRC_BUILTIN);char data[MAX_PATH]={0};cpy(data,sizeof(data),g_root);cat(data,sizeof(data),"\\Data");enumerate(data,2,true,true);char mm[MAX_PATH]={0};cpy(mm,sizeof(mm),g_root);cat(mm,sizeof(mm),"\\Interface\\AddOns\\MoonMarker");enumerate(mm,2,false,true);sortArchives();g_running=g_archiveCount>0;g_complete=!g_running;if(g_complete)sortLegacy();}
static void scannerStep(){if(!g_running)return;if(g_archiveIndex>=g_archiveCount){g_running=false;g_complete=true;g_current[0]=0;sortLegacy();return;}unsigned short src=(unsigned short)g_archiveIndex;Archive& a=g_archives[g_archiveIndex];cpy(g_current,sizeof(g_current),a.name);bool ok=sfileList(a.full,src);if(!ok)ok=directMpqList(a.full,src);char side[MAX_PATH]={0};cpy(side,sizeof(side),a.full);cat(side,sizeof(side),".listfile");bool sideOk=readTextSidecar(side,src);if(ok||sideOk)g_error[0]=0;else{cpy(g_error,sizeof(g_error),"NO_READABLE_LISTFILE: ");cat(g_error,sizeof(g_error),a.name);}g_archiveIndex++;if(g_archiveIndex>=g_archiveCount){g_running=false;g_complete=true;g_current[0]=0;sortLegacy();}}
static int pushScan(Lua50::State L){Lua50::PushBool(L,g_running);Lua50::PushBool(L,g_complete);Lua50::PushNumber(L,(double)g_archiveIndex);Lua50::PushNumber(L,(double)g_archiveCount);Lua50::PushNumber(L,(double)g_resultCount);if(g_current[0])Lua50::PushString(L,g_current);else Lua50::PushNil(L);if(g_error[0])Lua50::PushString(L,g_error);else Lua50::PushNil(L);return 7;}

static int pushLegacyScan(Lua50::State L){Lua50::PushBool(L,g_running);Lua50::PushBool(L,g_complete);Lua50::PushNumber(L,(double)g_archiveIndex);Lua50::PushNumber(L,(double)g_archiveCount);Lua50::PushNumber(L,(double)g_legacyCount);if(g_current[0])Lua50::PushString(L,g_current);else Lua50::PushNil(L);if(g_error[0])Lua50::PushString(L,g_error);else Lua50::PushNil(L);return 7;}

static void mediaScan(){g_mediaCount=0;if(!getRoot()){cpy(g_mediaStatus,sizeof(g_mediaStatus),"GAME_DIR_NOT_FOUND");return;}char dir[MAX_PATH]={0};cpy(dir,sizeof(dir),g_root);cat(dir,sizeof(dir),"\\Interface\\AddOns\\AutoRange\\Media");char pat[MAX_PATH]={0};cpy(pat,sizeof(pat),dir);cat(pat,sizeof(pat),"\\*");WIN32_FIND_DATAA fd={0};HANDLE f=FindFirstFileA(pat,&fd);if(f==INVALID_HANDLE_VALUE){cpy(g_mediaStatus,sizeof(g_mediaStatus),"MEDIA_DIR_NOT_FOUND");return;}do{if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&endsI(fd.cFileName,".tga")&&g_mediaCount<MEDIA_MAX)cpy(g_media[g_mediaCount++],MAX_PATH,fd.cFileName);}while(FindNextFileA(f,&fd));FindClose(f);for(unsigned long i=1;i<g_mediaCount;++i){char tmp[MAX_PATH];cpy(tmp,sizeof(tmp),g_media[i]);unsigned long j=i;while(j>0&&cmpI(tmp,g_media[j-1])<0){cpy(g_media[j],MAX_PATH,g_media[j-1]);--j;}cpy(g_media[j],MAX_PATH,tmp);}cpy(g_mediaStatus,sizeof(g_mediaStatus),g_mediaCount?"OK":"EMPTY");}
static bool cmdEq(const char* a,const char* b){return eqI(a,b);}

} // namespace

int dispatchAutoRange(Lua50::State L,const char* cmd){
    if(cmdEq(cmd,"AutoRange.M2Scan.Start")){scannerStart();return pushScan(L);}if(cmdEq(cmd,"AutoRange.M2Scan.Step")){scannerStep();return pushScan(L);}if(cmdEq(cmd,"AutoRange.M2Scan.Status"))return pushScan(L);
    if(cmdEq(cmd,"AutoRange.M2Scan.Get")){if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}long i=(long)Lua50::ToNumber(L,2);if(i<1||(unsigned long)i>g_resultCount){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}Result& r=g_results[i-1];Lua50::PushBool(L,true);Lua50::PushString(L,r.path);Lua50::PushString(L,sourceName(r.source));return 3;}
    if(cmdEq(cmd,"AutoRange.M2Scan.ArchiveGet")){if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}long i=(long)Lua50::ToNumber(L,2);if(i<1||(unsigned long)i>g_archiveCount){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}Lua50::PushBool(L,true);Lua50::PushString(L,g_archives[i-1].name);return 2;}
    if(cmdEq(cmd,"AutoRange.Media.Scan")){mediaScan();bool ok=eqI(g_mediaStatus,"OK")||eqI(g_mediaStatus,"EMPTY");Lua50::PushBool(L,ok);Lua50::PushNumber(L,(double)g_mediaCount);Lua50::PushString(L,g_mediaStatus);return 3;}
    if(cmdEq(cmd,"AutoRange.Media.Status")){Lua50::PushNumber(L,(double)g_mediaCount);Lua50::PushString(L,g_mediaStatus);return 2;}
    if(cmdEq(cmd,"AutoRange.Media.Get")){if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}long i=(long)Lua50::ToNumber(L,2);if(i<1||(unsigned long)i>g_mediaCount){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}Lua50::PushBool(L,true);Lua50::PushString(L,g_media[i-1]);return 2;}
    return -1;
}

int dispatchMoonAdvanced(Lua50::State L,const char* cmd){
    if(cmdEq(cmd,"MoonMarker.Advanced.ScanM2.Start")){scannerStart();return pushLegacyScan(L);}if(cmdEq(cmd,"MoonMarker.Advanced.ScanM2.Step")){scannerStep();return pushLegacyScan(L);}if(cmdEq(cmd,"MoonMarker.Advanced.ScanM2.Status"))return pushLegacyScan(L);
    if(cmdEq(cmd,"MoonMarker.Advanced.ScanM2.Get")){if(Lua50::GetTop(L)<2||!Lua50::IsNumber(L,2)){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_REQUIRED");return 2;}long i=(long)Lua50::ToNumber(L,2);if(i<1||(unsigned long)i>g_legacyCount){Lua50::PushBool(L,false);Lua50::PushString(L,"INDEX_OUT_OF_RANGE");return 2;}Lua50::PushBool(L,true);Lua50::PushString(L,g_results[g_legacyIndices[i-1]].path);return 2;}
    return -1;
}

void shutdown(){freeScan();if(g_heap){HeapDestroy(g_heap);g_heap=0;}g_archiveCount=g_archiveIndex=0;g_running=false;g_complete=false;g_mediaCount=0;cpy(g_mediaStatus,sizeof(g_mediaStatus),"NOT_SCANNED");}

} // namespace TysScanMedia
