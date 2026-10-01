#include <windows.h>
#include "dbc_internal.h"

extern "C" void* __cdecl memcpy(void*,const void*,unsigned int);
extern "C" void* __cdecl memset(void*,int,unsigned int);

namespace TysDbc {
namespace {

constexpr unsigned long SFILE_OPEN_ARCHIVE = 0x00648DD0UL;
constexpr unsigned long SFILE_OPEN_FILE_EX = 0x006477C0UL;
constexpr unsigned long SFILE_READ_FILE = 0x00648460UL;
constexpr unsigned long SFILE_GET_FILE_SIZE = 0x006487F0UL;
constexpr unsigned long SFILE_CLOSE_FILE = 0x00648730UL;
constexpr unsigned long SFILE_CLOSE_ARCHIVE = 0x00648EF0UL;
constexpr DWORD INVALID_FILE_SIZE_ = 0xFFFFFFFFUL;
constexpr DWORD MAX_DBC_BYTES = 96UL*1024UL*1024UL;
constexpr unsigned MAX_ARCHIVES = 256;

using SFileOpenArchiveProc = BOOL (__stdcall*)(const char*,DWORD,DWORD,void**);
using SFileOpenFileExProc = BOOL (__stdcall*)(void*,const char*,DWORD,void**);
using SFileReadFileProc = BOOL (__stdcall*)(void*,void*,DWORD,DWORD*,void*,DWORD);
using SFileGetFileSizeProc = DWORD (__stdcall*)(void*,DWORD*);
using SFileCloseFileProc = BOOL (__stdcall*)(void*);
using SFileCloseArchiveProc = BOOL (__stdcall*)(void*);

static HANDLE g_heap=0;
static bool g_archivesAttempted=false;
static unsigned g_archiveCount=0;
static char g_archives[MAX_ARCHIVES][MAX_PATH];
static char g_lastError[96]="NOT_INITIALIZED";

static unsigned slen(const char* s){unsigned n=0;if(s)while(s[n])++n;return n;}
static void cpy(char* d,unsigned cap,const char* s){if(!cap)return;unsigned i=0;if(s)for(;s[i]&&i+1<cap;++i)d[i]=s[i];d[i]=0;}
static void cat(char* d,unsigned cap,const char* s){unsigned n=slen(d);if(n<cap)cpy(d+n,cap-n,s);}
static char lower(char c){return (c>='A'&&c<='Z')?(char)(c+32):c;}
static int cmpI(const char* a,const char* b){while(*a&&*b){char x=lower(*a),y=lower(*b);if(x<y)return -1;if(x>y)return 1;++a;++b;}return *a?1:(*b?-1:0);}
static bool eqI(const char* a,const char* b){return cmpI(a,b)==0;}
static bool startsI(const char* s,const char* p){while(*p){if(!*s||lower(*s)!=lower(*p))return false;++s;++p;}return true;}
static const char* baseName(const char* p){const char* r=p;if(!p)return "";while(*p){if(*p=='\\'||*p=='/')r=p+1;++p;}return r;}
static bool endsMpq(const char* n){unsigned l=slen(n);return l>=4&&lower(n[l-4])=='.'&&lower(n[l-3])=='m'&&lower(n[l-2])=='p'&&lower(n[l-1])=='q';}
static void parentDir(char* p){unsigned l=slen(p);while(l>0){char c=p[l-1];if(c=='\\'||c=='/'){p[l-1]=0;return;}--l;}cpy(p,MAX_PATH,".");}
static void pathJoin(char* out,unsigned cap,const char* a,const char* b){cpy(out,cap,a);unsigned n=slen(out);if(n&&out[n-1]!='\\'&&out[n-1]!='/')cat(out,cap,"\\");cat(out,cap,b);}
static bool heapReady(){if(g_heap)return true;g_heap=HeapCreate(0,0,0);if(!g_heap){cpy(g_lastError,sizeof(g_lastError),"HEAP_CREATE_FAILED");return false;}return true;}
static void* alloc(unsigned long bytes){if(!heapReady())return 0;return HeapAlloc(g_heap,0,bytes);}
static void freeMem(void* p){if(p&&g_heap)HeapFree(g_heap,0,p);}
static char* dupText(const char* s){unsigned n=slen(s);char* p=(char*)alloc(n+1);if(!p)return 0;for(unsigned i=0;i<n;++i)p[i]=s[i];p[n]=0;return p;}

static int archivePriority(const char* path){
    const char* n=baseName(path);unsigned l=slen(n);
    if(eqI(n,"patch.mpq"))return 1000;
    if(l==11&&startsI(n,"patch-")&&lower(n[7])=='.'&&lower(n[8])=='m'&&lower(n[9])=='p'&&lower(n[10])=='q'){
        char ch=lower(n[6]);if(ch>='0'&&ch<='9')return 1001+(ch-'0');if(ch>='a'&&ch<='z')return 1011+(ch-'a');
    }
    if(startsI(n,"patch-"))return 900;
    if(eqI(n,"dbc.mpq"))return 100;
    return 0;
}
static bool archiveBetter(const char* a,const char* b){int ap=archivePriority(a),bp=archivePriority(b);if(ap!=bp)return ap>bp;return cmpI(a,b)>0;}

static void collectArchives(const char* directory,int depth){
    if(depth<0||g_archiveCount>=MAX_ARCHIVES)return;
    char pat[MAX_PATH]={0};pathJoin(pat,sizeof(pat),directory,"*");
    WIN32_FIND_DATAA fd={};HANDLE h=FindFirstFileA(pat,&fd);if(h==INVALID_HANDLE_VALUE)return;
    do{
        const char* n=fd.cFileName;if((n[0]=='.'&&n[1]==0)||(n[0]=='.'&&n[1]=='.'&&n[2]==0))continue;
        char full[MAX_PATH]={0};pathJoin(full,sizeof(full),directory,n);
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){collectArchives(full,depth-1);}
        else if(endsMpq(n)&&g_archiveCount<MAX_ARCHIVES){cpy(g_archives[g_archiveCount],MAX_PATH,full);++g_archiveCount;}
    }while(FindNextFileA(h,&fd));
    FindClose(h);
}
static void sortDedupeArchives(){
    for(unsigned i=1;i<g_archiveCount;++i){char tmp[MAX_PATH]={0};cpy(tmp,MAX_PATH,g_archives[i]);unsigned j=i;while(j>0&&archiveBetter(tmp,g_archives[j-1])){cpy(g_archives[j],MAX_PATH,g_archives[j-1]);--j;}cpy(g_archives[j],MAX_PATH,tmp);}
    unsigned w=0;for(unsigned i=0;i<g_archiveCount;++i){if(w==0||!eqI(g_archives[i],g_archives[w-1])){if(w!=i)cpy(g_archives[w],MAX_PATH,g_archives[i]);++w;}}g_archiveCount=w;
}
static void ensureArchives(){
    if(g_archivesAttempted)return;g_archivesAttempted=true;g_archiveCount=0;
    char exe[MAX_PATH]={0};DWORD n=GetModuleFileNameA(0,exe,MAX_PATH);if(!n||n>=MAX_PATH){cpy(g_lastError,sizeof(g_lastError),"EXE_PATH_FAILED");return;}
    parentDir(exe);char data[MAX_PATH]={0};pathJoin(data,sizeof(data),exe,"Data");collectArchives(data,2);sortDedupeArchives();
    if(!g_archiveCount)cpy(g_lastError,sizeof(g_lastError),"NO_MPQ");else cpy(g_lastError,sizeof(g_lastError),"OK");
}

static bool readArchiveFile(const char* archivePath,const char* internalPath,unsigned char** outBytes,DWORD* outSize){
    auto openArchive=(SFileOpenArchiveProc)SFILE_OPEN_ARCHIVE;auto openFile=(SFileOpenFileExProc)SFILE_OPEN_FILE_EX;auto readFile=(SFileReadFileProc)SFILE_READ_FILE;auto getSize=(SFileGetFileSizeProc)SFILE_GET_FILE_SIZE;auto closeFile=(SFileCloseFileProc)SFILE_CLOSE_FILE;auto closeArchive=(SFileCloseArchiveProc)SFILE_CLOSE_ARCHIVE;
    void* ar=0;if(!openArchive(archivePath,0,0,&ar)||!ar)return false;void* f=0;if(!openFile(ar,internalPath,0,&f)||!f){closeArchive(ar);return false;}DWORD hi=0,sz=getSize(f,&hi);if(sz==INVALID_FILE_SIZE_||hi||sz<20||sz>MAX_DBC_BYTES){closeFile(f);closeArchive(ar);return false;}unsigned char* b=(unsigned char*)alloc(sz);if(!b){closeFile(f);closeArchive(ar);cpy(g_lastError,sizeof(g_lastError),"HEAP_ALLOC_FAILED");return false;}DWORD rd=0;BOOL ok=readFile(f,b,sz,&rd,0,0);closeFile(f);closeArchive(ar);if(!ok||rd!=sz){HeapFree(g_heap,0,b);return false;}*outBytes=b;*outSize=sz;return true;
}
static unsigned long le32(const unsigned char* p){return (unsigned long)p[0]|((unsigned long)p[1]<<8)|((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24);}

struct DbcTable{
    bool attempted;bool ready;unsigned char* bytes;DWORD bytesSize;DWORD records;DWORD fields;DWORD recordSize;DWORD stringSize;DWORD stringStart;DWORD hashCap;DWORD* keys;DWORD* vals;char source[64];char error[64];
};
static DbcTable g_spell={};static DbcTable g_radius={};static DbcTable g_range={};static DbcTable g_duration={};static DbcTable g_creatureSpellData={};static DbcTable g_itemDisplay={};static DbcTable g_spellVisual={};static DbcTable g_spellVisualKit={};static DbcTable g_spellVisualEffect={};

static unsigned nextPow2(unsigned v){unsigned x=1;while(x<v&&x<0x40000000U)x<<=1;return x;}
static bool buildIndex(DbcTable& t){unsigned cap=nextPow2(t.records*2+1);t.keys=(DWORD*)alloc(cap*sizeof(DWORD));t.vals=(DWORD*)alloc(cap*sizeof(DWORD));if(!t.keys||!t.vals){cpy(t.error,sizeof(t.error),"HEAP_ALLOC_FAILED");cpy(g_lastError,sizeof(g_lastError),t.error);return false;}memset(t.vals,0,cap*sizeof(DWORD));t.hashCap=cap;for(DWORD i=0;i<t.records;++i){const unsigned char* row=t.bytes+20+i*t.recordSize;DWORD id=le32(row);DWORD pos=(id*2654435761UL)&(cap-1);while(t.vals[pos])pos=(pos+1)&(cap-1);t.keys[pos]=id;t.vals[pos]=i+1;}return true;}
static bool ensureTable(DbcTable& t,const char* path,DWORD minFields){
    if(t.attempted)return t.ready;t.attempted=true;ensureArchives();if(!g_archiveCount){cpy(t.error,sizeof(t.error),"NO_MPQ");cpy(g_lastError,sizeof(g_lastError),t.error);return false;}
    for(unsigned i=0;i<g_archiveCount;++i){if(readArchiveFile(g_archives[i],path,&t.bytes,&t.bytesSize)){cpy(t.source,sizeof(t.source),baseName(g_archives[i]));break;}}
    if(!t.bytes||t.bytesSize<20||t.bytes[0]!='W'||t.bytes[1]!='D'||t.bytes[2]!='B'||t.bytes[3]!='C'){cpy(t.error,sizeof(t.error),"NOT_FOUND_OR_BAD_WDBC");cpy(g_lastError,sizeof(g_lastError),t.error);return false;}
    t.records=le32(t.bytes+4);t.fields=le32(t.bytes+8);t.recordSize=le32(t.bytes+12);t.stringSize=le32(t.bytes+16);
    if(!t.records||t.fields<minFields||t.recordSize<t.fields*4){cpy(t.error,sizeof(t.error),"DBC_SCHEMA");cpy(g_lastError,sizeof(g_lastError),t.error);return false;}
    unsigned __int64 recBytes=(unsigned __int64)t.records*t.recordSize;unsigned __int64 total=20ULL+recBytes+t.stringSize;if(total>t.bytesSize){cpy(t.error,sizeof(t.error),"DBC_TRUNCATED");cpy(g_lastError,sizeof(g_lastError),t.error);return false;}t.stringStart=20+(DWORD)recBytes;if(!buildIndex(t))return false;t.ready=true;cpy(t.error,sizeof(t.error),"OK");cpy(g_lastError,sizeof(g_lastError),"OK");return true;
}
static const unsigned char* rowById(DbcTable& t,DWORD id){if(!t.ready||!t.hashCap)return 0;DWORD pos=(id*2654435761UL)&(t.hashCap-1),start=pos;do{DWORD v=t.vals[pos];if(!v)return 0;if(t.keys[pos]==id)return t.bytes+20+(v-1)*t.recordSize;pos=(pos+1)&(t.hashCap-1);}while(pos!=start);return 0;}
static const unsigned char* rowAt(DbcTable& t,DWORD index){if(!t.ready||index>=t.records)return 0;return t.bytes+20+index*t.recordSize;}
static DWORD u32(DbcTable& t,const unsigned char* r,DWORD f){if(!r||f>=t.fields||(f+1)*4>t.recordSize)return 0;return le32(r+f*4);}
static long i32(DbcTable& t,const unsigned char* r,DWORD f){return (long)u32(t,r,f);}
static float f32(DbcTable& t,const unsigned char* r,DWORD f){DWORD b=u32(t,r,f);float x=0;memcpy(&x,&b,4);return x;}
static const char* str(DbcTable& t,const unsigned char* r,DWORD f){static const char empty[]="";DWORD off=u32(t,r,f);if(off>=t.stringSize||t.stringStart+off>=t.bytesSize)return empty;const char* s=(const char*)(t.bytes+t.stringStart+off);DWORD max=t.stringSize-off;for(DWORD i=0;i<max;++i)if(s[i]==0)return s;return empty;}

static void setNumber(Lua50::State L,const char* k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setString(Lua50::State L,const char* k,const char* v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void addMeta(Lua50::State L,const char* name,DbcTable& t,DWORD id){setNumber(L,"id",id);setString(L,"dbc",name);setString(L,"source",t.source);setNumber(L,"fieldCount",t.fields);setNumber(L,"recordSize",t.recordSize);}
static int fail(Lua50::State L,const char* dbc,const char* err){char m[160]={0};cpy(m,sizeof(m),"UnitXP DBC ");cat(m,sizeof(m),dbc);cat(m,sizeof(m),": ");cat(m,sizeof(m),err);cpy(g_lastError,sizeof(g_lastError),err);Lua50::PushNil(L);Lua50::PushString(L,m);return 2;}
static const unsigned char* requireRow(Lua50::State L,DbcTable& t,const char* dbc,const char* path,DWORD minFields,DWORD id,int* rc){if(!ensureTable(t,path,minFields)){*rc=fail(L,dbc,t.error);return 0;}const unsigned char* r=rowById(t,id);if(!r){*rc=fail(L,dbc,"ID_NOT_FOUND");return 0;}*rc=0;return r;}

static void setIndexedNumber(Lua50::State L,const char* prefix,unsigned idx,double v){char k[64]={0};cpy(k,sizeof(k),prefix);unsigned n=slen(k);k[n++]=(char)('1'+idx);k[n]=0;setNumber(L,k,v);}
static int pushSpell(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_spell,"Spell","DBFilesClient\\Spell.dbc",120,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"Spell",g_spell,id);setNumber(L,"school",u32(g_spell,r,1));setNumber(L,"category",u32(g_spell,r,2));setNumber(L,"dispelType",u32(g_spell,r,4));setNumber(L,"mechanic",u32(g_spell,r,5));setNumber(L,"attributes",u32(g_spell,r,6));setNumber(L,"attributesEx",u32(g_spell,r,7));setNumber(L,"attributesEx2",u32(g_spell,r,8));setNumber(L,"attributesEx3",u32(g_spell,r,9));setNumber(L,"attributesEx4",u32(g_spell,r,10));setNumber(L,"castingTimeIndex",u32(g_spell,r,18));setNumber(L,"recoveryTime",u32(g_spell,r,19));setNumber(L,"categoryRecoveryTime",u32(g_spell,r,20));setNumber(L,"durationIndex",u32(g_spell,r,30));setNumber(L,"powerType",u32(g_spell,r,31));setNumber(L,"manaCost",u32(g_spell,r,32));setNumber(L,"rangeIndex",u32(g_spell,r,36));setNumber(L,"speed",f32(g_spell,r,37));setNumber(L,"stackAmount",u32(g_spell,r,39));setNumber(L,"procFlags",u32(g_spell,r,24));setNumber(L,"procChance",u32(g_spell,r,25));setNumber(L,"procCharges",u32(g_spell,r,26));setNumber(L,"maximumLevel",u32(g_spell,r,27));setNumber(L,"baseLevel",u32(g_spell,r,28));setNumber(L,"spellLevel",u32(g_spell,r,29));setNumber(L,"manaCostPerLevel",u32(g_spell,r,33));setNumber(L,"manaPerSecond",u32(g_spell,r,34));setNumber(L,"manaPerSecondPerLevel",u32(g_spell,r,35));
    for(unsigned i=0;i<3;++i){setIndexedNumber(L,"effect",i,u32(g_spell,r,61+i));setIndexedNumber(L,"effectDieSides",i,i32(g_spell,r,64+i));setIndexedNumber(L,"effectBaseDice",i,u32(g_spell,r,67+i));setIndexedNumber(L,"effectDicePerLevel",i,f32(g_spell,r,70+i));setIndexedNumber(L,"effectRealPointsPerLevel",i,f32(g_spell,r,73+i));setIndexedNumber(L,"effectBasePoints",i,i32(g_spell,r,76+i));setIndexedNumber(L,"effectMechanic",i,u32(g_spell,r,79+i));setIndexedNumber(L,"targetA",i,u32(g_spell,r,82+i));setIndexedNumber(L,"targetB",i,u32(g_spell,r,85+i));setIndexedNumber(L,"radiusIndex",i,u32(g_spell,r,88+i));setIndexedNumber(L,"applyAura",i,u32(g_spell,r,91+i));setIndexedNumber(L,"effectAmplitude",i,u32(g_spell,r,94+i));setIndexedNumber(L,"effectMultipleValue",i,f32(g_spell,r,97+i));setIndexedNumber(L,"effectChainTarget",i,u32(g_spell,r,100+i));setIndexedNumber(L,"effectItemType",i,u32(g_spell,r,103+i));setIndexedNumber(L,"miscValue",i,u32(g_spell,r,106+i));setIndexedNumber(L,"triggerSpell",i,u32(g_spell,r,109+i));setIndexedNumber(L,"effectPointsPerComboPoint",i,f32(g_spell,r,112+i));setIndexedNumber(L,"effectSimpleValue",i,(double)i32(g_spell,r,76+i)+(double)u32(g_spell,r,67+i));}
    setNumber(L,"spellVisual1",u32(g_spell,r,115));setNumber(L,"spellVisual2",u32(g_spell,r,116));setNumber(L,"spellIconID",u32(g_spell,r,117));setNumber(L,"activeIconID",u32(g_spell,r,118));setNumber(L,"spellPriority",u32(g_spell,r,119));if(g_spell.fields>120)setString(L,"name",str(g_spell,r,120));if(g_spell.fields>124)setString(L,"nameZhCN",str(g_spell,r,124));if(g_spell.fields>138)setString(L,"descriptionEnUS",str(g_spell,r,138));if(g_spell.fields>142)setString(L,"descriptionZhCN",str(g_spell,r,142));if(g_spell.fields>147)setString(L,"auraDescriptionEnUS",str(g_spell,r,147));if(g_spell.fields>151)setString(L,"auraDescriptionZhCN",str(g_spell,r,151));if(g_spell.fields>156)setNumber(L,"manaCostPercentage",u32(g_spell,r,156));if(g_spell.fields>157)setNumber(L,"startRecoveryCategory",u32(g_spell,r,157));if(g_spell.fields>158)setNumber(L,"startRecoveryTime",u32(g_spell,r,158));if(g_spell.fields>159)setNumber(L,"maximumTargetLevel",u32(g_spell,r,159));if(g_spell.fields>160)setNumber(L,"spellFamilyName",u32(g_spell,r,160));if(g_spell.fields>161)setNumber(L,"spellFamilyFlags1",u32(g_spell,r,161));if(g_spell.fields>162)setNumber(L,"spellFamilyFlags2",u32(g_spell,r,162));if(g_spell.fields>163)setNumber(L,"maximumAffectedTargets",u32(g_spell,r,163));if(g_spell.fields>164)setNumber(L,"damageClass",u32(g_spell,r,164));if(g_spell.fields>165)setNumber(L,"preventionType",u32(g_spell,r,165));if(g_spell.fields>166)setNumber(L,"stanceBarOrder",u32(g_spell,r,166));if(g_spell.fields>169){for(unsigned i=0;i<3;++i)setIndexedNumber(L,"effectDamageMultiplier",i,f32(g_spell,r,167+i));}return 1;}
static int pushRadius(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_radius,"SpellRadius","DBFilesClient\\SpellRadius.dbc",4,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellRadius",g_radius,id);setNumber(L,"radius",f32(g_radius,r,1));setNumber(L,"radiusPerLevel",i32(g_radius,r,2));setNumber(L,"radiusMax",i32(g_radius,r,3));return 1;}
static int pushRange(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_range,"SpellRange","DBFilesClient\\SpellRange.dbc",4,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellRange",g_range,id);setNumber(L,"rangeMin",f32(g_range,r,1));setNumber(L,"rangeMax",f32(g_range,r,2));setNumber(L,"flags",u32(g_range,r,3));if(g_range.fields>4)setString(L,"name",str(g_range,r,4));if(g_range.fields>13)setString(L,"shortName",str(g_range,r,13));return 1;}
static int pushDuration(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_duration,"SpellDuration","DBFilesClient\\SpellDuration.dbc",4,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellDuration",g_duration,id);setNumber(L,"duration",i32(g_duration,r,1));setNumber(L,"durationPerLevel",i32(g_duration,r,2));setNumber(L,"maxDuration",i32(g_duration,r,3));return 1;}
static int pushItemDisplay(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_itemDisplay,"ItemDisplayInfo","DBFilesClient\\ItemDisplayInfo.dbc",6,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"ItemDisplayInfo",g_itemDisplay,id);setString(L,"modelName1",str(g_itemDisplay,r,1));setString(L,"modelName2",str(g_itemDisplay,r,2));setString(L,"modelTexture1",str(g_itemDisplay,r,3));setString(L,"modelTexture2",str(g_itemDisplay,r,4));setString(L,"inventoryIcon",str(g_itemDisplay,r,5));if(g_itemDisplay.fields>9)setNumber(L,"flags",u32(g_itemDisplay,r,9));if(g_itemDisplay.fields>10)setNumber(L,"spellVisualId",u32(g_itemDisplay,r,10));if(g_itemDisplay.fields>11)setNumber(L,"groupSoundIndex",u32(g_itemDisplay,r,11));if(g_itemDisplay.fields>12)setNumber(L,"helmetGeosetVisId1",u32(g_itemDisplay,r,12));if(g_itemDisplay.fields>13)setNumber(L,"helmetGeosetVisId2",u32(g_itemDisplay,r,13));return 1;}
static int pushVisual(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_spellVisual,"SpellVisual","DBFilesClient\\SpellVisual.dbc",16,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellVisual",g_spellVisual,id);const char* k[15]={"precastKit","castKit","impactKit","stateKit","channelKit","hasMissile","missileModel","missilePathType","missileDestinationAttachment","missileSound","hasArea","areaModel","areaKit","animEventSoundID","flags"};for(unsigned i=0;i<15;++i)setNumber(L,k[i],u32(g_spellVisual,r,i+1));return 1;}
static int pushVisualKit(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_spellVisualKit,"SpellVisualKit","DBFilesClient\\SpellVisualKit.dbc",35,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellVisualKit",g_spellVisualKit,id);const char* k[34]={"kitType","animID","headEffect","chestEffect","baseEffect","leftHandEffect","rightHandEffect","breathEffect","specialEffect1","specialEffect2","specialEffect3","worldEffect","soundID","shakeID","charProc1","charProc2","charProc3","charProc4","charParamZero1","charParamZero2","charParamZero3","charParamZero4","charParamOne1","charParamOne2","charParamOne3","charParamOne4","charParamTwo1","charParamTwo2","charParamTwo3","charParamTwo4","charParamThree1","charParamThree2","charParamThree3","charParamThree4"};for(unsigned i=0;i<34;++i)setNumber(L,k[i],u32(g_spellVisualKit,r,i+1));return 1;}
static int pushVisualEffect(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_spellVisualEffect,"SpellVisualEffectName","DBFilesClient\\SpellVisualEffectName.dbc",5,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"SpellVisualEffectName",g_spellVisualEffect,id);setString(L,"name",str(g_spellVisualEffect,r,1));setString(L,"fileName",str(g_spellVisualEffect,r,2));setNumber(L,"specialAttachPoint",u32(g_spellVisualEffect,r,3));setNumber(L,"scale",f32(g_spellVisualEffect,r,4));return 1;}


static unsigned char foldByte(unsigned char c){return (c>='A'&&c<='Z')?(unsigned char)(c+32):c;}
static bool textEqualFold(const char* a,const char* b){if(!a||!b)return false;while(*a&&*b){if(foldByte((unsigned char)*a)!=foldByte((unsigned char)*b))return false;++a;++b;}return *a==0&&*b==0;}
static bool textPrefixFold(const char* value,const char* q){if(!value||!q)return false;while(*q){if(!*value||foldByte((unsigned char)*value)!=foldByte((unsigned char)*q))return false;++value;++q;}return true;}
static bool textContainsFold(const char* value,const char* q){if(!value||!q||!*q)return false;for(const char* p=value;*p;++p)if(textPrefixFold(p,q))return true;return false;}
static char* trimQuery(const char* raw){if(!raw)return 0;const char* b=raw;while(*b&&(*b==' '||*b=='\t'||*b=='\r'||*b=='\n'))++b;const char* e=b+slen(b);while(e>b&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n'))--e;unsigned n=(unsigned)(e-b);char* q=(char*)alloc(n+1);if(!q)return 0;for(unsigned i=0;i<n;++i)q[i]=b[i];q[n]=0;return q;}
static int textMatchRank(const char* value,const char* q,int exactRank,int prefixRank,int containsRank,unsigned char* type){if(!value||!*value)return 999;if(textEqualFold(value,q)){*type=0;return exactRank;}if(textPrefixFold(value,q)){*type=1;return prefixRank;}if(textContainsFold(value,q)){*type=2;return containsRank;}return 999;}
static const char* matchTypeName(unsigned char t){return t==0?"exact":(t==1?"prefix":"contains");}

struct SpellSearchHit{DWORD id;const char* name;const char* nameZhCN;unsigned char rank;unsigned char field;unsigned char type;};
static bool hitBefore(const SpellSearchHit& a,const SpellSearchHit& b){return a.rank<b.rank||(a.rank==b.rank&&a.id<b.id);}
static int pushSpellSearch(Lua50::State L,const char* rawQuery,DWORD limit){
    char* q=trimQuery(rawQuery);if(!q||!*q){freeMem(q);return fail(L,"SpellSearch","QUERY_REQUIRED");}
    if(!ensureTable(g_spell,"DBFilesClient\\Spell.dbc",120)){freeMem(q);return fail(L,"SpellSearch",g_spell.error);}
    if(limit<1)limit=100;if(limit>500)limit=500;
    SpellSearchHit* hits=(SpellSearchHit*)alloc(sizeof(SpellSearchHit)*limit);if(!hits){freeMem(q);return fail(L,"SpellSearch","HEAP_ALLOC_FAILED");}
    DWORD kept=0,total=0;
    for(DWORD i=0;i<g_spell.records;++i){const unsigned char* r=rowAt(g_spell,i);if(!r)continue;const char* en=g_spell.fields>120?str(g_spell,r,120):"";const char* zh=g_spell.fields>124?str(g_spell,r,124):"";SpellSearchHit h={};h.id=u32(g_spell,r,0);h.name=en;h.nameZhCN=zh;unsigned char mt=0;int rank=textMatchRank(zh,q,0,1,2,&mt);if(rank!=999){h.rank=(unsigned char)rank;h.field=1;h.type=mt;}else{rank=textMatchRank(en,q,3,4,5,&mt);if(rank==999)continue;h.rank=(unsigned char)rank;h.field=0;h.type=mt;}++total;DWORD pos=0;while(pos<kept&&!hitBefore(h,hits[pos]))++pos;if(kept<limit){for(DWORD j=kept;j>pos;--j)hits[j]=hits[j-1];hits[pos]=h;++kept;}else if(pos<kept){for(DWORD j=kept-1;j>pos;--j)hits[j]=hits[j-1];hits[pos]=h;}}
    Lua50::NewTable(L);for(DWORD i=0;i<kept;++i){Lua50::PushNumber(L,(double)(i+1));Lua50::NewTable(L);setNumber(L,"id",hits[i].id);setString(L,"name",hits[i].name);setString(L,"nameZhCN",hits[i].nameZhCN);setString(L,"matchField",hits[i].field?"nameZhCN":"name");setString(L,"matchType",matchTypeName(hits[i].type));Lua50::SetTable(L,-3);}Lua50::PushNumber(L,(double)total);Lua50::PushBool(L,total>kept);freeMem(hits);freeMem(q);return 3;
}

static int pushCreatureSpellData(Lua50::State L,DWORD id){int rc=0;const unsigned char* r=requireRow(L,g_creatureSpellData,"CreatureSpellData","DBFilesClient\\CreatureSpellData.dbc",5,id,&rc);if(!r)return rc;Lua50::NewTable(L);addMeta(L,"CreatureSpellData",g_creatureSpellData,id);for(unsigned i=0;i<4;++i){char k[8]="spell1";k[5]=(char)('1'+i);k[6]=0;setNumber(L,k,u32(g_creatureSpellData,r,1+i));}return 1;}
static int pushCreatureSpellSearch(Lua50::State L,DWORD spellId){if(!spellId)return fail(L,"CreatureSpellSearch","SPELL_REQUIRED");if(!ensureTable(g_creatureSpellData,"DBFilesClient\\CreatureSpellData.dbc",5))return fail(L,"CreatureSpellSearch",g_creatureSpellData.error);Lua50::NewTable(L);DWORD emitted=0,total=0;for(DWORD i=0;i<g_creatureSpellData.records;++i){const unsigned char* r=rowAt(g_creatureSpellData,i);if(!r)continue;DWORD slot=0;for(DWORD j=0;j<4;++j)if(u32(g_creatureSpellData,r,1+j)==spellId){slot=j+1;break;}if(!slot)continue;++total;if(emitted<500){++emitted;Lua50::PushNumber(L,(double)emitted);Lua50::NewTable(L);setNumber(L,"dataId",u32(g_creatureSpellData,r,0));setNumber(L,"slot",slot);setNumber(L,"spellId",spellId);setString(L,"dbcSource",g_creatureSpellData.source);Lua50::SetTable(L,-3);}}Lua50::PushNumber(L,(double)total);Lua50::PushBool(L,total>emitted);return 3;}

constexpr DWORD MAX_WDB_BYTES=64UL*1024UL*1024UL;
constexpr unsigned MAX_CACHE_FILES=16;
static bool readWholeDiskFile(const char* path,unsigned char** out,DWORD* outSize){*out=0;*outSize=0;HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return false;DWORD hi=0,sz=GetFileSize(h,&hi);if(sz==INVALID_FILE_SIZE||hi||sz<20||sz>MAX_WDB_BYTES){CloseHandle(h);return false;}unsigned char* b=(unsigned char*)alloc(sz);if(!b){CloseHandle(h);return false;}DWORD total=0;while(total<sz){DWORD rd=0;if(!ReadFile(h,b+total,sz-total,&rd,0)||!rd){freeMem(b);CloseHandle(h);return false;}total+=rd;}CloseHandle(h);*out=b;*outSize=sz;return true;}
static void collectNamedFiles(const char* directory,int depth,const char* wanted,char files[MAX_CACHE_FILES][MAX_PATH],unsigned* count){if(depth<0||*count>=MAX_CACHE_FILES)return;char pat[MAX_PATH]={0};pathJoin(pat,sizeof(pat),directory,"*");WIN32_FIND_DATAA fd={};HANDLE h=FindFirstFileA(pat,&fd);if(h==INVALID_HANDLE_VALUE)return;do{const char* n=fd.cFileName;if((n[0]=='.'&&n[1]==0)||(n[0]=='.'&&n[1]=='.'&&n[2]==0))continue;char full[MAX_PATH]={0};pathJoin(full,sizeof(full),directory,n);if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)collectNamedFiles(full,depth-1,wanted,files,count);else if(eqI(n,wanted)&&*count<MAX_CACHE_FILES){cpy(files[*count],MAX_PATH,full);++(*count);}}while(FindNextFileA(h,&fd));FindClose(h);}
static void sortPaths(char files[MAX_CACHE_FILES][MAX_PATH],unsigned count){for(unsigned i=1;i<count;++i){char tmp[MAX_PATH]={0};cpy(tmp,MAX_PATH,files[i]);unsigned j=i;while(j>0&&cmpI(tmp,files[j-1])<0){cpy(files[j],MAX_PATH,files[j-1]);--j;}cpy(files[j],MAX_PATH,tmp);}}
static bool readCStringView(const unsigned char* bytes,DWORD size,DWORD* pos,DWORD end,const char** value){if(*pos>=end||end>size)return false;DWORD b=*pos;while(*pos<end&&bytes[*pos]!=0)++(*pos);if(*pos>=end)return false;*value=(const char*)(bytes+b);++(*pos);return true;}
struct CreatureMatch{DWORD entry;char* name[4];char* subName;DWORD typeFlags;DWORD creatureType;DWORD petFamily;DWORD rank;DWORD petSpellListId;DWORD displayId;DWORD civilian;DWORD leader;DWORD slot;char cacheSource[64];DWORD cacheBuild;char cacheLocale[8];};
static void freeCreatureMatch(CreatureMatch* r){if(!r)return;for(unsigned i=0;i<4;++i){freeMem(r->name[i]);r->name[i]=0;}freeMem(r->subName);r->subName=0;}
static void copyCreatureMatch(CreatureMatch* d,DWORD entry,const char* names[4],const char* sub,DWORD typeFlags,DWORD creatureType,DWORD petFamily,DWORD rank,DWORD petSpellListId,DWORD displayId,DWORD civilian,DWORD leader,DWORD slot,const char* source,DWORD build,const char* locale){freeCreatureMatch(d);memset(d,0,sizeof(*d));d->entry=entry;for(unsigned i=0;i<4;++i)d->name[i]=dupText(names[i]?names[i]:"");d->subName=dupText(sub?sub:"");d->typeFlags=typeFlags;d->creatureType=creatureType;d->petFamily=petFamily;d->rank=rank;d->petSpellListId=petSpellListId;d->displayId=displayId;d->civilian=civilian;d->leader=leader;d->slot=slot;cpy(d->cacheSource,sizeof(d->cacheSource),source);d->cacheBuild=build;cpy(d->cacheLocale,sizeof(d->cacheLocale),locale);}
static int seenInsert(DWORD* keys,DWORD cap,DWORD entry){DWORD pos=(entry*2654435761UL)&(cap-1),start=pos;do{if(keys[pos]==entry)return 0;if(keys[pos]==0){keys[pos]=entry;return 1;}pos=(pos+1)&(cap-1);}while(pos!=start);return -1;}
static int findMatch(CreatureMatch* rows,DWORD count,DWORD entry){for(DWORD i=0;i<count;++i)if(rows[i].entry==entry)return (int)i;return -1;}
static bool parseCreatureCacheFile(const char* path,DWORD spellId,CreatureMatch* rows,DWORD* emitted,DWORD* total,DWORD* seen,DWORD seenCap,bool* parsedAny){unsigned char* bytes=0;DWORD size=0;if(!readWholeDiskFile(path,&bytes,&size))return false;DWORD build=le32(bytes+4);if(build<4000){freeMem(bytes);return false;}char locale[8]={0};for(unsigned i=0;i<4;++i){char c=(char)bytes[8+i];locale[i]=(c>=32&&c<=126)?c:'?';}char source[64]={0};cpy(source,sizeof(source),locale);cat(source,sizeof(source),"\\creaturecache.wdb");DWORD pos=20;DWORD guard=0;bool valid=false;while(pos+8<=size&&guard++<200000){DWORD entry=le32(bytes+pos);pos+=4;if(!entry)break;DWORD entrySize=le32(bytes+pos);pos+=4;if(!entrySize)break;DWORD start=pos,end=start+entrySize;if(end<start||end>size){freeMem(bytes);return false;}const char* names[4]={0,0,0,0};const char* sub=0;bool ok=true;for(unsigned i=0;i<4;++i)if(!readCStringView(bytes,size,&pos,end,&names[i])){ok=false;break;}if(ok&&!readCStringView(bytes,size,&pos,end,&sub))ok=false;if(!ok||pos+30>end){freeMem(bytes);return false;}DWORD typeFlags=le32(bytes+pos);pos+=4;DWORD creatureType=le32(bytes+pos);pos+=4;DWORD petFamily=le32(bytes+pos);pos+=4;DWORD rank=le32(bytes+pos);pos+=4;pos+=4;DWORD petSpellListId=le32(bytes+pos);pos+=4;DWORD displayId=le32(bytes+pos);pos+=4;DWORD civilian=bytes[pos++];DWORD leader=bytes[pos++];valid=true;const unsigned char* data=petSpellListId?rowById(g_creatureSpellData,petSpellListId):0;DWORD slot=0;if(data)for(DWORD j=0;j<4;++j)if(u32(g_creatureSpellData,data,1+j)==spellId){slot=j+1;break;}if(slot){int si=seenInsert(seen,seenCap,entry);if(si<0){freeMem(bytes);cpy(g_lastError,sizeof(g_lastError),"CREATURE_CACHE_LIMIT");return false;}int idx=findMatch(rows,*emitted,entry);if(si>0){++(*total);if(*emitted<500){idx=(int)(*emitted);++(*emitted);}}if(idx>=0)copyCreatureMatch(&rows[idx],entry,names,sub,typeFlags,creatureType,petFamily,rank,petSpellListId,displayId,civilian,leader,slot,source,build,locale);}pos=end;}if(valid)*parsedAny=true;freeMem(bytes);return valid;}
static int pushCreatureBySpell(Lua50::State L,DWORD spellId){if(!spellId)return fail(L,"CreatureBySpell","SPELL_REQUIRED");if(!ensureTable(g_creatureSpellData,"DBFilesClient\\CreatureSpellData.dbc",5))return fail(L,"CreatureBySpell",g_creatureSpellData.error);char exe[MAX_PATH]={0};DWORD n=GetModuleFileNameA(0,exe,MAX_PATH);if(!n||n>=MAX_PATH)return fail(L,"CreatureBySpell","NO_EXE_PATH");parentDir(exe);char root[MAX_PATH]={0};pathJoin(root,sizeof(root),exe,"Cache\\WDB");char (*files)[MAX_PATH]=(char (*)[MAX_PATH])alloc(MAX_CACHE_FILES*MAX_PATH);if(!files)return fail(L,"CreatureBySpell","HEAP_ALLOC_FAILED");memset(files,0,MAX_CACHE_FILES*MAX_PATH);unsigned fc=0;collectNamedFiles(root,3,"creaturecache.wdb",files,&fc);if(!fc){freeMem(files);return fail(L,"CreatureBySpell","NO_CREATURE_CACHE");}sortPaths(files,fc);CreatureMatch* rows=(CreatureMatch*)alloc(sizeof(CreatureMatch)*500);DWORD seenCap=131072;DWORD* seen=(DWORD*)alloc(sizeof(DWORD)*seenCap);if(!rows||!seen){freeMem(rows);freeMem(seen);freeMem(files);return fail(L,"CreatureBySpell","HEAP_ALLOC_FAILED");}memset(rows,0,sizeof(CreatureMatch)*500);memset(seen,0,sizeof(DWORD)*seenCap);DWORD emitted=0,total=0;bool parsedAny=false;for(unsigned i=0;i<fc;++i)parseCreatureCacheFile(files[i],spellId,rows,&emitted,&total,seen,seenCap,&parsedAny);if(!parsedAny){for(DWORD i=0;i<emitted;++i)freeCreatureMatch(&rows[i]);freeMem(rows);freeMem(seen);freeMem(files);return fail(L,"CreatureBySpell","NOT_FOUND_OR_BAD_WDB");}Lua50::NewTable(L);for(DWORD i=0;i<emitted;++i){CreatureMatch& r=rows[i];Lua50::PushNumber(L,(double)(i+1));Lua50::NewTable(L);setNumber(L,"entry",r.entry);setString(L,"name",r.name[0]);setString(L,"name2",r.name[1]);setString(L,"name3",r.name[2]);setString(L,"name4",r.name[3]);setString(L,"subName",r.subName);setNumber(L,"typeFlags",r.typeFlags);setNumber(L,"creatureType",r.creatureType);setNumber(L,"petFamily",r.petFamily);setNumber(L,"rank",r.rank);setNumber(L,"petSpellListId",r.petSpellListId);setNumber(L,"displayId",r.displayId);setNumber(L,"civilian",r.civilian);setNumber(L,"leader",r.leader);setNumber(L,"slot",r.slot);setNumber(L,"spellId",spellId);setString(L,"relationSource","CreatureSpellData.dbc + creaturecache.wdb");setString(L,"cacheSource",r.cacheSource);setNumber(L,"cacheBuild",r.cacheBuild);setString(L,"cacheLocale",r.cacheLocale);Lua50::SetTable(L,-3);}Lua50::PushNumber(L,(double)total);Lua50::PushBool(L,total>emitted);for(DWORD i=0;i<emitted;++i)freeCreatureMatch(&rows[i]);freeMem(rows);freeMem(seen);freeMem(files);cpy(g_lastError,sizeof(g_lastError),"OK");return 3;}

static void appendUInt(char* o,unsigned cap,unsigned long v){char t[16];unsigned n=0;if(!v)t[n++]='0';else while(v&&n<15){t[n++]=(char)('0'+(v%10));v/=10;}for(unsigned i=0;i<n;++i){unsigned l=slen(o);if(l+1>=cap)return;o[l]=t[n-1-i];o[l+1]=0;}}
static void appendInt(char* o,unsigned cap,long v){if(v<0){cat(o,cap,"-");appendUInt(o,cap,(unsigned long)(-v));}else appendUInt(o,cap,(unsigned long)v);}
static void appendFloat4(char* o,unsigned cap,float v){if(v<0){cat(o,cap,"-");v=-v;}unsigned long whole=(unsigned long)v;float frac=(v-(float)whole)*10000.0f+0.5f;unsigned long f=(unsigned long)frac;if(f>=10000){++whole;f-=10000;}appendUInt(o,cap,whole);cat(o,cap,".");unsigned div=1000;for(int i=0;i<4;++i){unsigned l=slen(o);if(l+1>=cap)return;o[l]=(char)('0'+((f/div)%10));o[l+1]=0;div/=10;}}
enum ResolveMode { RM_UNKNOWN=0, RM_CASTER=1, RM_GROUND=2, RM_CONE=3 };
static const char* resolveModeName(ResolveMode m){if(m==RM_CASTER)return "CASTER";if(m==RM_GROUND)return "GROUND";if(m==RM_CONE)return "CONE";return "UNKNOWN";}
static ResolveMode classifyTarget(DWORD t){
    switch(t){
        case 24:case 54:case 59:case 60:return RM_CONE;
        case 8:case 16:case 28:case 29:case 31:case 34:case 52:return RM_GROUND;
        case 1:case 2:case 4:case 7:case 15:case 20:case 22:case 30:case 33:case 36:case 51:case 56:return RM_CASTER;
        default:return RM_UNKNOWN;
    }
}
static ResolveMode combineMode(ResolveMode a,ResolveMode b){if(a==RM_CONE||b==RM_CONE)return RM_CONE;if(a==RM_GROUND||b==RM_GROUND)return RM_GROUND;if(a==RM_CASTER||b==RM_CASTER)return RM_CASTER;return RM_UNKNOWN;}
static ResolveMode spellHint(const unsigned char* r){ResolveMode m=RM_UNKNOWN;for(unsigned i=0;i<3;++i){m=combineMode(m,classifyTarget(u32(g_spell,r,82+i)));m=combineMode(m,classifyTarget(u32(g_spell,r,85+i)));}return m;}
static float radiusValue(DWORD radiusId){if(!radiusId)return 0.0f;const unsigned char* r=rowById(g_radius,radiusId);if(!r)return 0.0f;float v=f32(g_radius,r,1);return (v>0.0f&&v<1000.0f)?v:0.0f;}
static DWORD resolveDuration(DWORD spellId){const unsigned char* s=rowById(g_spell,spellId);if(!s)return 0;DWORD di=u32(g_spell,s,30);const unsigned char* d=di?rowById(g_duration,di):0;long v=d?i32(g_duration,d,1):0;return v>0?(DWORD)v:0;}
struct ResolveCandidate{bool valid;DWORD castSpell,geometrySpell;float radius;ResolveMode mode;DWORD radiusIndex,targetA,targetB,depth,effect,effectIndex;};
static int resolveModeScore(ResolveMode m){if(m==RM_CASTER)return 30;if(m==RM_GROUND)return 32;if(m==RM_CONE)return 12;return 0;}
static int effectGeometryScore(DWORD e){
    switch(e){
        case 2:case 7:case 9:case 17:case 31:case 58:case 62:case 98:case 121:return 100;
        case 27:return 98;
        case 6:case 35:case 119:case 128:case 129:case 132:return 90;
        case 32:return 65;
        case 68:case 78:case 114:return 55;
        case 64:return 5;
        case 3:case 77:return 1;
        default:return 25;
    }
}
static int targetGeometryScore(DWORD a,DWORD b){DWORD v[2]={a,b};int score=0;for(unsigned i=0;i<2;++i){int s=0;switch(v[i]){case 15:case 36:s=30;break;case 8:case 16:case 28:s=30;break;case 7:s=24;break;case 22:s=18;break;case 24:case 54:case 60:s=8;break;default:s=0;}if(s>score)score=s;}return score;}
static int candidateScore(const ResolveCandidate& c){unsigned d=c.depth>4?4:c.depth;return effectGeometryScore(c.effect)+resolveModeScore(c.mode)+targetGeometryScore(c.targetA,c.targetB)+(int)d*2;}
static bool betterCandidate(const ResolveCandidate& a,const ResolveCandidate& b){if(!b.valid)return true;int as=candidateScore(a),bs=candidateScore(b);if(as!=bs)return as>bs;if(a.radius!=b.radius)return a.radius<b.radius;if(a.depth!=b.depth)return a.depth>b.depth;return a.geometrySpell<b.geometrySpell;}
static bool seenResolve(DWORD id,const DWORD* seen,unsigned count){for(unsigned i=0;i<count;++i)if(seen[i]==id)return true;return false;}
static void walkResolve(DWORD castSpell,DWORD spellId,DWORD depth,ResolveMode inherited,DWORD* seen,unsigned* seenCount,ResolveCandidate* best){
    if(!spellId||depth>4||*seenCount>=256||seenResolve(spellId,seen,*seenCount))return;
    seen[(*seenCount)++]=spellId;const unsigned char* r=rowById(g_spell,spellId);if(!r)return;ResolveMode hint=combineMode(spellHint(r),inherited);
    for(unsigned i=0;i<3;++i){DWORD effect=u32(g_spell,r,61+i),rid=u32(g_spell,r,88+i);if(!effect||!rid)continue;float rv=radiusValue(rid);if(rv<=0.0f)continue;DWORD ta=u32(g_spell,r,82+i),tb=u32(g_spell,r,85+i);ResolveMode specific=combineMode(classifyTarget(ta),classifyTarget(tb));if(specific==RM_UNKNOWN)specific=hint;ResolveCandidate c={};c.valid=true;c.castSpell=castSpell;c.geometrySpell=spellId;c.radius=rv;c.mode=specific;c.radiusIndex=rid;c.targetA=ta;c.targetB=tb;c.depth=depth;c.effect=effect;c.effectIndex=i;if(betterCandidate(c,*best))*best=c;}
    for(unsigned i=0;i<3;++i){DWORD child=u32(g_spell,r,109+i);if(child&&child!=spellId)walkResolve(castSpell,child,depth+1,hint,seen,seenCount,best);}
}
static void appendFloat3(char* o,unsigned cap,float v){if(v<0){cat(o,cap,"-");v=-v;}unsigned long whole=(unsigned long)v;unsigned long f=(unsigned long)((v-(float)whole)*1000.0f+0.5f);if(f>=1000){++whole;f-=1000;}appendUInt(o,cap,whole);cat(o,cap,".");unsigned div=100;for(int i=0;i<3;++i){unsigned l=slen(o);if(l+1>=cap)return;o[l]=(char)('0'+((f/div)%10));o[l+1]=0;div/=10;}}
static void appendSource(char* o,unsigned cap,const char* s){if(!s)return;for(unsigned i=0;s[i];++i){char c=s[i]=='|'?'_':s[i];unsigned l=slen(o);if(l+1>=cap)return;o[l]=c;o[l+1]=0;}}
static int resolveAutoRange(Lua50::State L,DWORD root){
    if(!root){Lua50::PushString(L,"E|INVALID_SPELL");return 1;}
    if(!ensureTable(g_spell,"DBFilesClient\\Spell.dbc",120)){char e[128]="E|";cat(e,sizeof(e),g_spell.error);Lua50::PushString(L,e);return 1;}
    if(!ensureTable(g_radius,"DBFilesClient\\SpellRadius.dbc",4)){char e[128]="E|";cat(e,sizeof(e),g_radius.error);Lua50::PushString(L,e);return 1;}
    if(!ensureTable(g_duration,"DBFilesClient\\SpellDuration.dbc",4)){char e[128]="E|";cat(e,sizeof(e),g_duration.error);Lua50::PushString(L,e);return 1;}
    if(!rowById(g_spell,root)){Lua50::PushString(L,"E|SPELL_NOT_FOUND");return 1;}
    ResolveCandidate best={};DWORD seen[256]={0};unsigned seenCount=0;walkResolve(root,root,0,RM_UNKNOWN,seen,&seenCount,&best);
    char out[768]={0};if(!best.valid){cpy(out,sizeof(out),"E|NO_RADIUS|");appendUInt(out,sizeof(out),root);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),resolveDuration(root));cat(out,sizeof(out),"|");appendSource(out,sizeof(out),g_spell.source);Lua50::PushString(L,out);return 1;}
    DWORD dur=resolveDuration(root);if(!dur&&best.geometrySpell!=root)dur=resolveDuration(best.geometrySpell);const char* source=best.depth==0?"DIRECT":"TRIGGER";
    cpy(out,sizeof(out),"A|");appendUInt(out,sizeof(out),root);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.geometrySpell);cat(out,sizeof(out),"|");appendFloat3(out,sizeof(out),best.radius);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),dur);cat(out,sizeof(out),"|");cat(out,sizeof(out),resolveModeName(best.mode));cat(out,sizeof(out),"|");cat(out,sizeof(out),source);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.radiusIndex);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.targetA);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.targetB);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.depth);cat(out,sizeof(out),"|");appendSource(out,sizeof(out),g_spell.source);cat(out,sizeof(out),"|");appendSource(out,sizeof(out),g_radius.source);cat(out,sizeof(out),"|");appendSource(out,sizeof(out),g_duration.source);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.effect);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),best.effectIndex);cat(out,sizeof(out),"|");appendInt(out,sizeof(out),candidateScore(best));Lua50::PushString(L,out);cpy(g_lastError,sizeof(g_lastError),"OK");return 1;
}

