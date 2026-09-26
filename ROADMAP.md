# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic LOS1 baseline recovered and exact rebuild proven: `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- [x] Complete self-consistent API33 source archive restored in CI; archive reconstruction passes.
- [x] API34 Foundation F1 substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 surface integrated; engine query helper `0x006E2EA0` and required 1.12.1 cooldown opcodes restored to the exact-base adaptation layer.
- [x] API36 UnitState descriptor/status/events/lifecycle recovered; update-object opcodes restored to the exact-base adaptation layer.
- [x] API37 Spatial Status/Get/Distance/Behind public surfaces recovered from final target.
- [x] Spatial final reach path: `object+0x08 -> descriptor+0x204/+0x208`, finite range `[0,100]`.
- [x] Spatial facing path: `object+0x118 -> movement+0x1c`, finite range `[-100,100]`.
- [x] Spatial distance normalization: target eight-iteration Newton sqrt path.
- [x] Spatial ranged/chains/melee formulas independently confirmed against final API37 target.
- [x] Unit.Distance mode comparison confirmed ASCII case-insensitive; exact aliases preserved.
- [x] S5-R1F1 Behind behavior recovered: facing validated first; degenerate XY succeeds false/0; calibrated `behindDot=(actor.x-target.x)/distance2d`.
- [x] UnitState selector helper `0x100466AF` rechecked: Get/Track/Untrack are string-only, token resolver `0x00515940`, non-token path 1..16 hex digits, exact selector errors preserved.
- [x] Fast GUID lookup ABI corrected to `__stdcall(low32, high32)` for `0x00464870`.
- [x] Spatial target helper `0x1000BB5D` explicit ObjectManager fallback recovered: root `0x00B41414`, head `manager+0xAC`, link offset `manager+0xA4`, bounded to 4096 objects.
- [x] LLVM 18 clang-cl `/Fo` compatibility fixed by normalizing `/Fo:` to `/Fo` after all overlays.
- [x] Cooldown.Status public ready surface matched to `READY_COOLDOWN_CORE_C1R2` when incoming/tick subscriptions are ready.
- [x] UnitState.Status public ready surface matched to `READY_UNITSTATE_US1R2` when incoming/tick subscriptions are ready.
- [x] Full-link current recovery in GitHub Actions run #57 succeeded.
- [x] Current full candidate SHA256: `b30b23ef3df5c24cb285bdb77d10bda4de8c18a1b8a8b99e6b3e50cf1d293f60`, size `359424`.
- [x] Strict callable API regression: `118/118`, missing `0`, extra `0`, exact set equality.
- [x] Broad dotted-string regression: `129/129`, missing `0`, extra `0`, exact set equality.
- [x] PE external surface matched: PE32/i386, image base/alignment/subsystem/timestamp, KERNEL32-only import DLL, 40/40 imported functions, 4/4 exports.
- [x] Exact `118` and `129` reference inventories stored in `recovery/` and enforced by `verify_static_contract.py`.
- [x] Recovery Build #58 passed the new exact static-contract gate: `dotted 129/129`, `callable 118/118`, missing `0`, extra `0`, `STATIC_CONTRACT=PASS`.

## Static delta still open

Current target-vs-recovery section deltas after run #57/#58 (same DLL bytes):

- `.text`: recovery `0x44759`, target `0x489C0`, delta `0x4267` (16999 bytes)
- `.rdata`: recovery `0x9BC7`, target `0xA4FB`, delta `0x934` (2356 bytes)
- `.data`: exact `0x4C00` / `0x4C00`
- `.reloc`: recovery `0x4710`, target `0x4FB0`, delta `0x8A0` (2208 bytes)
- file-size delta: `21504` bytes

Do not infer missing behavior from size alone and never pad. Continue static recovery only when target disassembly or runtime regression identifies a concrete behavior mismatch.

`UNIT_RESOLVER_UNAVAILABLE` remains target-only. No absolute VA/code reference has been found so far; treat it as unresolved/possibly orphaned literal, not a confirmed missing branch.

## Runtime validation — next priority

- [ ] Foundation.Status comparison.
- [ ] Cooldown Status/Get/List plus STARTED/CHANGED/READY ordering, clear/reset, deadline and world-leave regression.
- [ ] UnitState Status/Get/Track/Untrack/List/Clear exact selector/error and lifecycle/event regression.
- [ ] Spatial Status/Get exact table/value/error regression.
- [ ] Unit.Distance case-insensitive mode/alias/return-code regression.
- [ ] Unit.Behind tuple/degenerate-XY/calibrated rear-axis regression.
- [ ] Explicit GUID lookup fast path + ObjectManager fallback regression.
- [ ] World enter/leave/reload boundary regression across reconstructed modules.
- [ ] Fold verified final overlay corrections into stored reconstructed core sources after runtime acceptance.
- [ ] S5-R2 ~105° remains unfinished experimental observation only; never encode it as a threshold.
