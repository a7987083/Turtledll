# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

API33 is the last source-authentic baseline. API34-37 is behavior-equivalent reconstruction from final DLL disassembly, historical branches/source, diagnostic plugins and supporting references.

## Baseline / target

- API33 exact LOS1 rebuild SHA256: `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- Final API37 target SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`.
- Target size: `380928` bytes.
- Target callable APIs: `118`.
- Target broad dotted strings: `129`.

## Current full-linked candidate

Current static-accepted build:

- source commit: `2b8c65e8fa2c0701b5c1c13b39a9a54775d360bc`
- GitHub Actions: Recovery Build #57 / run id `36261981451`
- SHA256: `b30b23ef3df5c24cb285bdb77d10bda4de8c18a1b8a8b99e6b3e50cf1d293f60`
- size: `359424` bytes
- callable APIs: `118/118`, missing `0`, extra `0`, exact set equality
- dotted strings: `129/129`, missing `0`, extra `0`, exact set equality
- runtime verified: **no**

PE external surface also matches the target:

- PE32 / i386 / Windows GUI 5.1
- image base `0x10000000`
- section alignment `0x1000`, file alignment `0x200`
- PE timestamp zero
- KERNEL32 only
- 40 imported KERNEL32 functions, exact set equality
- exports: `FirstEnterWorld`, `Load`, `TaiYangShenDianNativeStatus`, `_DllMain@12`
- `.data` size exact `0x4C00`

Current residual static section deltas:

- `.text`: recovery `0x44759`, target `0x489C0`, delta `0x4267`
- `.rdata`: recovery `0x9BC7`, target `0xA4FB`, delta `0x934`
- `.reloc`: recovery `0x4710`, target `0x4FB0`, delta `0x8A0`
- file size delta: `21504` bytes

Do not equate these size deltas with missing behavior and never pad the DLL.

## Build / CI

Build transformation order:

1. authentic API33 LOS1 source archive
2. `recovered/api37-s5r1f1/prepare_exact_base.py`
3. `recovered/api37-s5r1f1/apply_overlay.py`
4. `recovered/api37-s5r1f1/post_overlay_exact.py`
5. clang-cl/lld-link build

The source archive is complete and CI reconstruction now passes. LLVM 18 requires clang-cl object output syntax `/Fo<file>`; final post-overlay normalizes historical `/Fo:` to `/Fo` after all compile lines are present.

Recovery Build #57 passes archive reconstruction, all overlay steps, compile/link, contract smoke strings and artifact upload.

## Cooldown CD1-R2

Confirmed/integrated:

- engine query helper `0x006E2EA0`
- required 1.12.1 cooldown opcodes
- source/kind classification
- STARTED/CHANGED/READY transitions
- clear/reset/deadline semantics
- world-leave reset
- internal successful init state `READY_NATIVEBUS_ENGINE_QUERY`
- target public Status ready value `READY_COOLDOWN_CORE_C1R2` when incoming + world-tick subscriptions are ready

Runtime verification is still pending.

## UnitState US1-R2

Final selector/helper contract:

- selector helper `0x100466AF`
- Get/Track/Untrack public selector is string-only
- case-insensitive `player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40`
- token resolver `0x00515940`
- live GUID read from object `+0x30/+0x34`
- non-token path: optional `0x`, 1..16 hex digits, surrounding spaces/tabs, nonzero
- errors: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`
- snapshot fast lookup `0x00464870` uses `__stdcall(low32, high32)`
- target public Status ready value `READY_UNITSTATE_US1R2` when incoming + world-tick subscriptions are ready

`UNIT_RESOLVER_UNAVAILABLE` is a separate target literal. Current binary scan found no absolute 32-bit VA reference/direct code xref to that literal. Treat it as unresolved/possibly orphaned; do not add it merely for binary/string parity.

Runtime verification is still pending.

## Spatial S5-R1F1

Public target handlers:

- `Spatial.Status 0x1000649D`
- `Spatial.Get 0x10006623`
- `Unit.Distance 0x100077F0`
- `Unit.Behind 0x10007AD8`

Recovered behavior:

- selectors are string-only
- token resolver `0x00515940`
- literal GUID parser: optional `0x`, 1..16 hex digits
- fast lookup `0x00464870` with `__stdcall(low32, high32)`
- explicit bounded ObjectManager fallback via target helper model `0x1000BB5D`
- manager global `0x00B41414`, head `manager+0xAC`, next base `manager+0xA4`, max 4096
- Unit.Distance aliases ASCII case-insensitive via helper `0x100057EE`
- target-confirmed RANGED/CHAINS/MELEE formulas
- reach `object+0x08 -> descriptor+0x204/+0x208`, finite `[0,100]`
- facing `object+0x118 -> movement+0x1c`, finite `[-100,100]`
- eight-iteration Newton sqrt
- Behind validates facing before distance handling
- XY distance `<=0.0001` succeeds false/dot0
- S5-R1F1 calibrated score `(actor.x-target.x)/distance2d`

S5-R2 ~105° remains unfinished observation only; no threshold is implemented.

Runtime verification is still pending.

## Next steps

1. live compare Foundation.Status;
2. live Cooldown Status/Get/List, event ordering, reset, deadline and world-leave behavior;
3. live UnitState selector/error/Get/Track/Untrack/List/Clear, events and lifecycle;
4. live Spatial Status/Get/Distance/Behind and literal-GUID fallback;
5. investigate remaining `.text/.rdata/.reloc` differences only if runtime or target-disassembly evidence identifies a concrete behavior mismatch;
6. after runtime acceptance, fold final verified overlay corrections into stored reconstructed core source files for maintainability.

## Evidence priority

1. final target DLL disassembly
2. historical project branches/source handoffs
3. Aug 25-31 plugins
4. Turtle/Tortoise 1.18.1 for supporting server/protocol semantics only
5. other 1.12 client references

## Maintenance

Keep all five files synchronized: `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md`.
