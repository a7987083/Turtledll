# Turtledll — TaiYangShenDian API37 Source Recovery

This repository reconstructs the TaiYangShenDian API37 / S5-R1F1 source line from the last complete API33 handoff plus the original API34→API37 DLL chain.

## Recovery anchors

- Last source-authentic baseline: `ARX1 API33 + DW1 + LOS1`, 2026-08-30.
- Exact API33 rebuild SHA256: `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- Final binary contract: API37 S5-R1F1, build id `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration`.
- Final original API37 DLL SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`.

## Source-authentic vs reconstructed

API33 is source-authentic and has been rebuilt byte-identically. API34 Foundation F1, API35 Cooldown CD1-R2, API36 UnitState US1-R2 and API37 Spatial S5-R1F1 are behavior-equivalent reconstructions based on final DLL disassembly, historical branches/source, diagnostic plugins, protocol references and repeated build validation.

The original post-API33 C++ source text is not claimed to have been recovered byte-for-byte.

## Current build

The current full-linked static-accepted candidate was produced by Recovery Build #57 from source commit `2b8c65e8fa2c0701b5c1c13b39a9a54775d360bc`:

- SHA256: `b30b23ef3df5c24cb285bdb77d10bda4de8c18a1b8a8b99e6b3e50cf1d293f60`
- size: `359424` bytes
- runtime verified: **no**

Build transformation order:

1. authentic API33 LOS1 source archive
2. `recovered/api37-s5r1f1/prepare_exact_base.py`
3. `recovered/api37-s5r1f1/apply_overlay.py`
4. `recovered/api37-s5r1f1/post_overlay_exact.py`
5. clang-cl / lld-link

## Static verification

Against the final original API37 DLL:

- callable command inventory: `118/118`, missing `0`, extra `0`, exact set equality
- broad dotted-string inventory: `129/129`, missing `0`, extra `0`, exact set equality
- KERNEL32 imported functions: `40/40`, exact set equality
- exports: exact 4-name set (`FirstEnterWorld`, `Load`, `TaiYangShenDianNativeStatus`, `_DllMain@12`)
- PE32/i386, image base, alignments, Windows GUI 5.1 subsystem and zero timestamp match
- `.data` size matches exactly at `0x4C00`

Reference inventories are stored at:

- `recovery/API37_CALLABLE_118.txt`
- `recovery/API37_DOTTED_129.txt`

`recovery/verify_static_contract.py` provides exact set verification and is wired into the Recovery Build workflow.

## Remaining work

Static public-surface recovery is complete enough for live regression, but the reconstructed DLL is not byte-identical to the original. Remaining section/file-size differences are tracked in `PROJECT_STATE.json` and `KNOWN_ISSUES.md`; they must not be closed by padding or speculative code.

The next priority is live WoW 1.12.1 / Turtle WoW validation of Foundation, Cooldown, UnitState and Spatial behavior, especially event/lifecycle timing and explicit GUID fallback paths.
