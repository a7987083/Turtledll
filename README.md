# Turtledll — TaiYangShenDian API37 Source Recovery

This repository reconstructs the TaiYangShenDian API37 / S5-R1F1 source line from the last complete API33 handoff plus the original API34→API37 DLL chain.

## Recovery anchors

- Last complete source baseline: `ARX1 API33 + DW1 + LOS1`, 2026-08-30.
- Final binary contract: `API37 S5-R1F1`, build id `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration`.
- Final original DLL SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`.

## What is source-authentic vs reconstructed

`recovered/api37-s5r1f1/` uses the API33 handoff source as the base. API34 Foundation F1, API35 CD1-R2, API36 US1-R2 and API37 S5-R1F1 were reconstructed from the successive original DLL contracts, strings, constants, status schemas and observed module boundaries.

The original post-API33 C++ source was not available. Therefore this repository does **not** claim byte-identical recovery of those later source files. It does preserve the final API command surface and the recovery build compiles successfully.

## Build

```bash
cd recovered/api37-s5r1f1/build
bash build.sh
```

Local recovery build SHA256 at import time:

`d0ac0158fba9d7aade4ee30afb86f14090e282556e7d86e99759ed107a7cbfff`

## Verification

`api/API37_SET_DIFF.txt` records the string-level API command comparison between the original final DLL and the recovered build. At import time: `missing=0`, `extra=0` under the repository's broader command-string extraction rule (125 strings). The earlier 118 figure is the narrower main-API namespace count; the two numbers use different filters.

Runtime behavior of reconstructed API34→37 modules still requires WoW 1.12.1 / Turtle WoW real-machine validation.
