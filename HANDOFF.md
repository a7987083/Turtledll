# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

API33 is the last source-authentic baseline. API34-37 is behavior-equivalent reconstruction from final DLL disassembly, historical branches/source, diagnostic plugins and supporting references.

## Baseline / target

- API33 exact rebuild SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- Final API37 target SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`, size `380928`, callable APIs `118`, broad dotted strings `129`.

## Last full linked candidate

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous size delta `6656`
- callable APIs `118/118`, missing `0`, extra `0`
- dotted strings `129/129`, missing `0`, extra `0`
- exact target build id restored
- runtime verified: no

This DLL predates the newest UnitState/Spatial selector and Spatial degenerate-mode corrections. Do not present it as the latest behavior-complete build.

## Cooldown CD1-R2

Engine query `0x006E2EA0`, uint32 timing, source/kind classification, STARTED/CHANGED/READY transitions, reset semantics, exact Status/Get policy surface and world-leave reset are integrated. Successful init status is `READY_NATIVEBUS_ENGINE_QUERY`.

## UnitState US1-R2

Descriptor/status/events/lifecycle are recovered. Final descriptor path is `object+0x08 -> descriptor`, with health/power/flags fields documented in `recovery/API36_UNITSTATE_US1R2_EXACT_DISASSEMBLY.md`.

### Selector correction

Final selector helper `0x100466AF` has now been rechecked. Earlier recovery behavior was too permissive.

- public Get/Track/Untrack argument 2 is string-only
- recognized tokens: case-insensitive player/target/mouseover/pet/party1..4/raid1..40
- token resolver: client `0x00515940`, returning object pointer
- GUID read: object `+0x30/+0x34`, nonzero
- non-token syntax: optional `0x`, 1..16 hex digits, surrounding spaces/tabs only, nonzero
- precise errors: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`
- success helper status: `OK`

`apply_overlay.py` now transforms the earlier simplified UnitState selector implementation into this final public contract before build.

`UNIT_RESOLVER_UNAVAILABLE` is a different target string whose owning code path remains unproven; do not assign it speculatively.

## Spatial S5-R1F1

Public handlers:

- `Spatial.Status 0x1000649D`
- `Spatial.Get 0x10006623`
- `Unit.Distance 0x100077F0`
- `Unit.Behind 0x10007AD8`

Confirmed behavior:

- Spatial public selectors are string-only.
- token resolver is `0x00515940`; non-token input is a hex GUID path.
- current reconstructed direct GUID lookup uses `0x00464870`; target helper `0x1000BB5D` appears to keep an additional ObjectManager fallback path, still pending exact recovery.
- Unit.Distance aliases are ASCII case-insensitive via helper `0x100057EE`.
- exact RANGED/CHAINS/MELEE formulas are target-confirmed.
- reach helper uses `object+0x08 -> descriptor+0x204/+0x208`, finite `[0,100]`.
- position/facing validation and eight-iteration Newton sqrt are target-confirmed.
- Behind validates targetFacing before distance branch.
- if XY distance `<=0.0001`, Behind helper succeeds with false/dot0.
- otherwise final S5-R1F1 calibrated score is `(actor.x-target.x)/distance2d`; targetFacing is diagnostic/output only in this build.

This is client calibration behavior, not server Backstab truth. S5-R2 ~105° remains unfinished observation only and must not become a threshold.

## Build / CI

`apply_overlay.py` currently carries the newest UnitState and Spatial exact-target corrections. GitHub Actions now syntax-checks the overlay before source reconstruction; that syntax step passes.

Full CI relink remains blocked earlier in source reconstruction because `recovery/archive.parts` contains only three 12KB base64 pieces of an incomplete XZ stream. Previous log shows `xz: Unexpected end of input`.

A complete self-consistent API33 source archive must replace all existing parts; appending unrelated chunks is invalid because the current parts belong to a different compressed stream.

## Next steps

1. replace/fix CI source archive or otherwise materialize the authentic API33 source base;
2. apply current overlay and full-link API37;
3. rerun callable API `118/118` and dotted strings `129/129`;
4. recalculate PE section delta;
5. recover target GUID-object fallback traversal if it remains a meaningful behavior difference;
6. live-client regression.

Never pad to match binary size.

## Evidence priority

1. final target DLL disassembly
2. historical project branches/source handoffs
3. Aug 25-31 plugins
4. Turtle/Tortoise 1.18.1 for supporting server/protocol semantics only
5. other 1.12 client references

## Maintenance

Keep all five files synchronized: `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md`.
