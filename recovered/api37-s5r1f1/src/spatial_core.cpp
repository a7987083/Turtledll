#include <windows.h>
#include "spatial_core.h"
#include "wow112_offsets.h"
namespace TysSpatialCore { namespace {
constexpr unsigned long FAST_GUID_LOOKUP=0x00464870u;
constexpr unsigned long UNIT_GUID_FN=0x00515970u;
constexpr unsigned long OBJECT_TYPE_UNIT=3u,OBJECT_TYPE_PLAYER=4u;
constexpr float MELEE_Z_LIMIT=6.0f,MIN_COMBAT_REACH=1.5f,MIN_MELEE_REACH=5.0f,MELEE_REACH_PAD=1.333333373069763f,EPSILON_XY=0.00001f;
struct Vec3{float x,y,z;};
enum DistanceMode{MODE_CENTER3D,MODE_CENTER2D,MODE_RANGED,MODE_CHAINS,MODE_MELEE,MODE_BAD};
struct SpatialSample{unsigned long long actorGuid,targetGuid;unsigned long actorObject,targetObject;Vec3 actorPos,targetPos;float actorCombatReach,actorBoundingRadius,targetCombatReach,targetBoundingRadius,distance2d,distance3d,zDelta,rangedEdgeGap,chainsEdgeGap,meleeBaseReach,meleeBaseGap2d;bool meleeZEligible;float targetFacing,behindDot;bool behindKnown,behind;};
static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v?v:"");Lua50::SetTable(L,-3);}static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static int pushError(Lua50::State L,const char*code){Lua50::PushNil(L);Lua50::PushString(L,code);return 2;}
static bool readableProtection(DWORD p0){DWORD p=p0&0xffu;return p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool canRead(unsigned long a,unsigned long n){if(!a||!n)return false;MEMORY_BASIC_INFORMATION m={};if(VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m))return false;if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!readableProtection(m.Protect))return false;unsigned long b=(unsigned long)m.BaseAddress,e=b+(unsigned long)m.RegionSize;return a>=b&&a<=e&&n<=e-a;}
template<class T>static bool safeRead(unsigned long a,T*out){if(!out||!canRead(a,sizeof(T)))return false;*out=*(const T*)a;return true;}
static bool executable(unsigned long a){MEMORY_BASIC_INFORMATION m={};if(!a||VirtualQuery((const void*)a,&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;DWORD p=m.Protect&0xffu;return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
static bool supportedBuild(){unsigned char*base=(unsigned char*)GetModuleHandleA(0);if(!base||*(unsigned short*)base!=0x5A4D)return false;unsigned long peoff=*(unsigned long*)(base+0x3c);unsigned char*pe=base+peoff;if(*(unsigned long*)pe!=0x00004550UL)return false;unsigned short machine=*(unsigned short*)(pe+4),magic=*(unsigned short*)(pe+24);unsigned long timestamp=*(unsigned long*)(pe+8);return (unsigned long)base==WoW112::IMAGE_BASE&&machine==WoW112::MACHINE_I386&&magic==WoW112::PE32_MAGIC&&timestamp==WoW112::PE_TIMESTAMP;}
static bool finitef(float v){unsigned long b=*(unsigned long*)&v;return (b&0x7f800000u)!=0x7f800000u;}
static float absf(float v){unsigned long b=*(unsigned long*)&v;b&=0x7fffffffu;return *(float*)&b;}
static float sqrtf_x87(float v){
    float o=0.0f;
    __asm {
        fld v
        fsqrt
        fstp o
    }
    return o;
}
static void sincosf_x87(float v,float*s,float*c){
    float sv=0.0f,cv=0.0f;
    __asm {
        fld v
        fsincos
        fstp cv
        fstp sv
    }
    if(s)*s=sv;if(c)*c=cv;
}
static bool finiteVec(const Vec3&v){return finitef(v.x)&&finitef(v.y)&&finitef(v.z)&&absf(v.x)<1000000.0f&&absf(v.y)<1000000.0f&&absf(v.z)<1000000.0f;}
static float nonNegative(float v){return finitef(v)&&v>0.0f?v:0.0f;}static float maxf(float a,float b){return a>b?a:b;}static float clampGap(float v){return v>0.0f?v:0.0f;}
static double round4(float v){double x=(double)v*10000.0;long q=(long)(x+(x>=0.0?0.5:-0.5));return (double)q/10000.0;}
static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}
static bool parseGuidText(const char*s,unsigned long long*out){if(!s||!out)return false;while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')++s;if(!*s)return false;bool explicitHex=false,hexAlpha=false;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){explicitHex=true;s+=2;}const char*digits=s;unsigned count=0;for(;*s;++s){if(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')break;if((*s>='A'&&*s<='F')||(*s>='a'&&*s<='f'))hexAlpha=true;else if(*s<'0'||*s>'9')return false;++count;}while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n')++s;if(*s||count==0||count>20)return false;unsigned base=(explicitHex||hexAlpha||count==16)?16u:10u;unsigned long long v=0;for(unsigned i=0;i<count;++i){char c=digits[i];unsigned d=0;if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='A'&&c<='F')d=10u+(unsigned)(c-'A');else if(c>='a'&&c<='f')d=10u+(unsigned)(c-'a');else return false;if(d>=base)return false;unsigned long long old=v;v=v*base+d;if(v<old)return false;}if(!v)return false;*out=v;return true;}
static bool resolveGuid(Lua50::State L,int idx,unsigned long long*out){if(!out)return false;*out=0;if(Lua50::IsNumber(L,idx)){double n=Lua50::ToNumber(L,idx);if(n<=0.0)return false;*out=(unsigned long long)n;return *out!=0;}if(!Lua50::IsString(L,idx))return false;const char*s=Lua50::ToString(L,idx);if(!s||!*s)return false;if(executable(UNIT_GUID_FN)){using UnitGuidFn=unsigned long long(__fastcall*)(const char*);unsigned long long g=((UnitGuidFn)UNIT_GUID_FN)(s);if(g){*out=g;return true;}}return parseGuidText(s,out);}
static bool resolveObject(unsigned long long guid,unsigned long*out){if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;using GetObjectFn=unsigned long(__fastcall*)(unsigned long long);unsigned long object=((GetObjectFn)FAST_GUID_LOOKUP)(guid);if(!object||(object&1u))return false;unsigned long type=0;if(!safeRead(object+0x14u,&type))return false;if(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER)return false;*out=object;return true;}
static bool resolvePair(Lua50::State L,SpatialSample*out){if(!out||Lua50::GetTop(L)<3)return false;SpatialSample s={};if(!resolveGuid(L,2,&s.actorGuid)||!resolveGuid(L,3,&s.targetGuid)||s.actorGuid==s.targetGuid)return false;if(!resolveObject(s.actorGuid,&s.actorObject)||!resolveObject(s.targetGuid,&s.targetObject))return false;*out=s;return true;}
static bool unitPosition(unsigned long object,Vec3*out){if(!object||!out)return false;unsigned long vtable=0,fn=0;if(!safeRead(object,&vtable)||!vtable||!safeRead(vtable+0x14u,&fn)||!executable(fn))return false;using GetPositionFn=Vec3*(__thiscall*)(unsigned long,Vec3*);Vec3 p={};((GetPositionFn)fn)(object,&p);if(!finiteVec(p))return false;*out=p;return true;}
static bool unitFacing(unsigned long object,float*out){if(!object||!out)return false;unsigned long movement=0;if(!safeRead(object+0x118u,&movement)||!movement)return false;float f=0.0f;if(!safeRead(movement+0x1cu,&f)||!finitef(f))return false;*out=f;return true;}
static bool unitReach(unsigned long object,float*radius,float*reach){if(!object||!radius||!reach)return false;unsigned long attr=0;if(!safeRead(object+0x110u,&attr)||!attr||(attr&1u))return false;float r=0.0f,c=0.0f;if(!safeRead(attr+0x1ecu,&r)||!safeRead(attr+0x1f0u,&c))return false;if(!finitef(r)||!finitef(c))return false;*radius=nonNegative(r);*reach=nonNegative(c);return true;}
static bool sampleGeometry(SpatialSample*s){if(!s)return false;if(!unitPosition(s->actorObject,&s->actorPos)||!unitPosition(s->targetObject,&s->targetPos))return false;if(!unitReach(s->actorObject,&s->actorBoundingRadius,&s->actorCombatReach)||!unitReach(s->targetObject,&s->targetBoundingRadius,&s->targetCombatReach))return false;float dx=s->actorPos.x-s->targetPos.x,dy=s->actorPos.y-s->targetPos.y,dz=s->actorPos.z-s->targetPos.z;float d2sq=dx*dx+dy*dy,d3sq=d2sq+dz*dz;if(d2sq<0.0f||d3sq<0.0f)return false;s->distance2d=sqrtf_x87(d2sq);s->distance3d=sqrtf_x87(d3sq);s->zDelta=absf(dz);s->rangedEdgeGap=clampGap(s->distance3d-s->actorCombatReach-s->targetCombatReach);s->chainsEdgeGap=clampGap(s->distance3d-s->actorBoundingRadius-s->targetBoundingRadius);float ma=maxf(MIN_COMBAT_REACH,s->actorCombatReach),mt=maxf(MIN_COMBAT_REACH,s->targetCombatReach);s->meleeBaseReach=maxf(MIN_MELEE_REACH,ma+mt+MELEE_REACH_PAD);s->meleeBaseGap2d=clampGap(s->distance2d-s->meleeBaseReach);s->meleeZEligible=s->zDelta<MELEE_Z_LIMIT;return true;}
static bool sampleBehind(SpatialSample*s){if(!s)return false;Vec3 ap={},tp={};if(!unitPosition(s->actorObject,&ap)||!unitPosition(s->targetObject,&tp))return false;float dx=ap.x-tp.x,dy=ap.y-tp.y,d2sq=dx*dx+dy*dy;if(!(d2sq>EPSILON_XY*EPSILON_XY))return false;float d2=sqrtf_x87(d2sq),f=0.0f;if(!unitFacing(s->targetObject,&f))return false;float sv=0.0f,cv=0.0f;sincosf_x87(f,&sv,&cv);float nx=dx/d2,ny=dy/d2,forwardDot=nx*cv+ny*sv;s->targetFacing=f;s->behindDot=-forwardDot;s->behindKnown=finitef(s->behindDot);s->behind=s->behindKnown&&s->behindDot>0.0f;return s->behindKnown;}
static void pushGuidString(Lua50::State L,const char*k,unsigned long long guid){static const char h[]="0123456789ABCDEF";char b[19]={};b[0]='0';b[1]='x';for(int i=0;i<16;++i){unsigned sh=(15u-(unsigned)i)*4u;b[2+i]=h[(unsigned)((guid>>sh)&0xFULL)];}b[18]=0;setStr(L,k,b);}
static DistanceMode parseMode(Lua50::State L){if(Lua50::GetTop(L)<4||!Lua50::IsString(L,4))return MODE_CENTER3D;const char*m=Lua50::ToString(L,4);if(!m)return MODE_CENTER3D;if(sameText(m,"CENTER3D")||sameText(m,"GAUm®éÜj×