static char g_triggerTrace[60000]={0};
static void traceNode(DWORD spellId,DWORD depth,DWORD* seen,unsigned* seenCount){
    if(!spellId||depth>8||*seenCount>=256||seenResolve(spellId,seen,*seenCount))return;seen[(*seenCount)++]=spellId;const unsigned char* r=rowById(g_spell,spellId);if(!r)return;
    for(unsigned i=0;i<3;++i){DWORD effect=u32(g_spell,r,61+i),rid=u32(g_spell,r,88+i),ta=u32(g_spell,r,82+i),tb=u32(g_spell,r,85+i),tr=u32(g_spell,r,109+i);float rad=rid?radiusValue(rid):0.0f;if(g_triggerTrace[0])cat(g_triggerTrace,sizeof(g_triggerTrace),"\n");cat(g_triggerTrace,sizeof(g_triggerTrace),"N|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),depth);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),spellId);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),i);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),effect);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),rid);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendFloat3(g_triggerTrace,sizeof(g_triggerTrace),rad);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),ta);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),tb);cat(g_triggerTrace,sizeof(g_triggerTrace),"|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),tr);}
    if(depth<8)for(unsigned i=0;i<3;++i){DWORD child=u32(g_spell,r,109+i);if(child&&child!=spellId)traceNode(child,depth+1,seen,seenCount);}
}
static int triggerTrace(Lua50::State L,DWORD root){if(!root){Lua50::PushString(L,"E|INVALID_SPELL");return 1;}if(!ensureTable(g_spell,"DBFilesClient\\Spell.dbc",120)||!ensureTable(g_radius,"DBFilesClient\\SpellRadius.dbc",4)){Lua50::PushString(L,"E|DBC_UNAVAILABLE");return 1;}if(!rowById(g_spell,root)){Lua50::PushString(L,"E|SPELL_NOT_FOUND");return 1;}g_triggerTrace[0]=0;cpy(g_triggerTrace,sizeof(g_triggerTrace),"H|ROOT|");appendUInt(g_triggerTrace,sizeof(g_triggerTrace),root);cat(g_triggerTrace,sizeof(g_triggerTrace),"|MAX_DEPTH|8|READ_ONLY|1");DWORD seen[256]={0};unsigned seenCount=0;traceNode(root,0,seen,&seenCount);Lua50::PushString(L,g_triggerTrace);return 1;}

