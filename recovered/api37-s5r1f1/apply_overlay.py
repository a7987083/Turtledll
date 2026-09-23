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

    copied = []
    for src in sorted(source_overlay.iterdir()):
        if not src.is_file():
            continue
        dst = target_src / src.name
        shutil.copy2(src, dst)
        copied.append(src.name)

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
        "        TysProfiler::onWorldLeaving(L);\n",
        "        TysProfiler::onWorldLeaving(L);\n"
        "        TysUnitStateCore::onWorldLeaving();\n",
        "UnitState PLAYER_LEAVING_WORLD bridge",
    )
    dllmain.write_text(d)

    print("overlay copied:", ", ".join(copied))
    print("patched:", build)
    print("patched:", dllmain)


if __name__ == "__main__":
    main()
