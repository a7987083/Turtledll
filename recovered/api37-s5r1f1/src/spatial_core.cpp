#include <windows.h>
#include <cstdint>
#include "spatial_core.h"

namespace TysSpatialCore {
namespace {

// Recovery basis:
// - UnitXP_SP3 historical distanceBetween.cpp / inSight.cpp behavior.
// - S5-R1/S5-R1F1 diagnostic-plugin field contract.
// - S5-R1F1 binary policy strings: Range unchanged; rear-axis dot > 0 = behind.
// This is behavior reconstruction, not a claim that this is the lost original source.

constexpr std::uintptr_t FAST_GUID_LOOKUP = WoW112::FAST_GUID_LOOKUP;
constexpr std::uintptr_t UNIT_TOKEN_RESOLVER = WoW112::RESOLVE_UNIT_TOKEN;
constexpr std::uint32_t OBJECT_TYPE_UNIT   = 3u;
constexpr std::uint32_t OBJECT_TYPE_PLAYER = 4u;
constexpr float MELEE_Z_LIMIT = 6.0f;
constexpr float MIN_COMBAT_REACH = 1.5f;
constexpr float MIN_MELEE_REACH = 5.0f;
constexpr float MELEE_REACH_PAD = 1.333333373069763f;
constexpr float EPSILON_XY = 0.0001f;

struct Vec3 { float x, y, z; };

enum DistanceMeter {
    METER_CENTER3D,
    METER_CENTER2D,
    METER_RANGED,
    METER_CHAINS,
    METER_MELEE,
    METER_GAUSSIAN,
    METER_BAD
};

struct SpatialSample {
    std::uint64_t actorGuid;
    std::uint64_t targetGuid;
    std::uint32_t actorObject;
    std::uint32_t targetObject;
    Vec3 actorPos;
    Vec3 targetPos;
    float actorCombatReach;
    float actorBoundingRadius;
    float targetCombatReach;
    float targetBoundingRadius;
    float distance2d;
    float distance3d;
    float zDelta;
    float rangedEdgeGap;
    float chainsEdgeGap;
    float meleeBaseReach;
    float meleeBaseGap2d;
    bool meleeZEligible;
    float targetFacing;
    float behindDot;
    bool behindKnown;
    bool behind;
};

static volatile LONG g_queryCount=0;
static volatile LONG g_distanceCount=0;
static volatile LONG g_behindCount=0;
static volatile LONG g_unavailableCount=0;

static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static void setNil(Lua50::State L,const char*k){Lua50::PushString(L,k);Lua50::PushNil(L);Lua50::SetTable(L,-3);}

static bool readableProtection(DWORD protection){
    const DWORD p=protection&0xffu;
    return p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||
           p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool canRead(std::uintptr_t address,std::size_t bytes){
    if(!address||!bytes)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((const void*)address,&m,sizeof(m))!=sizeof(m))return false;
    if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!readableProtection(m.Protect))return false;
    const std::uintptr_t begin=(std::uintptr_t)m.BaseAddress;
    const std::uintptr_t end=begin+m.RegionSize;
    return address>=begin&&address<=end&&bytes<=end-address;
}

template<class T> static bool safeRead(std::uintptr_t a,T*out){
    if(!out||!canRead(a,sizeof(T)))return false;
    *out=*(const T*)a;
    return true;
}

static bool executable(std::uintptr_t a){
    if(!a)return false;
    MEMORY_BASIC_INFORMATION m={};
    if(VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&0xffu;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

static bool supportedBuild(){
    const unsigned char* base=(const unsigned char*)GetModuleHandleA(0);
    if(!base||base!=(const unsigned char*)WoW112::IMAGE_BASE)return false;
    if(*(const std::uint16_t*)base!=0x5A4Du)return false;
    const std::uint32_t peoff=*(const std::uint32_t*)(base+0x3Cu);
    const unsigned char* pe=base+peoff;
    if(*(const std::uint32_t*)pe!=0x00004550u)return false;
    const std::uint16_t machine=*(const std::uint16_t*)(pe+4u);
    const std::uint32_t timestamp=*(const std::uint32_t*)(pe+8u);
    const std::uint16_t magic=*(const std::uint16_t*)(pe+24u);
    return machine==WoW112::MACHINE_I386&&timestamp==WoW112::PE_TIMESTAMP&&magic==WoW112::PE32_MAGIC;
}

static bool finitef(float v){return v==v&&v>-3.4e38f&&v<3.4e38f;}
static float absf(float v){return v<0.0f?-v:v;}
static float sqrtfLocal(float v){if(v<=0.0f)return 0.0f;float x=v>1.0f?v:1.0f;for(unsigned i=0;i<8;++i)x=0.5f*(x+v/x);return x;}
static float wrapPi(float x){const float pi=3.14159265358979323846f,two=6.28318530717958647692f;while(x>pi)x-=two;while(x<-pi)x+=two;return x;}
static float sinfLocal(float x){x=wrapPi(x);const float x2=x*x;return x*(1.0f-x2*(1.0f/6.0f)+x2*x2*(1.0f/120.0f)-x2*x2*x2*(1.0f/5040.0f));}
static float cosfLocal(float x){x=wrapPi(x);const float x2=x*x;return 1.0f-x2*0.5f+x2*x2*(1.0f/24.0f)-x2*x2*x2*(1.0f/720.0f);}
static bool sameText(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b){if(*a++!=*b++)return false;}return *a==*b;}
static bool finiteVec(const Vec3&v){return finitef(v.x)&&finitef(v.y)&&finitef(v.z);}
static float nonNegative(float v){return finitef(v)&&v>0.0f?v:0.0f;}
static float maxf(float a,float b){return a>b?a:b;}
static float clampGap(float v){return v>0.0f?v:0.0f;}

static bool parseGuidText(const char*s,std::uint64_t*out){
    if(!s||!out)return false;
    while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')++s;
    if(!*s)return false;
    const char*begin=s;
    bool explicitHex=false,hexAlpha=false;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){explicitHex=true;s+=2;}
    const char*digits=s;
    unsigned count=0;
    for(;*s;++s){
        if(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')break;
        if((*s>='A'&&*s<='F')||(*s>='a'&&*s<='f'))hexAlpha=true;
        else if(*s<'0'||*s>'9')return false;
        ++count;
    }
    while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')++s;
    if(*s||count==0||count>20)return false;
    const unsigned base=(explicitHex||hexAlpha||count==16)?16u:10u;
    std::uint64_t v=0;
    for(unsigned i=0;i<count;++i){
        const char c=digits[i];
        unsigned d=0;
        if(c>='0'&&c<='9')d=(unsigned)(c-'0');
        else if(c>='A'&&c<='F')d=10u+(unsigned)(c-'A');
        else if(c>='a'&&c<='f')d=10u+(unsigned)(c-'a');
        else return false;
        if(d>=base)return false;
        const std::uint64_t old=v;
        v=v*base+d;
        if(v<old)return false;
    }
    (void)begin;
    if(!v)return false;
    *out=v;
    return true;
}

static bool resolveGuid(Lua50::State L,int idx,std::uint64_t*out){
    if(!out)return false;
    *out=0;
    if(!Lua50::IsString(L,idx))return false;
    const char*s=Lua50::ToString(L,idx);
    if(!s||!*s)return false;

    // Preserve the old UnitXP behavior: unit tokens are resolved through the client.
    if(executable(UNIT_TOKEN_RESOLVER)){
        using ResolveUnitFn=std::uint32_t(__fastcall*)(const char*);
        const std::uint32_t object=((ResolveUnitFn)UNIT_TOKEN_RESOLVER)(s);
        if(object&&!(object&1u)){
            std::uint64_t tokenGuid=0;
            if(safeRead((std::uintptr_t)object+WoW112::OFF_CGOBJECT_GUID,&tokenGuid)&&tokenGuid){
                *out=tokenGuid;return true;
            }
        }
    }
    return parseGuidText(s,out);
}

static bool resolveObject(std::uint64_t guid,std::uint32_t*out){
    if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;
    using GetObjectFn=std::uint32_t(__fastcall*)(std::uint64_t);
    const std::uint32_t object=((GetObjectFn)FAST_GUID_LOOKUP)(guid);
    if(!object||(object&1u))return false;
    std::uint32_t type=0;
    if(!safeRead((std::uintptr_t)object+0x14u,&type))return false;
    if(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER)return false;
    *out=object;
    return true;
}

static bool unitPosition(std::uint32_t object,Vec3*out){
    if(!object||!out)return false;
    std::uint32_t vtable=0,fn=0;
    if(!safeRead((std::uintptr_t)object,&vtable)||!vtable||!safeRead((std::uintptr_t)vtable+0x14u,&fn)||!executable(fn))return false;
    using GetPositionFn=Vec3*(__thiscall*)(std::uint32_t,Vec3*);
    Vec3 p={};
    ((GetPositionFn)fn)(object,&p);
    if(!finiteVec(p))return false;
    *out=p;
    return true;
}

static bool unitFacing(std::uint32_t object,float*out){
    if(!object||!out)return false;
    std::uint32_t movement=0;
    if(!safeRead((std::uintptr_t)object+0x118u,&movement)||!movement)return false;
    float f=0.0f;
    if(!safeRead((std::uintptr_t)movement+0x1cu,&f)||!finitef(f)||f<-100.0f||f>100.0f)return false;
    *out=f;
    return true;
}

static bool unitReach(std::uint32_t object,float*radius,float*reach){
    if(!object||!radius||!reach)return false;
    std::uint32_t attr=0;
    if(!safeRead((std::uintptr_t)object+WoW112::OFF_CGOBJECT_DESCRIPTOR,&attr)||!attr||(attr&1u))return false;
    float r=0.0f,c=0.0f;
    if(!safeRead((std::uintptr_t)attr+0x204u,&r)||!safeRead((std::uintptr_t)attr+0x208u,&c))return false;
    if(!finitef(r)||!finitef(c)||r<0.0f||c<0.0f||r>100.0f||c>100.0f)return false;
    *radius=r;
    *reach=c;
    return true;
}

enum PairStatus { PAIR_OK=0, PAIR_UNIT_NOT_VISIBLE, PAIR_DATA_UNAVAILABLE };

static PairStatus samplePair(Lua50::State L,SpatialSample*out){
    if(!out||Lua50::GetTop(L)<3)return PAIR_UNIT_NOT_VISIBLE;
    SpatialSample s={};
    if(!resolveGuid(L,2,&s.actorGuid)||!resolveGuid(L,3,&s.targetGuid)||s.actorGuid==s.targetGuid)return PAIR_UNIT_NOT_VISIBLE;
    if(!resolveObject(s.actorGuid,&s.actorObject)||!resolveObject(s.targetGuid,&s.targetObject))return PAIR_UNIT_NOT_VISIBLE;
    if(!unitPosition(s.actorObject,&s.actorPos)||!unitPosition(s.targetObject,&s.targetPos))return PAIR_DATA_UNAVAILABLE;
    if(!unitReach(s.actorObject,&s.actorBoundingRadius,&s.actorCombatReach)||
       !unitReach(s.targetObject,&s.targetBoundingRadius,&s.targetCombatReach))return PAIR_DATA_UNAVAILABLE;

    const float dx=s.actorPos.x-s.targetPos.x;
    const float dy=s.actorPos.y-s.targetPos.y;
    const float dz=s.actorPos.z-s.targetPos.z;
    const float d2sq=dx*dx+dy*dy;
    const float d3sq=d2sq+dz*dz;
    if(d2sq<0.0f||d3sq<0.0f)return PAIR_DATA_UNAVAILABLE;
    s.distance2d=sqrtfLocal(d2sq);
    s.distance3d=sqrtfLocal(d3sq);
    s.zDelta=absf(dz);
    s.rangedEdgeGap=clampGap(s.distance3d-s.actorCombatReach-s.targetCombatReach);
    s.chainsEdgeGap=clampGap(s.distance3d-s.actorBoundingRadius-s.targetBoundingRadius);

    const float meleeActor=maxf(MIN_COMBAT_REACH,s.actorCombatReach);
    const float meleeTarget=maxf(MIN_COMBAT_REACH,s.targetCombatReach);
    s.meleeBaseReach=maxf(MIN_MELEE_REACH,meleeActor+meleeTarget+MELEE_REACH_PAD);
    s.meleeBaseGap2d=clampGap(s.distance2d-s.meleeBaseReach);
    s.meleeZEligible=s.zDelta<MELEE_Z_LIMIT;

    s.behindKnown=false;
    s.behind=false;
    s.behindDot=0.0f;
    s.targetFacing=0.0f;
    if(unitFacing(s.targetObject,&s.targetFacing)){
        s.behindKnown=true;
        if(s.distance2d>EPSILON_XY){
            // Exact S5-R1F1 binary behavior: the calibrated rear-axis test
            // uses the normalized world-X delta. targetFacing is validated
            // and returned for diagnostics but is not consumed by this dot.
            s.behindDot=dx/s.distance2d;
            s.behind=finitef(s.behindDot)&&s.behindDot>0.0f;
        }
    }
    *out=s;
    return PAIR_OK;
}

static DistanceMeter parseMeter(Lua50::State L){
    if(Lua50::GetTop(L)<4||!Lua50::IsString(L,4))return METER_CENTER3D;
    const char*m=Lua50::ToString(L,4);
    if(!m)return METER_CENTER3D;
    if(sameText(m,"CENTER3D")||sameText(m,"GAUSSIAN"))return METER_CENTER3D;
    if(sameText(m,"CENTER2D"))return METER_CENTER2D;
    if(sameText(m,"RANGED")||sameText(m,"RANGED_EDGE"))return METER_RANGED;
    if(sameText(m,"CHAINS")||sameText(m,"CHAINS_EDGE"))return METER_CHAINS;
    if(sameText(m,"MELEE")||sameText(m,"MELEE_BASE_GAP"))return METER_MELEE;
    return METER_BAD;
}

static float distanceForMeter(const SpatialSample&s,DistanceMeter meter){
    if(meter==METER_CENTER2D)return s.distance2d;
    if(meter==METER_RANGED)return s.rangedEdgeGap;
    if(meter==METER_CHAINS)return s.chainsEdgeGap;
    if(meter==METER_MELEE)return s.meleeBaseGap2d;
    return s.distance3d;
}

static void pushGuidString(Lua50::State L,const char*k,std::uint64_t guid){
    static const char h[]="0123456789ABCDEF";char b[24]={};b[0]='0';b[1]='x';
    for(unsigned i=0;i<16;++i)b[2+i]=h[(unsigned)((guid>>((15-i)*4))&15ULL)];b[18]=0;
    setStr(L,k,b);
}

static int unavailableSpatial(Lua50::State L){++g_unavailableCount;Lua50::PushNil(L);Lua50::PushString(L,"SPATIAL_DATA_UNAVAILABLE");return 2;}
static int unavailableBehind(Lua50::State L){++g_unavailableCount;Lua50::PushNil(L);Lua50::PushString(L,"BEHIND_UNAVAILABLE");return 2;}

} // namespace

int dispatchStatus(Lua50::State L){
    Lua50::NewTable(L);
    setStr(L,"stage","S5-R1F1");
    setStr(L,"rangePolicy","EXPLICIT_QUERY_NO_BACKGROUND_WORK");
    setStr(L,"behindPolicy","CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI");
    setStr(L,"behindDotSemantics","POSITIVE_REAR_NEGATIVE_FRONT");
    setStr(L,"facingCalibration","RAW_MOVEMENT_AXIS_TREATED_AS_REAR_FROM_S5R2_LIVE_SAMPLES");
    setStr(L,"losPolicy","REUSE_UNIT_INSIGHT_LOS1_EXPLICIT");
    setStr(L,"serverBackstabPolicy","S5R2_OBSERVER_CALIBRATES_CAST_RESULTS");
    setBool(L,"newHook",false);
    setBool(L,"thread",false);
    setBool(L,"timer",false);
    setBool(L,"objectManagerScan",false);
    setNum(L,"boundingRadiusIndex",129);
    setNum(L,"combatReachIndex",130);
    return 1;
}

int dispatchGet(Lua50::State L){
    ++g_queryCount;
    if(!supportedBuild()){Lua50::PushNil(L);Lua50::PushString(L,"UNSUPPORTED_BUILD");return 2;}
    SpatialSample s={};
    const PairStatus ps=samplePair(L,&s);
    if(ps==PAIR_UNIT_NOT_VISIBLE){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_NOT_VISIBLE");return 2;}
    if(ps!=PAIR_OK)return unavailableSpatial(L);
    Lua50::NewTable(L);
    pushGuidString(L,"actorGuid",s.actorGuid);
    pushGuidString(L,"targetGuid",s.targetGuid);
    setNum(L,"distance3d",s.distance3d);
    setNum(L,"distance2d",s.distance2d);
    setNum(L,"zDelta",s.zDelta);
    setNum(L,"actorCombatReach",s.actorCombatReach);
    setNum(L,"actorBoundingRadius",s.actorBoundingRadius);
    setNum(L,"targetCombatReach",s.targetCombatReach);
    setNum(L,"targetBoundingRadius",s.targetBoundingRadius);
    setNum(L,"rangedEdgeGap",s.rangedEdgeGap);
    setNum(L,"chainsEdgeGap",s.chainsEdgeGap);
    setBool(L,"meleeZEligible",s.meleeZEligible);
    setNum(L,"meleeBaseReach",s.meleeBaseReach);
    setNum(L,"meleeBaseGap2d",s.meleeBaseGap2d);
    setBool(L,"behindKnown",s.behindKnown);
    setBool(L,"behind",s.behind);
    setNum(L,"behindDot",s.behindDot);
    setNum(L,"targetFacing",s.targetFacing);
    setStr(L,"behindSemantics","CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI");
    // S5-R1F1 deliberately does not claim server Backstab truth.
    setStr(L,"serverBehind","UNVERIFIED");
    setStr(L,"los","USE Unit.InSight EXPLICITLY");
    return 1;
}

int dispatchDistance(Lua50::State L){
    ++g_distanceCount;
    if(!supportedBuild()){Lua50::PushNil(L);Lua50::PushString(L,"UNSUPPORTED_BUILD");return 2;}
    SpatialSample s={};
    const PairStatus ps=samplePair(L,&s);
    if(ps==PAIR_UNIT_NOT_VISIBLE){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_NOT_VISIBLE");return 2;}
    if(ps!=PAIR_OK)return unavailableSpatial(L);
    const DistanceMeter meter=parseMeter(L);
    if(meter==METER_BAD){Lua50::PushNil(L);Lua50::PushString(L,"BAD_MODE");return 2;}
    if(meter==METER_MELEE&&!s.meleeZEligible){Lua50::PushNil(L);Lua50::PushString(L,"MELEE_Z_SEPARATION");return 2;}
    Lua50::PushNumber(L,distanceForMeter(s,meter));
    Lua50::PushString(L,meter==METER_MELEE?"SERVER_INSPIRED_BASE_NO_LEEWAY":"OK");
    return 2;
}

int dispatchBehind(Lua50::State L){
    ++g_behindCount;
    if(!supportedBuild()){Lua50::PushNil(L);Lua50::PushString(L,"UNSUPPORTED_BUILD");return 2;}
    SpatialSample s={};
    const PairStatus ps=samplePair(L,&s);
    if(ps==PAIR_UNIT_NOT_VISIBLE){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_NOT_VISIBLE");return 2;}
    if(ps!=PAIR_OK||!s.behindKnown)return unavailableBehind(L);
    Lua50::PushBool(L,s.behind);
    Lua50::PushString(L,"CLIENT_GEOMETRY");
    Lua50::PushNumber(L,s.behindDot);
    Lua50::PushNumber(L,s.targetFacing);
    return 4;
}

} // namespace TysSpatialCore
