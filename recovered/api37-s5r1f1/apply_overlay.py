#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys


def die(msg: str) -> None:
    raise SystemExit(msg)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        die(f"overlay marker missing: {label}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: apply_overlay.py <extracted-source-root>")
    root = Path(sys.argv[1]).resolve()
    repo_overlay = Path(__file__).resolve().parent
    source_overlay = repo_overlay / "src"
    target_src = root / "src"
    build = root / "build" / "build.sh"
    dllmain = target_src / "dllmain.cpp"
    if not target_src.is_dir() or not build.is_file() or not dllmain.is_file():
        die(f"invalid recovery source root: {root}")

    names = [
        "custom_event_bridge.h",
        "custom_event_bridge.cpp",
        "cooldown_classifier.h",
        "cooldown_transition.h",
        "cooldown_core.h",
        "cooldown_core.cpp",
        "unit_state_descriptor_layout.h",
        "unit_state_lifecycle.h",
        "unit_state_core.h",
        "unit_state_core.cpp",
        "spatial_core.h",
        "spatial_core.cpp",
    ]
    copied = []
    for name in names:
        src = source_overlay / name
        if not src.is_file():
            die(f"overlay source missing: {src}")
        shutil.copy2(src, target_src / name)
        copied.append(name)

    spatial = target_src / "spatial_core.cpp"
    s = spatial.read_text()

    s = replace_once(
        s,
        "constexpr unsigned long UNIT_GUID_FN=0x00515970u;",
        "constexpr unsigned long UNIT_RESOLVER_FN=0x00515940u;",
        "Spatial final unit resolver address",
    )

    # Final S5-R1F1 uses a case-insensitive ASCII compare helper at 0x100057EE
    # for Unit.Distance mode aliases.
    s = replace_once(
        s,
        "static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}",
        "static unsigned char foldAscii(unsigned char c){if(c>='A'&&c<='Z')return (unsigned char)(c|0x20u);return c;}static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){unsigned char ac=(unsigned char)a[i],bc=(unsigned char)b[i];if(foldAscii(ac)!=foldAscii(bc))return false;if(!ac||!bc)return ac==bc;}}",
        "S5-R1F1 case-insensitive mode compare",
    )

    # Final pair selector path is string-only. Unit tokens are resolved by the
    # client object resolver at 0x00515940 and GUID literals are 1..16 hex
    # digits with optional 0x and surrounding space/tab only.
    old_spatial_selector = """static bool parseGuidText(const char*s,unsigned long long*out){if(!s||!out)return false;while(*s==' '||*s=='\\t'||*s=='\\r'||*s=='\\n')++s;if(!*s)return false;bool explicitHex=false,hexAlpha=false;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){explicitHex=true;s+=2;}const char*digits=s;unsigned count=0;for(;*s;++s){if(*s==' '||*s=='\\t'||*s=='\\r'||*s=='\\n')break;if((*s>='A'&&*s<='F')||(*s>='a'&&*s<='f'))hexAlpha=true;else if(*s<'0'||*s>'9')return false;++count;}while(*s==' '||*s=='\\t'||*s=='\\r'||*s=='\\n')++s;if(*s||count==0||count>20)return false;unsigned base=(explicitHex||hexAlpha||count==16)?16u:10u;unsigned long long v=0;for(unsigned i=0;i<count;++i){char c=digits[i];unsigned d=0;if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='A'&&c<='F')d=10u+(unsigned)(c-'A');else if(c>='a'&&c<='f')d=10u+(unsigned)(c-'a');else return false;if(d>=base)return false;unsigned long long old=v;v=v*base+d;if(v<old)return false;}if(!v)return false;*out=v;return true;}
static bool resolveGuid(Lua50::State L,int idx,unsigned long long*out){if(!out)return false;*out=0;if(Lua50::IsNumber(L,idx)){double n=Lua50::ToNumber(L,idx);if(n<=0.0)return false;*out=(unsigned long long)n;return *out!=0;}if(!Lua50::IsString(L,idx))return false;const char*s=Lua50::ToString(L,idx);if(!s||!*s)return false;if(executable(UNIT_GUID_FN)){using UnitGuidFn=unsigned long long(__fastcall*)(const char*);unsigned long long g=((UnitGuidFn)UNIT_GUID_FN)(s);if(g){*out=g;return true;}}return parseGuidText(s,out);}
static bool resolveObject(unsigned long long guid,unsigned long*out){if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;using GetObjectFn=unsigned long(__fastcall*)(unsigned long long);unsigned long object=((GetObjectFn)FAST_GUID_LOOKUP)(guid);if(!object||(object&1u))return false;unsigned long type=0;if(!safeRead(object+0x14u,&type))return false;if(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER)return false;*out=object;return true;}
static bool resolvePair(Lua50::State L,SpatialSample*out){if(!out||Lua50::GetTop(L)<3)return false;SpatialSample s={};if(!resolveGuid(L,2,&s.actorGuid)||!resolveGuid(L,3,&s.targetGuid)||s.actorGuid==s.targetGuid)return false;if(!resolveObject(s.actorGuid,&s.actorObject)||!resolveObject(s.targetGuid,&s.targetObject))return false;*out=s;return true;}"""
    new_spatial_selector = """static bool hexNibble(char c,unsigned*out){if(c>='0'&&c<='9'){*out=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*out=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*out=(unsigned)(c-'A'+10);return true;}return false;}
static bool parseGuidText(const char*s,unsigned long long*out){if(!s||!out)return false;*out=0;while(*s==' '||*s=='\\t')++s;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;const char*digits=s;unsigned count=0;while(*s&&*s!=' '&&*s!='\\t'){unsigned n=0;if(!hexNibble(*s,&n)||count>=16)return false;++count;++s;}while(*s==' '||*s=='\\t')++s;if(*s||!count)return false;unsigned long long v=0;for(unsigned i=0;i<count;++i){unsigned n=0;if(!hexNibble(digits[i],&n))return false;v=(v<<4)|n;}if(!v)return false;*out=v;return true;}
static bool tokenIndex(const char*s){if(!s)return false;if(sameText(s,"player")||sameText(s,"target")||sameText(s,"mouseover")||sameText(s,"pet"))return true;const char*p=s;if(foldAscii((unsigned char)p[0])=='p'&&foldAscii((unsigned char)p[1])=='a'&&foldAscii((unsigned char)p[2])=='r'&&foldAscii((unsigned char)p[3])=='t'&&foldAscii((unsigned char)p[4])=='y'&&p[5]>='1'&&p[5]<='4'&&!p[6])return true;if(foldAscii((unsigned char)p[0])=='r'&&foldAscii((unsigned char)p[1])=='a'&&foldAscii((unsigned char)p[2])=='i'&&foldAscii((unsigned char)p[3])=='d'){unsigned n=0,i=4;if(p[i]<'0'||p[i]>'9')return false;for(;p[i]>='0'&&p[i]<='9';++i)n=n*10u+(unsigned)(p[i]-'0');return !p[i]&&n>=1u&&n<=40u;}return false;}
static bool validateObjectGuid(unsigned long object,unsigned long long*out){if(!object||!out||(object&1u)||!canRead(object+0x30u,8u))return false;unsigned long long g=*(const unsigned long long*)(object+0x30u);if(!g)return false;unsigned long type=0;if(!safeRead(object+0x14u,&type)||(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER))return false;*out=g;return true;}
static bool resolveSelector(const char*s,unsigned long long*out,unsigned long*objectOut){if(!s||!out||!objectOut)return false;*out=0;*objectOut=0;if(tokenIndex(s)){if(!executable(UNIT_RESOLVER_FN))return false;using UnitResolverFn=unsigned long(__fastcall*)(const char*);unsigned long obj=((UnitResolverFn)UNIT_RESOLVER_FN)(s);unsigned long long g=0;if(!validateObjectGuid(obj,&g))return false;*out=g;*objectOut=obj;return true;}unsigned long long g=0;if(!parseGuidText(s,&g)||!executable(FAST_GUID_LOOKUP))return false;using GetObjectFn=unsigned long(__fastcall*)(unsigned long long);unsigned long obj=((GetObjectFn)FAST_GUID_LOOKUP)(g);unsigned long long live=0;if(!validateObjectGuid(obj,&live)||live!=g)return false;*out=g;*objectOut=obj;return true;}
static bool resolvePair(Lua50::State L,SpatialSample*out){if(!out||Lua50::GetTop(L)<3||!Lua50::IsString(L,2)||!Lua50::IsString(L,3))return false;const char*a=Lua50::ToString(L,2),*t=Lua50::ToString(L,3);SpatialSample s={};if(!resolveSelector(a,&s.actorGuid,&s.actorObject)||!resolveSelector(t,&s.targetGuid,&s.targetObject)||s.actorGuid==s.targetGuid)return false;*out=s;return true;}"""
    s = replace_once(s, old_spatial_selector, new_spatial_selector, "Spatial exact selector pair resolver")

    # Final behind helper 0x1000B7CB validates target facing before evaluating
    # the calibrated rear-axis score. A <=0.0001 XY distance succeeds with
    # behind=false and dot=0.
    s = replace_once(
        s,
        "static bool sampleBehind(SpatialSample*s){if(!s)return false;Vec3 ap={},tp={};if(!unitPosition(s->actorObject,&ap)||!unitPosition(s->targetObject,&tp))return false;float dx=ap.x-tp.x,dy=ap.y-tp.y,d2sq=dx*dx+dy*dy;if(!(d2sq>EPSILON_XY*EPSILON_XY))return false;float d2=sqrtApprox(d2sq),f=0.0f;if(!unitFacing(s->targetObject,&f))return false;float sv=0.0f,cv=0.0f;sincosf_x87(f,&sv,&cv);float nx=dx/d2,ny=dy/d2,forwardDot=nx*cv+ny*sv;s->targetFacing=f;s->behindDot=-forwardDot;s->behindKnown=finitef(s->behindDot);s->behind=s->behindKnown&&s->behindDot>0.0f;return s->behindKnown;}",
        "static bool sampleBehind(SpatialSample*s){if(!s)return false;Vec3 ap={},tp={};if(!unitPosition(s->actorObject,&ap)||!unitPosition(s->targetObject,&tp))return false;float f=0.0f;if(!unitFacing(s->targetObject,&f))return false;s->targetFacing=f;float dx=ap.x-tp.x,dy=ap.y-tp.y,d2sq=dx*dx+dy*dy;float d2=sqrtApprox(d2sq);s->behindKnown=true;if(!(d2>EPSILON_XY)){s->behindDot=0.0f;s->behind=false;return true;}s->behindDot=dx/d2;s->behindKnown=finitef(s->behindDot);s->behind=s->behindKnown&&s->behindDot>0.0f;return s->behindKnown;}",
        "S5-R1F1 calibrated rear-axis and degenerate XY path",
    )
    spatial.write_text(s)

    unit = target_src / "unit_state_core.cpp"
    u = unit.read_text()
    u = replace_once(
        u,
        "constexpr unsigned long UNIT_GUID_FN=0x00515970u;",
        "constexpr unsigned long UNIT_GUID_FN=0x00515940u;",
        "UnitState final unit resolver address",
    )
    old_unit_selector = """static bool hexNibble(char c,unsigned*o){if(c>='0'&&c<='9'){*o=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*o=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*o=(unsigned)(c-'A'+10);return true;}return false;}static bool parseGuidText(const char*s,unsigned long long*out){if(!s||!out)return false;*out=0;while(*s==' '||*s=='\\t'||*s=='\\r'||*s=='\\n')++s;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;unsigned long long v=0;unsigned d=0;for(;*s&&d<16;++s,++d){unsigned n=0;if(!hexNibble(*s,&n))return false;v=(v<<4)|n;}if(*s||!d||!v)return false;*out=v;return true;}
static bool resolveGuid(Lua50::State L,int idx,unsigned long long*out){if(!out)return false;*out=0;if(Lua50::IsNumber(L,idx)){double n=Lua50::ToNumber(L,idx);if(n<=0)return false;*out=(unsigned long long)n;return *out!=0;}if(!Lua50::IsString(L,idx))return false;const char*s=Lua50::ToString(L,idx);if(!s||!*s)return false;if(executable(UNIT_GUID_FN)){using Fn=unsigned long long(__fastcall*)(const char*);unsigned long long g=((Fn)UNIT_GUID_FN)(s);if(g){*out=g;return true;}}return parseGuidText(s,out);}"""
    new_unit_selector = """static bool hexNibble(char c,unsigned*o){if(c>='0'&&c<='9'){*o=(unsigned)(c-'0');return true;}if(c>='a'&&c<='f'){*o=(unsigned)(c-'a'+10);return true;}if(c>='A'&&c<='F'){*o=(unsigned)(c-'A'+10);return true;}return false;}static unsigned char foldSelector(unsigned char c){if(c>='A'&&c<='Z')return (unsigned char)(c|0x20u);return c;}static bool sameSelector(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){unsigned char ac=(unsigned char)a[i],bc=(unsigned char)b[i];if(foldSelector(ac)!=foldSelector(bc))return false;if(!ac||!bc)return ac==bc;}}static bool isUnitToken(const char*s){if(!s)return false;if(sameSelector(s,"player")||sameSelector(s,"target")||sameSelector(s,"mouseover")||sameSelector(s,"pet"))return true;if(foldSelector((unsigned char)s[0])=='p'&&foldSelector((unsigned char)s[1])=='a'&&foldSelector((unsigned char)s[2])=='r'&&foldSelector((unsigned char)s[3])=='t'&&foldSelector((unsigned char)s[4])=='y'&&s[5]>='1'&&s[5]<='4'&&!s[6])return true;if(foldSelector((unsigned char)s[0])=='r'&&foldSelector((unsigned char)s[1])=='a'&&foldSelector((unsigned char)s[2])=='i'&&foldSelector((unsigned char)s[3])=='d'){unsigned n=0,i=4;if(s[i]<'0'||s[i]>'9')return false;for(;s[i]>='0'&&s[i]<='9';++i)n=n*10u+(unsigned)(s[i]-'0');return !s[i]&&n>=1u&&n<=40u;}return false;}static bool parseGuidText(const char*s,unsigned long long*out){if(!s||!out)return false;*out=0;while(*s==' '||*s=='\\t')++s;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;const char*d=s;unsigned count=0;while(*s&&*s!=' '&&*s!='\\t'){unsigned n=0;if(!hexNibble(*s,&n)||count>=16)return false;++count;++s;}while(*s==' '||*s=='\\t')++s;if(*s||!count)return false;unsigned long long v=0;for(unsigned i=0;i<count;++i){unsigned n=0;if(!hexNibble(d[i],&n))return false;v=(v<<4)|n;}if(!v)return false;*out=v;return true;}
static bool resolveSelector(const char*s,unsigned long long*out,const char**err){if(err)*err="BAD_SELECTOR";if(!s||!out)return false;*out=0;if(isUnitToken(s)){if(!executable(UNIT_GUID_FN)){if(err)*err="RESOLVE_UNIT_UNAVAILABLE";return false;}using Fn=unsigned long(__fastcall*)(const char*);unsigned long obj=((Fn)UNIT_GUID_FN)(s);if(!obj||!canRead(obj+0x30u,8u)){if(err)*err="UNIT_NOT_FOUND";return false;}unsigned long long g=*(const unsigned long long*)(obj+0x30u);if(!g){if(err)*err="UNIT_NOT_FOUND";return false;}*out=g;if(err)*err="OK";return true;}if(!parseGuidText(s,out)){if(err)*err="GUID_INVALID";return false;}if(err)*err="OK";return true;}"""
    u = replace_once(u, old_unit_selector, new_unit_selector, "UnitState exact selector helper")
    u = replace_once(
        u,
        "int dispatchTrack(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,\"BAD_SELECTOR\");return 2;}",
        "int dispatchTrack(Lua50::State L){initialize();unsigned long long g=0;const char*err=\"BAD_SELECTOR\";if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)||!resolveSelector(Lua50::ToString(L,2),&g,&err)){Lua50::PushNil(L);Lua50::PushString(L,err);return 2;}",
        "UnitState.Track exact selector errors",
    )
    u = replace_once(
        u,
        "int dispatchUntrack(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushBool(L,false);Lua50::PushString(L,\"BAD_SELECTOR\");return 2;}",
        "int dispatchUntrack(Lua50::State L){initialize();unsigned long long g=0;const char*err=\"BAD_SELECTOR\";if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)||!resolveSelector(Lua50::ToString(L,2),&g,&err)){Lua50::PushBool(L,false);Lua50::PushString(L,err);return 2;}",
        "UnitState.Untrack exact selector errors",
    )
    u = replace_once(
        u,
        "int dispatchGet(Lua50::State L){initialize();unsigned long long g=0;if(Lua50::GetTop(L)<2||!resolveGuid(L,2,&g)){Lua50::PushNil(L);Lua50::PushString(L,\"BAD_SELECTOR\");return 2;}",
        "int dispatchGet(Lua50::State L){initialize();unsigned long long g=0;const char*err=\"BAD_SELECTOR\";if(Lua50::GetTop(L)<2||!Lua50::IsString(L,2)||!resolveSelector(Lua50::ToString(L,2),&g,&err)){Lua50::PushNil(L);Lua50::PushString(L,err);return 2;}",
        "UnitState.Get exact selector errors",
    )
    unit.write_text(u)

    b = build.read_text()
    b = replace_once(
        b,
        "clang-cl $CPPFLAGS $INC /Fo:cooldown_core.obj ../src/cooldown_core.cpp\n",
        "clang-cl $CPPFLAGS $INC /Fo:custom_event_bridge.obj ../src/custom_event_bridge.cpp\n"
        "clang-cl $CPPFLAGS $INC /Fo:cooldown_core.obj ../src/cooldown_core.cpp\n",
        "compile custom_event_bridge",
    )
    b = replace_once(
        b,
        "foundation_stats.obj foundation_f1.obj cooldown_core.obj",
        "foundation_stats.obj foundation_f1.obj custom_event_bridge.obj cooldown_core.obj",
        "link custom_event_bridge",
    )
    build.write_text(b)

    d = dllmain.read_text()
    d = replace_once(
        d,
        'constexpr const char* BUILD_ID = "RECOVERED-API37-S5R1F1";',
        'constexpr const char* BUILD_ID = "20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration";',
        "exact API37 build id",
    )
    if "TysCooldownCore::onWorldLeaving();" not in d:
        if "        TysUnitStateCore::onWorldLeaving();\n" in d:
            d = d.replace(
                "        TysUnitStateCore::onWorldLeaving();\n",
                "        TysCooldownCore::onWorldLeaving();\n"
                "        TysUnitStateCore::onWorldLeaving();\n",
                1,
            )
        else:
            d = replace_once(
                d,
                "        TysProfiler::onWorldLeaving(L);\n",
                "        TysProfiler::onWorldLeaving(L);\n"
                "        TysCooldownCore::onWorldLeaving();\n"
                "        TysUnitStateCore::onWorldLeaving();\n",
                "Cooldown/UnitState PLAYER_LEAVING_WORLD bridge",
            )
    elif "TysUnitStateCore::onWorldLeaving();" not in d:
        d = d.replace(
            "        TysCooldownCore::onWorldLeaving();\n",
            "        TysCooldownCore::onWorldLeaving();\n"
            "        TysUnitStateCore::onWorldLeaving();\n",
            1,
        )
    dllmain.write_text(d)

    print("overlay copied:", ", ".join(copied))
    print("patched:", spatial)
    print("patched:", unit)
    print("patched:", build)
    print("patched:", dllmain)


if __name__ == "__main__":
    main()
