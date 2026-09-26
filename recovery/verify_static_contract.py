#!/usr/bin/env python3
from pathlib import Path
import re
import sys

DOTTED_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+$")


def ascii_strings(data: bytes, minimum: int = 4):
    out = set()
    buf = bytearray()
    for b in data:
        if 0x20 <= b <= 0x7E:
            buf.append(b)
        else:
            if len(buf) >= minimum:
                out.add(buf.decode("ascii"))
            buf.clear()
    if len(buf) >= minimum:
        out.add(buf.decode("ascii"))
    return out


def load_set(path: Path):
    return {line.strip() for line in path.read_text().splitlines() if line.strip() and not line.lstrip().startswith("#")}


def report(name: str, actual: set[str], expected: set[str]) -> bool:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    print(f"{name}: actual={len(actual)} expected={len(expected)} missing={len(missing)} extra={len(extra)}")
    if missing:
        print(f"{name} missing:")
        for item in missing:
            print(f"  - {item}")
    if extra:
        print(f"{name} extra:")
        for item in extra:
            print(f"  + {item}")
    return not missing and not extra


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_static_contract.py <candidate.dll>", file=sys.stderr)
        return 2

    dll = Path(sys.argv[1])
    if not dll.is_file():
        print(f"candidate DLL not found: {dll}", file=sys.stderr)
        return 2

    here = Path(__file__).resolve().parent
    expected_callable = load_set(here / "API37_CALLABLE_118.txt")
    expected_dotted = load_set(here / "API37_DOTTED_129.txt")
    expected_noncallable = expected_dotted - expected_callable

    if len(expected_callable) != 118:
        print(f"reference callable inventory is not 118: {len(expected_callable)}", file=sys.stderr)
        return 2
    if len(expected_dotted) != 129:
        print(f"reference dotted inventory is not 129: {len(expected_dotted)}", file=sys.stderr)
        return 2
    if len(expected_noncallable) != 11:
        print(f"reference non-callable dotted inventory is not 11: {len(expected_noncallable)}", file=sys.stderr)
        return 2

    strings = ascii_strings(dll.read_bytes())
    actual_dotted = {s for s in strings if DOTTED_RE.fullmatch(s)}
    actual_callable = actual_dotted - expected_noncallable

    ok_dotted = report("dotted", actual_dotted, expected_dotted)
    ok_callable = report("callable", actual_callable, expected_callable)
    if ok_dotted and ok_callable:
        print("STATIC_CONTRACT=PASS")
        return 0
    print("STATIC_CONTRACT=FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
