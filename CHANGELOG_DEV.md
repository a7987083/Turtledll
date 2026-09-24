# CHANGELOG_DEV

## 2026-09-24

### Fast GUID ABI + ObjectManager fallback recovery

- Rechecked final target helper `0x1000BB5D` and corrected the recovery ABI for client fast GUID lookup `0x00464870`.
- Target pushes GUID high then GUID low and the callee returns with `ret 8`; recovery now models the function as `__stdcall(low32, high32)`, not `__fastcall(uint64)`.
- The same ABI correction is applied to UnitState snapshot resolution.
- Recovered the target's bounded explicit ObjectManager fallback used when Spatial literal-GUID fast lookup misses/rejects:
  - global ObjectManager pointer `0x00B41414`
  - head `manager+0xAC`
  - next-link base offset `manager+0xA4`
  - next object `*(current + nextBase + 4)`
  - exact GUID check at `object+0x30/+0x34`
  - object-range validation through `0x38`
  - maximum 4096 candidates and cycle/zero/bad-link termination
- This is an explicit-query fallback and does not contradict `objectManagerScan=false` / no background polling.
- Added `recovered/api37-s5r1f1/post_overlay_exact.py`; workflow now executes it after `apply_overlay.py`.
- Both overlay scripts pass GitHub Actions Python syntax validation.
- Updated Spatial evidence documentation to record fast lookup ABI, fallback layout and call graph.

### Selector + Spatial fidelity correction

- Final `UnitState.Get`, `Track`, and `Untrack` require argument 2 to be a Lua string; numeric selectors are not accepted.
- Unit tokens are case-insensitive `player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40` and use client object resolver `0x00515940`.
- Non-token selector is optional `0x`, 1..16 hexadecimal digits, surrounding spaces/tabs only, nonzero.
- Exact UnitState selector errors preserved: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`.
- Spatial public selectors are string-only with the same token/hex split.
- Unit.Distance mode matching helper `0x100057EE` is ASCII case-insensitive.
- Final ranged/chains/melee formulas independently confirmed from API37 target helper/call sites.
- Final Behind order corrected: targetFacing validation occurs before degenerate-distance handling; `distance2d <= 0.0001` succeeds with `behind=false`, dot `0`.
- Final S5-R1F1 calibrated score is normalized X delta and remains client calibration only.

### Full-build status

- Last full linked candidate SHA256 remains `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272`.
- It predates the newest selector/Spatial/fast-GUID corrections; no newer DLL SHA is claimed yet.
- Previous static regression reached callable APIs `118/118` and dotted strings `129/129`.
- GitHub Actions reaches and passes overlay syntax validation, then still fails before overlay/build because `recovery/archive.parts` is an incomplete XZ stream (`xz: Unexpected end of input`).

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