static bool isTotemEffect(DWORD e){return e==74||(e>=87&&e<=90);}
struct TotemCandidate{bool valid;DWORD summon;DWORD effectIndex;DWORD effectType;DWORD entry;unsigned depth;};
static bool findTotem(DWORD id,unsigned depth,DWORD* visited,unsigned* vc,TotemCandidate* out){if(!id||depth>4||*vc>=32)return false;for(unsigned i=0;i<*vc;++i)if(visited[i]==id)return false;visited[(*vc)++]=id;const unsigned char* r=rowById(g_spell,id);if(!r)return false;for(unsigned i=0;i<3;++i){DWORD e=u32(g_spell,r,61+i);if(isTotemEffect(e)){out->valid=true;out->summon=id;out->effectIndex=i;out->effectType=e;out->entry=u32(g_spell,r,106+i);out->depth=depth;return true;}}for(unsigned i=0;i<3;++i){if(u32(g_spell,r,61+i)==64){DWORD ch=u32(g_spell,r,109+i);if(ch&&findTotem(ch,depth+1,visited,vc,out))return true;}}return false;}
static int totemInfo(Lua50::State L,DWORD root){char out[256]={0};if(!root){Lua50::PushString(L,"T|0|0|0|0|0|0|0|SPELL_INVALID");return 1;}if(!ensureTable(g_spell,"DBFilesClient\\Spell.dbc",120)){cpy(out,sizeof(out),"E|TOTEM_DBC|");cat(out,sizeof(out),g_spell.error);Lua50::PushString(L,out);return 1;}DWORD vis[32]={0};unsigned vc=0;TotemCandidate c={};if(!findTotem(root,0,vis,&vc,&c)||!c.valid){cpy(out,sizeof(out),"T|0|");appendUInt(out,sizeof(out),root);cat(out,sizeof(out),"|0|0|0|0|0|NOT_TOTEM");Lua50::PushString(L,out);return 1;}DWORD life=0;const unsigned char* sr=rowById(g_spell,c.summon);DWORD di=sr?u32(g_spell,sr,30):0;if(di&&ensureTable(g_duration,"DBFilesClient\\SpellDuration.dbc",4)){const unsigned char* dr=rowById(g_duration,di);long d=dr?i32(g_duration,dr,1):0;if(d>0)life=(DWORD)d;}cpy(out,sizeof(out),"T|1|");appendUInt(out,sizeof(out),root);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),c.summon);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),c.effectIndex);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),c.effectType);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),c.entry);cat(out,sizeof(out),"|");appendUInt(out,sizeof(out),life);cat(out,sizeof(out),"|TOTEM_DBC_EVENT");Lua50::PushString(L,out);return 1;}

} // anonymous

