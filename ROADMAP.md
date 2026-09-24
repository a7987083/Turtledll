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
- [x] UnitState selector helper `0x100466AF` rechecked: Get/Track/Untrack are string-only, token resolver `0x00515940`, non-token path 1..16 hex digits, exact selector errors preserved.
- [x] Fast GUID lookup ABI corrected to `__stdcall(low32, high32)` for `0x00464870`.
- [x] Spatial target helper `0x1000BB5D` explicit ObjectManager fallback recovered: root `0x00B41414`, head `manager+0xAC`, link offset `manager+0xA4`, bounded to 4096 objects.
- [x] `post_overlay_exact.py` added to apply fast-GUID ABI + fallback corrections after the main overlay.
- [x] Both recovery overlay Python scripts pass GitHub Actions syntax validation.
- [x] Previous combined candidate reached callable APIs `118/118` and dotted strings `129/129`.
- [x] Exact final API37 build id restored.
- [x] Last full linked candidate remains SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272` bytes; it predates the newest corrections.

## Immediate next work

- [ ] Replace the truncated `recovery/archive.parts` with one complete self-consistent authentic API33 source archive.
- [ ] Full-link API37 with main overlay + exact post-overlay; do not assign a new candidate SHA before successful link.
- [ ] Re-run strict `118/118` callable API and `129/129` dotted-string regression on that DLL.
- [ ] Recalculate PE section deltas; prior `6656`-byte gap is stale after current corrections.
- [ ] Continue checking remaining final-target helper differences only if static delta still points to real behavior.
- [ ] Treat `UNIT_RESOLVER_UNAVAILABLE` cautiously: target string exists but owning code path remains unproven.

## Runtime validation

- [ ] Foundation.Status comparison.
- [ ] Cooldown event/reset/deadline/world-leave regression.
- [ ] UnitState exact selector/error/Get/Track/Untrack/Clear/events/world-leave regression.
- [ ] Spatial selector/Get exact table/value/error regression.
- [ ] Unit.Distance case-insensitive mode/alias/return-code regression.
- [ ] Unit.Behind tuple/degenerate-XY/calibrated rear-axis regression.
- [ ] Explicit GUID lookup fast path + fallback regression.
- [ ] S5-R2 ~105° remains unfinished experimental observation only; never encode it as a threshold.
