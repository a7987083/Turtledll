# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 exact surface integrated and compile-tested.
- [x] API36 UnitState descriptor/status/events/lifecycle recovered from final target.
- [x] API37 Spatial Status/Get/Distance/Behind public surfaces recovered from final target.
- [x] Spatial final reach path: `object+0x08 -> descriptor+0x204/+0x208`, finite range `[0,100]`.
- [x] Spatial facing path: `object+0x118 -> movement+0x1c`, finite range `[-100,100]`.
- [x] Spatial distance normalization: target eight-iteration Newton sqrt path.
- [x] Spatial ranged/chains/melee formulas independently confirmed against final API37 target.
- [x] Unit.Distance mode comparison confirmed ASCII case-insensitive; exact aliases preserved.
- [x] S5-R1F1 behind helper confirmed: facing validated first; degenerate XY succeeds false/0; calibrated `behindDot=(actor.x-target.x)/distance2d`.
- [x] UnitState selector helper `0x100466AF` rechecked: public Get/Track/Untrack are string-only, token resolver is `0x00515940`, non-token path is 1..16-digit hexadecimal GUID parsing, and precise selector errors are preserved.
- [x] Recovery overlay updated for UnitState selector semantics and Spatial selector/mode/behind semantics.
- [x] Recovery overlay Python syntax validated by GitHub Actions.
- [x] Previous combined candidate reached strict callable APIs `118/118` and broad dotted strings `129/129`.
- [x] Exact final API37 build id restored through `apply_overlay.py`.
- [x] Last full linked candidate remains SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272` bytes.

## Immediate next work

- [ ] Complete a full API37 relink with the newest selector + Spatial corrections; do not assign a new DLL SHA before link succeeds.
- [ ] Re-run strict `118/118` callable API and `129/129` dotted-string regression on the new DLL.
- [ ] Recalculate PE section deltas; prior `6656`-byte gap is stale after these source changes.
- [ ] Recover the final Spatial/GUID object-resolution fallback after fast GUID lookup; current overlay mirrors token/hex surface but the target helper `0x1000BB5D` appears to retain an ObjectManager fallback path.
- [ ] Treat `UNIT_RESOLVER_UNAVAILABLE` cautiously: target string exists but owning code path remains unproven.
- [ ] Replace the truncated `recovery/archive.parts` with one complete, self-consistent API33 source archive so CI can reach overlay/build/link stages.

## Runtime validation

- [ ] Foundation.Status comparison.
- [ ] Cooldown event/reset/deadline/world-leave regression.
- [ ] UnitState exact selector/error/Get/Track/Untrack/Clear/events/world-leave regression.
- [ ] Spatial selector/Get exact table/value/error regression.
- [ ] Unit.Distance case-insensitive mode/alias/return-code regression.
- [ ] Unit.Behind tuple/degenerate-XY/calibrated rear-axis regression.
- [ ] S5-R2 ~105° remains unfinished experimental observation only; never encode it as a threshold.
