#!/usr/bin/env python3
from pathlib import Path
import sys


def die(msg: str) -> None:
    raise SystemExit(msg)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        die(f"post-overlay marker missing: {label}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: post_overlay_exact.py <extracted-source-root>")
    root = Path(sys.argv[1]).resolve()
    src = root / "src"
    spatial = src / "spatial_core.cpp"
    unit = src / "unit_state_core.cpp"
    if not spatial.is_file() or not unit.is_file():
        die(f"missing generated core sources under {src}")

    s = spatial.read_text()

    # Final target fast lookup 0x00464870 is a stack-call/ret8 function:
    # caller pushes GUID high then GUID low. Model it as __stdcall(low, high).
    # If the fast path misses/rejects, helper 0x1000BB5D walks the client
    # ObjectManager rooted at 0x00B41414: head=manager+0xAC and link base
    # offset=manager+0xA4; next=*(current+nextBase+4), bounded to 4096 nodes.
    old_spatial = """static bool validateObjectGuid(unsigned long object,unsigned long long*out){if(!object||!out||(object&1u)||!canRead(object+0x30u,8u))return false;unsigned long long g=*(const unsigned long long*)(object+0x30u);if(!g)return false;unsigned long type=0;if(!safeRead(object+0x14u,&type)||(type!=OBJECT_TYPE_UNIT&&type!=OBJECT_TYPE_PLAYER))return false;*out=g;return true;}
static bool resolveSelector(const char*s,unsigned long long*out,unsigned long*objectOut){if(!s||!out||!objectOut)return false;*out=0;*objectOut=0;if(tokenIndex(s)){if(!executable(UNIT_RESOLVER_FN))return false;using UnitResolverFn=unsigned long(__fastcall*)(const char*);unsigned long obj=((UnitResolverFn)UNIT_RESOLVER_FN)(s);unsigned long long g=0;if(!validateObjectGuid(obj,&g))return false;*out=g;*objectOut=obj;return true;}unsigned long long g=0;if(!parseGuidText(s,&g)||!executable(FAST_GUID_LOOKUP))return false;using GetObjectFn=unsigned long(__fastcall*)(unsigned long long);unsigned long obj=((GetObjectFn)FAST_GUID_LOOKUP)(g);unsigned long long live=0;if(!validateObjectGuid(obj,&live)||live!=g)return false;*out=g;*objectOut=obj;return true;}"""
    new_spatial = """static bool validateObjectGuid(unsigned long object,unsigned long long*out){if(!object||!out||(object&1u)||!canRead(object,0x38u)||!canRead(object+0x30u,8u))return false;unsigned long long g=*(const unsigned long long*)(object+0x30u);if(!g)return false;*out=g;return true;}
static unsigned long fastGuidObject(unsigned long long g){if(!g||!executable(FAST_GUID_LOOKUP))return 0;using Fn=unsigned long(__stdcall*)(unsigned long,unsigned long);unsigned long lo=(unsigned long)g,hi=(unsigned long)(g>>32);return ((Fn)FAST_GUID_LOOKUP)(lo,hi);}
static bool objectManagerState(unsigned long*outHead,long*outNextBase){if(!outHead||!outNextBase||!canRead(0x00B41414u,4u))return false;unsigned long mgr=*(const unsigned long*)0x00B41414u;if(!mgr||!canRead(mgr+0xACu,4u)||!canRead(mgr+0xA4u,4u))return false;*outHead=*(const unsigned long*)(mgr+0xACu);*outNextBase=*(const long*)(mgr+0xA4u);return true;}
static unsigned long findGuidObject(unsigned long long g){if(!g)return 0;unsigned long obj=fastGuidObject(g);unsigned long long live=0;if(validateObjectGuid(obj,&live)&&live==g)return obj;unsigned long cur=0;long nextBase=0;if(!objectManagerState(&cur,&nextBase))return 0;for(unsigned n=0;cur&&!(cur&1u)&&n<4096u;++n){live=0;if(validateObjectGuid(cur,&live)&&live==g)return cur;unsigned long link=(unsigned long)((long)cur+nextBase+4);unsigned long next=0;if(link&&canRead(link,4u))next=*(const unsigned long*)link;if(next==cur)break;cur=next;}return 0;}
static bool resolveSelector(const char*s,unsigned long long*out,unsigned long*objectOut){if(!s||!out||!objectOut)return false;*out=0;*objectOut=0;if(tokenIndex(s)){if(!executable(UNIT_RESOLVER_FN))return false;using UnitResolverFn=unsigned long(__fastcall*)(const char*);unsigned long obj=((UnitResolverFn)UNIT_RESOLVER_FN)(s);unsigned long long g=0;if(!validateObjectGuid(obj,&g))return false;*out=g;*objectOut=obj;return true;}unsigned long long g=0;if(!parseGuidText(s,&g))return false;unsigned long obj=findGuidObject(g);if(!obj)return false;*out=g;*objectOut=obj;return true;}"""
    s = replace_once(s, old_spatial, new_spatial, "Spatial fast GUID ABI + ObjectManager fallback")
    spatial.write_text(s)

    u = unit.read_text()
    old_unit = "static bool resolveObject(unsigned long long guid,unsigned long*out){if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;using Fn=unsigned long(__fastcall*)(unsigned long long);unsigned long obj=((Fn)FAST_GUID_LOOKUP)(guid);"
    new_unit = "static bool resolveObject(unsigned long long guid,unsigned long*out){if(!guid||!out||!executable(FAST_GUID_LOOKUP))return false;using Fn=unsigned long(__stdcall*)(unsigned long,unsigned long);unsigned long obj=((Fn)FAST_GUID_LOOKUP)((unsigned long)guid,(unsigned long)(guid>>32));"
    u = replace_once(u, old_unit, new_unit, "UnitState fast GUID ABI")
    unit.write_text(u)

    print("post-overlay exact patches applied:", spatial)
    print("post-overlay exact patches applied:", unit)


if __name__ == "__main__":
    main()
