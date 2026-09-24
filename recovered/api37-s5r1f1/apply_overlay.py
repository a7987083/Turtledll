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

    # Final S5-R1F1 uses a case-insensitive ASCII compare helper at 0x100057EE
    # for Unit.Distance mode aliases.
    s = replace_once(
        s,
        "static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}",
        "static unsigned char foldAscii(unsigned char c){if(c>='A'&&c<='Z')return (unsigned char)(c|0x20u);return c;}static bool sameText(const char*a,const char*b){if(!a||!b)return false;for(unsigned i=0;;++i){unsigned char ac=(unsigned char)a[i],bc=(unsigned char)b[i];if(foldAscii(ac)!=foldAscii(bc))return false;if(!ac||!bc)return ac==bc;}}",
        "S5-R1F1 case-insensitive mode compare",
    )

    # Final behind helper 0x1000B7CB validates target facing before evaluating
    # the calibrated rear-axis score. The score itself is normalized X delta;
    # targetFacing remains diagnostic/output data in this build. A <=0.0001 XY
    # distance succeeds with behind=false and dot=0 instead of returning an
    # unavailable error.
    s = replace_once(
        s,
        "static bool sampleBehind(SpatialSample*s){if(!s)return false;Vec3 ap={},tp={};if(!unitPosition(s->actorObject,&ap)||!unitPosition(s->targetObject,&tp))return false;float dx=ap.x-tp.x,dy=ap.y-tp.y,d2sq=dx*dx+dy*dy;if(!(d2sq>EPSILON_XY*EPSILON_XY))return false;float d2=sqrtApprox(d2sq),f=0.0f;if(!unitFacing(s->targetObject,&f))return false;float sv=0.0f,cv=0.0f;sincosf_x87(f,&sv,&cv);float nx=dx/d2,ny=dy/d2,forwardDot=nx*cv+ny*sv;s->targetFacing=f;s->behindDot=-forwardDot;s->behindKnown=finitef(s->behindDot);s->behind=s->behindKnown&&s->behindDot>0.0f;return s->behindKnown;}",
        "static bool sampleBehind(SpatialSample*s){if(!s)return false;Vec3 ap={},tp={};if(!unitPosition(s->actorObject,&ap)||!unitPosition(s->targetObject,&tp))return false;float f=0.0f;if(!unitFacing(s->targetObject,&f))return false;s->targetFacing=f;float dx=ap.x-tp.x,dy=ap.y-tp.y,d2sq=dx*dx+dy*dy;float d2=sqrtApprox(d2sq);s->behindKnown=true;if(!(d2>EPSILON_XY)){s->behindDot=0.0f;s->behind=false;return true;}s->behindDot=dx/d2;s->behindKnown=finitef(s->behindDot);s->behind=s->behindKnown&&s->behindDot>0.0f;return s->behindKnown;}",
        "S5-R1F1 calibrated rear-axis and degenerate XY path",
    )
    spatial.write_text(s)

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
    print("patched:", build)
    print("patched:", dllmain)


if __name__ == "__main__":
    main()