int dispatchAutoRangeStatus(Lua50::State L){
    // Legacy UnitXP AutoRange.Status compatibility.  Reuse the already migrated
    // lazy/cached DBC tables; do not create a second loader or background path.
    if(!ensureTable(g_spell,"DBFilesClient\\Spell.dbc",120)){
        if(eqI(g_spell.error,"NO_MPQ")) Lua50::PushString(L,"E|NO_MPQ");
        else if(eqI(g_spell.error,"DBC_SCHEMA")) Lua50::PushString(L,"E|DBC_SCHEMA");
        else Lua50::PushString(L,"E|SPELL_DBC");
        return 1;
    }
    if(!ensureTable(g_radius,"DBFilesClient\\SpellRadius.dbc",4)){
        if(eqI(g_radius.error,"NO_MPQ")) Lua50::PushString(L,"E|NO_MPQ");
        else if(eqI(g_radius.error,"DBC_SCHEMA")) Lua50::PushString(L,"E|DBC_SCHEMA");
        else Lua50::PushString(L,"E|RADIUS_DBC");
        return 1;
    }
    if(!ensureTable(g_duration,"DBFilesClient\\SpellDuration.dbc",4)){
        if(eqI(g_duration.error,"NO_MPQ")) Lua50::PushString(L,"E|NO_MPQ");
        else if(eqI(g_duration.error,"DBC_SCHEMA")) Lua50::PushString(L,"E|DBC_SCHEMA");
        else Lua50::PushString(L,"E|DURATION_DBC");
        return 1;
    }
    char out[256]={0};
    cpy(out,sizeof(out),"OK|Schema=CLIENT1121-B32|Spell=");
    appendSource(out,sizeof(out),g_spell.source);
    cat(out,sizeof(out),"|Radius=");
    appendSource(out,sizeof(out),g_radius.source);
    cat(out,sizeof(out),"|Duration=");
    appendSource(out,sizeof(out),g_duration.source);
    Lua50::PushString(L,out);
    cpy(g_lastError,sizeof(g_lastError),"OK");
    return 1;
}

