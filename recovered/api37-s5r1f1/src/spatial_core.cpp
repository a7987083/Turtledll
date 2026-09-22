#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "spatial_core.h"

namespace TysSpatialCore {
namespace {

// Recovery basis:
// - UnitXP_SP3 historical distanceBetween.cpp / inSight.cpp behavior.
// - S5-R1/S5-R1F1 diagnostic-plugin field contract.
// - S5-R1F1 binary policy strings: Range unchanged; rear-axis dot > 0 = behind.
// This is behavior reconstruction, not a claim that this is the lost original source.

constexpr std::uintptr_t FAST_GUID_LOOKUP = 0x00464870u;
constexpr std::uintptr_t UNIT_GUID_FN      = 0x00515970u;
constexpr std::uint32_t OBJECT_TYPE_UNIT   = 3u;
constexpr std::uint32_t OBJECT_TYPE_PLAYER = 4u;
constexpr float MELEE_Z_LIMIT = 6.0f;
constexpr float MIN_COMBAT_REACH = 1.5f;
constexpr float MIN_MELEE_REACH = 5.0f;
constexpr float MELEE_REACH_PAD = 1.333333373069763f;
constexpr float EPSILON_XY = 0.00001f;

struct Vec3 { float x, y, z; };

enum DistanceMeter {
    METER_RANGED,
    METER_MELEE_AUTOATTACK,
    METER_AOE,
    METER_CHAINS,
    METER_GAUSSIAN
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

static bool finiteVec(const Vec3&v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::fabs(v.x)<1000000.0f&&std::fabs(v.y)<1000000.0f&&std::fabs(v.z)<1000000.0f;}
static float nonNegative(float v){return std::isfinite(v)&&v>0.0f?v:0.0f;}
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
    if(Lua50::IsNumber(L,idx)){
        const double n=Lua50::ToNumber(L,idx);
        if(n<=0.0)return false;
        *out=(std::uint64_t)n;
        return *out!=0;
    }
    if(!Lua50::IsString(L,idx))return false;
    const char*s=Lua50::ToString(L,idx);
    if(!s||!*s)return false;

