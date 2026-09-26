#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"prepare marker missing: {label}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: prepare_exact_base.py <source-root>")

    root = Path(sys.argv[1]).resolve()
    src = root / "src"
    build = root / "build" / "build.sh"
    dllmain = src / "dllmain.cpp"
    overlay_src = Path(__file__).resolve().parent / "src"

    for p in (src, build, dllmain, overlay_src):
        if not p.exists():
            raise SystemExit(f"missing required path: {p}")

    # Files that the historical partial-recovery source already contained, but
    # the source-authentic API33 LOS1 handoff does not.  Keep the later recovery
    # logic in apply_overlay.py/post_overlay_exact.py unchanged.
    copied = []
    for name in (
        "foundation_stats.h",
        "foundation_stats.cpp",
        "foundation_f1.h",
        "foundation_f1.cpp",
        "los_pair_cache.h",
        "los_pair_cache.cpp",
    ):
        source = overlay_src / name
        if not source.exists():
            raise SystemExit(f"missing recovery source: {source}")
        shutil.copy2(source, src / name)
        copied.append(name)

    b = build.read_text()
    compile_anchor = "clang-cl $CPPFLAGS $INC /Fo:corpse_marker.obj ../src/corpse_marker.cpp\n"
    compile_block = (
        compile_anchor
        + "clang-cl $CPPFLAGS $INC /Fo:foundation_stats.obj ../src/foundation_stats.cpp\n"
        + "clang-cl $CPPFLAGS $INC /Fo:foundation_f1.obj ../src/foundation_f1.cpp\n"
        + "clang-cl $CPPFLAGS $INC /Fo:cooldown_core.obj ../src/cooldown_core.cpp\n"
        + "clang-cl $CPPFLAGS $INC /Fo:unit_state_core.obj ../src/unit_state_core.cpp\n"
        + "clang-cl $CPPFLAGS $INC /Fo:spatial_core.obj ../src/spatial_core.cpp\n"
    )
    b = replace_once(b, compile_anchor, compile_block, "API37 compile scaffold")
    b = replace_once(
        b,
        "corpse_marker.obj buffer.obj",
        "corpse_marker.obj foundation_stats.obj foundation_f1.obj cooldown_core.obj unit_state_core.obj spatial_core.obj buffer.obj",
        "API37 link scaffold",
    )
    build.write_text(b)

    d = dllmain.read_text()
    d = replace_once(
        d,
        '#include "corpse_marker.h"\n',
        '#include "corpse_marker.h"\n'
        '#include "foundation_f1.h"\n'
        '#include "cooldown_core.h"\n'
        '#include "unit_state_core.h"\n'
        '#include "spatial_core.h"\n',
        "API37 module includes",
    )
    d = replace_once(
        d,
        'constexpr const char* DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1";',
        'constexpr const char* DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1-F1-CD1R2-US1R2-S5R1F1";',
        "API37 DLL version",
    )
    d = replace_once(
        d,
        'constexpr const char* API_VERSION = "33";',
        'constexpr const char* API_VERSION = "37";',
        "API37 API version",
    )
    d = replace_once(
        d,
        'constexpr const char* BUILD_ID = "20260830-v140-arx1-api33-dw1-los-paircache50";',
        'constexpr const char* BUILD_ID = "RECOVERED-API37-S5R1F1";',
        "API37 intermediate build id",
    )

    dispatch_anchor = '    if(eq(cmd,"DrinkWalk.Status")) return TysDrinkWalkNative::dispatchStatus(L);\n'
    dispatch_block = (
        '    if(eq(cmd,"Foundation.Status")) return TysFoundationF1::dispatchStatus(L);\n'
        '    if(eq(cmd,"Cooldown.Status")) return TysCooldownCore::dispatchStatus(L);\n'
        '    if(eq(cmd,"Cooldown.Get")) return TysCooldownCore::dispatchGet(L);\n'
        '    if(eq(cmd,"Cooldown.List")) return TysCooldownCore::dispatchList(L);\n'
        '    if(eq(cmd,"UnitState.Status")) return TysUnitStateCore::dispatchStatus(L);\n'
        '    if(eq(cmd,"UnitState.Get")) return TysUnitStateCore::dispatchGet(L);\n'
        '    if(eq(cmd,"UnitState.Track")) return TysUnitStateCore::dispatchTrack(L);\n'
        '    if(eq(cmd,"UnitState.Untrack")) return TysUnitStateCore::dispatchUntrack(L);\n'
        '    if(eq(cmd,"UnitState.List")) return TysUnitStateCore::dispatchList(L);\n'
        '    if(eq(cmd,"UnitState.Clear")) return TysUnitStateCore::dispatchClear(L);\n'
        '    if(eq(cmd,"Spatial.Status")) return TysSpatialCore::dispatchStatus(L);\n'
        '    if(eq(cmd,"Spatial.Get")) return TysSpatialCore::dispatchGet(L);\n'
        '    if(eq(cmd,"Unit.Distance")) return TysSpatialCore::dispatchDistance(L);\n'
        '    if(eq(cmd,"Unit.Behind")) return TysSpatialCore::dispatchBehind(L);\n'
        + dispatch_anchor
    )
    d = replace_once(d, dispatch_anchor, dispatch_block, "API37 dispatcher scaffold")
    dllmain.write_text(d)

    print("prepared exact API33 LOS1 base for API37 overlay")
    print("copied:", ", ".join(copied))
    print("patched:", build)
    print("patched:", dllmain)


if __name__ == "__main__":
    main()