bool isCommand(const char* cmd){if(!cmd)return false;return eqI(cmd,"spell")||eqI(cmd,"spellradius")||eqI(cmd,"spellrange")||eqI(cmd,"spellduration")||eqI(cmd,"creaturespelldata")||eqI(cmd,"creaturespellsearch")||eqI(cmd,"creaturebyspell")||eqI(cmd,"itemdisplayinfo")||eqI(cmd,"spellvisual")||eqI(cmd,"spellvisualkit")||eqI(cmd,"spellvisualeffect")||eqI(cmd,"AutoRange.Resolve")||eqI(cmd,"AutoRange.TriggerTrace")||eqI(cmd,"AutoRange.TotemInfo");}
bool isSearchCommand(const char* cmd){return cmd&&eqI(cmd,"spellsearch");}
int dispatch(Lua50::State L,const char* cmd,unsigned long id){if(eqI(cmd,"spell"))return pushSpell(L,id);if(eqI(cmd,"spellradius"))return pushRadius(L,id);if(eqI(cmd,"spellrange"))return pushRange(L,id);if(eqI(cmd,"spellduration"))return pushDuration(L,id);if(eqI(cmd,"creaturespelldata"))return pushCreatureSpellData(L,id);if(eqI(cmd,"creaturespellsearch"))return pushCreatureSpellSearch(L,id);if(eqI(cmd,"creaturebyspell"))return pushCreatureBySpell(L,id);if(eqI(cmd,"itemdisplayinfo"))return pushItemDisplay(L,id);if(eqI(cmd,"spellvisual"))return pushVisual(L,id);if(eqI(cmd,"spellvisualkit"))return pushVisualKit(L,id);if(eqI(cmd,"spellvisualeffect"))return pushVisualEffect(L,id);if(eqI(cmd,"AutoRange.Resolve"))return resolveAutoRange(L,id);if(eqI(cmd,"AutoRange.TriggerTrace"))return triggerTrace(L,id);if(eqI(cmd,"AutoRange.TotemInfo"))return totemInfo(L,id);Lua50::PushNil(L);Lua50::PushString(L,"API_NOT_FOUND");return 2;}
int dispatchSearch(Lua50::State L,const char* cmd,const char* query,unsigned long limit){if(eqI(cmd,"spellsearch"))return pushSpellSearch(L,query,limit);Lua50::PushNil(L);Lua50::PushString(L,"API_NOT_FOUND");return 2;}
bool archiveScanAttempted(){return g_archivesAttempted;}
unsigned long archiveCount(){return g_archiveCount;}
const char* lastError(){return g_lastError;}
void resetLastError(){cpy(g_lastError,sizeof(g_lastError),"NOT_INITIALIZED");}

} // namespace TysDbc