    // Preserve the old UnitXP behavior: unit tokens are resolved through the client.
    if(executable(UNIT_GUID_FN)){
        using UnitGuidFn=std::uint64_t(__fastcall*)(const char*);
        const std::uint64_t tokenGuid=((UnitGuidFn)UNIT_GUID_FN)(s);
        if(tokenGuid){*out=tokenGuid;return true;}
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
    if(!safeRead((std::uintptr_t)movement+0x1cu,&f)||!std::isfinite(f))return false;
    *out=f;
    return true;
}

static bool unitReach(std::uint32_t object,float*radius,float*reach){
    if(!object||!radius||!reach)return false;
    std::uint32_t attr=0;
    if(!safeRead((std::uintptr_t)object+0x110u,&attr)||!attr||(attr&1u))return false;
    float r=0.0f,c=0.0f;
    if(!safeRead((std::uintptr_t)attr+0x1ecu,&r)||!safeRead((std::uintptr_t)attr+0x1f0u,&c))return false;
    if(!std::isfinite(r)||!std::isfinite(c))return false;
    *radius=nonNegative(r);
    *reach=nonNegative(c);
    return true;
}

static bool samplePair(Lua50::State L,SpatialSample*out){
    if(!out||Lua50::GetTop(L)<3)return false;
    SpatialSample s={};
    if(!resolveGuid(L,2,&s.actorGuid)||!resolveGuid(L,3,&s.targetGuid)||s.actorGuid==s.targetGuid)return false;
    if(!resolveObject(s.actorGuid,&s.actorObject)||!resolveObject(s.targetGuid,&s.targetObject))return false;
    if(!unitPosition(s.actorObject,&s.actorPos)||!unitPosition(s.targetObject,&s.targetPos))return false;
    if(!unitReach(s.actorObject,&s.actorBoundingRadius,&s.actorCombatReach)||
       !unitReach(s.targetObject,&s.targetBoundingRadius,&s.targetCombatReach))return false;

    const float dx=s.actorPos.x-s.targetPos.x;
    const float dy=s.actorPos.y-s.targetPos.y;
    const float dz=s.actorPos.z-s.targetPos.z;
    const float d2sq=dx*dx+dy*dy;
    const float d3sq=d2sq+dz*dz;
    if(d2sq<0.0f||d3sq<0.0f)return false;
    s.distance2d=std::sqrt(d2sq);
    s.distance3d=std::sqrt(d3sq);
    s.zDelta=std::fabs(dz);
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
    if(s.distance2d>EPSILON_XY&&unitFacing(s.targetObject,&s.targetFacing)){
        // S5-R1F1 calibration: compare target's REAR axis against target->actor.
        // This is algebraically the negative of the old UnitXP forward-axis dot.
        const float nx=dx/s.distance2d;
        const float ny=dy/s.distance2d;
        const float forwardDot=nx*std::cos(s.targetFacing)+ny*std::sin(s.targetFacing);
        s.behindDot=-forwardDot;
        s.behindKnown=std::isfinite(s.behindDot);
        s.behind=s.behindKnown&&s.behindDot>0.0f;
    }
    *out=s;
    return true;
}

static DistanceMeter parseMeter(Lua50::State L){
    if(Lua50::GetTop(L)<4||!Lua50::IsString(L,4))return METER_RANGED;
    const char*m=Lua50::ToString(L,4);
    if(!m)return METER_RANGED;
    if(std::strcmp(m,"meleeAutoAttack")==0)return METER_MELEE_AUTOATTACK;
    if(std::strcmp(m,"AoE")==0)return METER_AOE;
    if(std::strcmp(m,"chains")==0)return METER_CHAINS;
    if(std::strcmp(m,"Gaussian")==0)return METER_GAUSSIAN;
    return METER_RANGED;
}

static float distanceForMeter(const SpatialSample&s,DistanceMeter meter){
    if(meter==METER_MELEE_AUTOATTACK){
        if(!s.meleeZEligible)return s.distance3d;
        return s.meleeBaseGap2d;
    }
    if(meter==METER_CHAINS)return s.chainsEdgeGap;
    if(meter==METER_RANGED)return s.rangedEdgeGap;
    if(meter==METER_AOE){
        float total=0.0f;
        std::uint32_t actorType=0,targetType=0;
        safeRead((std::uintptr_t)s.actorObject+0x14u,&actorType);
        safeRead((std::uintptr_t)s.targetObject+0x14u,&targetType);
        if(actorType==OBJECT_TYPE_UNIT)total=s.actorCombatReach;
        if(targetType==OBJECT_TYPE_UNIT)total=s.targetCombatReach;
        return clampGap(s.distance3d-total);
    }
    return s.distance3d;
}

static void pushGuidString(Lua50::State L,const char*k,std::uint64_t guid){
    char b[24]={};
#if defined(_MSC_VER)
    std::sprintf(b,"0x%016I64X",(unsigned long long)guid);
#else
    std::sprintf(b,"0x%016llX",(unsigned long long)guid);
#endif
    setStr(L,k,b);
}

static int unavailable(Lua50::State L){++g_unavailableCount;Lua50::PushNil(L);Lua50::PushString(L,"SPATIAL_DATA_UNAVAILABLE");return 2;}

} // namespace

int dispatchStatus(Lua50::State L){
    Lua50::NewTable(L);
    setStr(L,"stage","S5-R1F1");
    setStr(L,"status","READY_EXPLICIT_QUERY_NO_BACKGROUND_WORK");
    setStr(L,"rangePolicy","CLIENT_GEOMETRY");
    setStr(L,"losPolicy","REUSE_UNIT_INSIGHT_LOS1_EXPLICIT");
    setStr(L,"behindPolicy","CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI");
    setStr(L,"behindDotSemantics","POSITIVE_REAR_NEGATIVE_FRONT");
    setStr(L,"behindSemantics","RAW_MOVEMENT_AXIS_TREATED_AS_REAR_FROM_S5R2_LIVE_SAMPLES");
    setStr(L,"serverBackstabPolicy","S5R2_OBSERVER_CALIBRATES_CAST_RESULTS");
    setNum(L,"fastGuidLookupAddress",FAST_GUID_LOOKUP);
    setNum(L,"queryCount",g_queryCount);
    setNum(L,"distanceCount",g_distanceCount);
    setNum(L,"behindCount",g_behindCount);
    setNum(L,"unavailableCount",g_unavailableCount);
    setBool(L,"backgroundThread",false);
    setBool(L,"objectManagerPolling",false);
    setBool(L,"directHook",false);
    return 1;
}

int dispatchGet(Lua50::State L){
    ++g_queryCount;
    SpatialSample s={};
    if(!samplePair(L,&s))return unavailable(L);
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
    setStr(L,"behindSemantics","POSITIVE_REAR_NEGATIVE_FRONT");
    // S5-R1F1 deliberately does not claim server Backstab truth.
    setNil(L,"serverBehind");
    return 1;
}

int dispatchDistance(Lua50::State L){
    ++g_distanceCount;
    SpatialSample s={};
    if(!samplePair(L,&s)){Lua50::PushNil(L);return 1;}
    Lua50::PushNumber(L,distanceForMeter(s,parseMeter(L)));
    return 1;
}

int dispatchBehind(Lua50::State L){
    ++g_behindCount;
    SpatialSample s={};
    if(!samplePair(L,&s)||!s.behindKnown)return unavailable(L);
    Lua50::PushBool(L,s.behind);
    Lua50::PushString(L,"CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI");
    Lua50::PushNumber(L,s.behindDot);
    Lua50::PushNumber(L,s.targetFacing);
    return 4;
}

} // namespace TysSpatialCore
