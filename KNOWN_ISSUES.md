# KNOWN_ISSUES

## Active blockers

### 1. Latest corrections are not full-linked yet

The latest overlays now include UnitState selector semantics, Spatial selector/mode/Behind semantics, binary-confirmed fast GUID ABI, and the Spatial explicit ObjectManager fallback. The last full DLL predates these changes.

Last full linked candidate:

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous target delta `6656` (stale)
- callable APIs `118/118`
- dotted strings `129/129`

Do not claim a newer/final DLL SHA until complete relink succeeds.

### 2. Stored UnitState/Spatial core source is older than final target; overlays carry exact corrections

`apply_overlay.py` and `post_overlay_exact.py` currently transform the stored reconstructed sources before compilation. This is auditable and compile-pipeline safe, but eventually folding the verified corrections directly into the stored core source files would simplify maintenance.

Important transformed behaviors include:

- UnitState string-only selector + precise errors
- token resolver `0x00515940`
- `0x00464870` fast GUID ABI `__stdcall(low32,high32)`
- Spatial literal-GUID bounded ObjectManager fallback
- Unit.Distance case-insensitive modes
- S5-R1F1 degenerate XY Behind handling and normalized-X calibration

### 3. CI source archive is truncated

`recovery/archive.parts` contains only three 12KB base64 pieces and decodes to an incomplete XZ stream. Workflow logs fail at reconstruction with:

```text
xz: Unexpected end of input
```

Both overlay Python scripts are syntax-checked before reconstruction and pass. The archive itself must be replaced as one complete self-consistent stream; appending chunks from another compression run is invalid.

A complete authentic API33 source archive is available locally for replacement, but has not yet been fully written back into the GitHub split archive.

### 4. Previous PE delta is stale

The previous `6656`-byte file-size delta and section comparison were measured before the latest selector/Spatial/fast-GUID/fallback corrections. Recalculate only after successful full relink. Never pad to match binary size.

### 5. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but its owning handler/xref is still unproven. It is distinct from confirmed UnitState selector error `RESOLVE_UNIT_UNAVAILABLE`. Do not add it speculatively.

### 6. Runtime verification remains pending

The latest reconstructed API34-37 behavior has not yet been compared against the original DLL in a live WoW/Turtle client. Cooldown ordering/coalescing, UnitState selector/lifecycle/event timing, and Spatial selector/mode/Behind/GUID-fallback behavior still require runtime regression.

## Resolved / reduced issues

- Exact UnitState descriptor snapshot path and event/lifecycle behavior are binary-confirmed.
- UnitState selector final contract is overlay-corrected.
- Strict callable API regression previously reached `118/118`, missing `0`, extra `0`.
- Broad dotted strings previously reached `129/129`, missing `0`, extra `0`.
- Exact final API37 build id restored.
- Spatial public Status/Get/Distance/Behind surfaces are disassembly-mapped.
- Unit.Distance exact mode aliases and ASCII case-insensitive matching recovered.
- Ranged/chains/melee formulas independently target-confirmed.
- Spatial reach path corrected to final `object+0x08 -> descriptor+0x204/+0x208`.
- Spatial radius/reach bounds `[0,100]`, facing bounds `[-100,100]`, Newton sqrt recovered.
- Final S5-R1F1 degenerate XY Behind behavior recovered.
- Final S5-R1F1 calibrated rear score corrected to normalized X delta.
- Fast GUID lookup `0x00464870` ABI corrected to `__stdcall(low32,high32)`.
- Spatial target ObjectManager fallback recovered: `0x00B41414`, `+0xAC` head, `+0xA4` next-base, 4096 bound.
- CI overlay Python syntax validation passes for both overlay scripts.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
