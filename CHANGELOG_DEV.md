# CHANGELOG_DEV

## 2026-09-24

### Spatial S5-R1F1 exact public-surface/helper recovery

- Disassembled final target handlers: `Spatial.Status 0x1000649D`, `Spatial.Get 0x10006623`, `Unit.Distance 0x100077F0`, `Unit.Behind 0x10007AD8`.
- Recovered exact `Spatial.Status` fields including `EXPLICIT_QUERY_NO_BACKGROUND_WORK`, S5-R1F1 rear-axis/facing policies, no-hook/thread/timer/object-manager-scan flags, and field indices 129/130.
- Recovered exact `Spatial.Get` table shape, fixed semantic strings and `(nil, code)` error surface.
- Confirmed numeric float outputs are rounded to four decimal places.
- Recovered exact `Unit.Distance` modes/aliases: CENTER3D/GAUSSIAN, CENTER2D, RANGED/RANGED_EDGE, CHAINS/CHAINS_EDGE, MELEE/MELEE_BASE_GAP.
- Confirmed Unit.Distance success returns two values: normal `(value,"OK")`; melee `(value,"SERVER_INSPIRED_BASE_NO_LEEWAY")`; exact errors include `MELEE_Z_SEPARATION` and `BAD_MODE`.
- Confirmed Unit.Behind success returns four values: `(bool,"CLIENT_GEOMETRY",behindDot,targetFacing)`; exact error includes `BEHIND_UNAVAILABLE`.
- Split geometry and behind sampling in recovery to match target call graph: Behind does not require reach geometry; Spatial.Get tolerates unavailable behind data.
- Disassembled target position/reach/facing helpers. Corrected Spatial reach from historical `object+0x110` approximation to final `object+0x08 -> descriptor+0x204/+0x208`; radius/reach must be finite and within `[0,100]`.
- Added final facing range validation `[-100,100]`.
- Replaced x87 `fsqrt` reconstruction with the target's eight-iteration Newton sqrt approximation initialized with `max(value,1.0)`.
- Rechecked final behind helper `0x1000B7CB` instruction-by-instruction. The calibrated score is not the earlier conventional cos/sin facing dot; target code zeroes the Y coefficient before accumulation, yielding `behindDot=(actor.x-target.x)/distance2d`, with `behind = behindDot > 0`. `targetFacing` is validated and returned only as diagnostic/output data in S5-R1F1.
- Added an overlay-time exact rear-axis patch so future full builds use the disassembly-confirmed normalized-X behavior.
- Exact Spatial source object compile previously succeeded under the existing i686 clang-cl no-STL/no-default-lib flags; full latest DLL relink is still pending.
- Added/updated `recovery/API37_SPATIAL_S5R1F1_PUBLIC_SURFACE_DISASSEMBLY.md`.
- Last valid full-candidate SHA remains `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34` until a new complete relink succeeds.

### Build-id alignment and binary-delta investigation

- Restored exact final API37 build id `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration` through `apply_overlay.py`.
- Last full clang-cl/lld-link candidate succeeded: SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272`.
- Meaningful `.rdata` delta was reduced to `0x0c`; remaining known delta is mainly code.
- `UNIT_RESOLVER_UNAVAILABLE` has no proven direct/suffix absolute xref and remains unowned.

### UnitState selector exact recovery

- Recovered final selector helper at `0x100466AF`, client resolver `0x00515940`, unit-token set and GUID parser/error paths.
- Strict callable API check remained exact at `118/118`; broad dotted strings `129/129`.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
