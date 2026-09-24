# CHANGELOG_DEV

## 2026-09-24

### Selector + Spatial fidelity correction

- Rechecked final UnitState selector helper `0x100466AF` and corrected an earlier recovery overclaim.
- Final `UnitState.Get`, `Track`, and `Untrack` require argument 2 to be a Lua string; numeric selectors are not accepted.
- Recognized tokens are case-insensitive `player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40`.
- Recognized tokens call client object resolver `0x00515940`, then read nonzero live GUID from `object+0x30/+0x34`.
- Exact selector errors preserved: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`; helper success status `OK`.
- Non-token GUID syntax is optional `0x`, 1..16 hexadecimal digits, surrounding spaces/tabs only, nonzero.
- Updated `apply_overlay.py` so UnitState handlers preserve the precise final selector errors instead of collapsing failures to `BAD_SELECTOR`.
- Rechecked Spatial pair resolver: public Spatial selectors are also string-only; tokens use `0x00515940`; GUID literals use the separate hex path. Current recovery still needs the target helper's fast-lookup fallback traversal for full internal parity.
- Confirmed Unit.Distance mode matching helper `0x100057EE` is ASCII case-insensitive; overlay updated.
- Confirmed final ranged/chains/melee formulas directly from API37 helper/call-site mapping.
- Corrected final Behind degenerate path: target facing is validated first; `distance2d <= 0.0001` succeeds with `behind=false`, dot `0`.
- Recovery overlay syntax is now checked before archive reconstruction in CI; workflow run 48 reached and passed this syntax-check step.

### Spatial S5-R1F1 exact public-surface/helper recovery

- Public handlers: `Spatial.Status 0x1000649D`, `Spatial.Get 0x10006623`, `Unit.Distance 0x100077F0`, `Unit.Behind 0x10007AD8`.
- Exact reach path is final descriptor `object+0x08 -> +0x204/+0x208`; values finite and `[0,100]`.
- Facing is `object+0x118 -> movement+0x1c`, finite and `[-100,100]`.
- Geometry uses eight Newton sqrt iterations from `max(value,1.0)`.
- S5-R1F1 calibrated rear score is normalized X delta after facing validation. This is a client calibration artifact, not server Backstab truth.
- ~105° remains unfinished S5-R2 observation only and is not implemented as a threshold.

### Full-build status

- Last full linked candidate SHA256 remains `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272`.
- That candidate predates the newest selector/Spatial corrections; no newer DLL SHA is claimed yet.
- Previous strict static regression reached callable APIs `118/118` and dotted strings `129/129`.
- CI still cannot relink because `recovery/archive.parts` is an incomplete XZ stream; the failure occurs before overlay/build.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
