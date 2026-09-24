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
- previous size delta `6656` (stale after latest source corrections)
- callable APIs `118/118`, missing `0`, extra `0`
- dotted strings `129/129`, missing `0`, extra `0`
- runtime verified: no

This DLL predates the newest selector, Spatial, and fast-GUID ABI/fallback corrections. Do not present it as the latest behavior-complete build.

## Cooldown CD1-R2

Engine query `0x006E2EA0`, source/kind classification, STARTED/CHANGED/READY transitions, reset semantics, exact Status/Get policy surface and world-leave reset are integrated. Successful init status is `READY_NATIVEBUS_ENGINE_QUERY`.

## UnitState US1-R2

Final descriptor path is `object+0x08 -> descriptor`; health/power/flags/event/lifecycle behavior is documented in `recovery/API36_UNITSTATE_US1R2_EXACT_DISASSEMBLY.md`.

Final selector helper `0x100466AF`:

- Get/Track/Untrack public selector is string-only
- case-insensitive player/target/mouseover/pet/party1..4/raid1..40
- token resolver `0x00515940` returns object pointer
- live GUID read from object `+0x30/+0x34`
- non-token path: optional `0x`, 1..16 hex digits, surrounding spaces/tabs, nonzero
- errors: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`

UnitState snapshot fast lookup `0x00464870` is now modeled with the binary-confirmed ABI `__stdcall(low32, high32)` in the exact post-overlay.

`UNIT_RESOLVER_UNAVAILABLE` is a separate target string whose owner remains unproven; do not conflate it with `RESOLVE_UNIT_UNAVAILABLE`.

## Spatial S5-R1F1

Public handlers:

- `Spatial.Status 0x1000649D`
- `Spatial.Get 0x10006623`
- `Unit.Distance 0x100077F0`
- `Unit.Behind 0x10007AD8`

Confirmed behavior:

- selectors are string-only
- token resolver `0x00515940`; non-token input uses optional-`0x`, 1..16-digit hex GUID parser
- Unit.Distance aliases are ASCII case-insensitive via helper `0x100057EE`
- exact RANGED/CHAINS/MELEE formulas target-confirmed
- reach helper `object+0x08 -> descriptor+0x204/+0x208`, finite `[0,100]`
- facing `object+0x118 -> movement+0x1c`, finite `[-100,100]`
- eight-iteration Newton sqrt path
- Behind validates facing before distance handling
- XY distance `<=0.0001` succeeds false/dot0
- otherwise S5-R1F1 calibrated score is `(actor.x-target.x)/distance2d`; targetFacing remains diagnostic/output only

### Literal GUID object lookup

Target helper `0x1000BB5D` is now recovered more completely:

1. call `0x00464870` using `__stdcall(low32, high32)`;
2. validate returned object range/GUID;
3. on miss/reject, bounded explicit ObjectManager walk using:
   - global manager pointer `0x00B41414`
   - head `manager+0xAC`
   - next-link base offset `manager+0xA4`
   - next `*(current + nextBase + 4)`
   - max 4096 candidates
   - stop on zero/cycle/bad link

This is explicit-query fallback, not background ObjectManager polling. Recovery implementation is carried by `post_overlay_exact.py` after the main overlay.

S5-R2 ~105° remains unfinished observation only; no threshold is implemented.

## Build / CI

Build transformation order:

1. authentic API33 source base
2. `recovered/api37-s5r1f1/apply_overlay.py`
3. `recovered/api37-s5r1f1/post_overlay_exact.py`
4. existing clang-cl/lld-link build

Both Python overlay scripts pass CI syntax validation. GitHub Actions still fails before overlay/build because `recovery/archive.parts` is a truncated XZ stream. Existing parts must be replaced by one complete self-consistent archive; unrelated chunks cannot be appended.

## Next steps

1. replace/fix the API33 source archive used by CI;
2. full-link API37 with both overlays;
3. rerun callable API `118/118` and dotted strings `129/129`;
4. recalculate PE section delta;
5. inspect remaining binary behavior differences only if static delta justifies it;
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
