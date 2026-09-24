# KNOWN_ISSUES

## Active blockers

### 1. Latest exact Spatial behavior is not yet full-linked

The newest Spatial recovery now includes exact public wrappers, final descriptor reach layout, Newton sqrt normalization, facing validation and the disassembly-confirmed S5-R1F1 calibrated rear-axis score.

The last full linked DLL still predates the newest rear-axis correction. Do not claim a new final candidate SHA until a complete API37 relink succeeds.

Last full candidate:

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous delta to target `6656`
- callable APIs `118/118`
- dotted strings `129/129`

### 2. Rear-axis source file still relies on an overlay-time correction

The final target behind helper at `0x1000B7CB` computes `behindDot=(actor.x-target.x)/distance2d` after validating targetFacing, and `behind = dot > 0`. `targetFacing` is output/diagnostic only in this build.

`apply_overlay.py` now patches this exact behavior before compilation. The standalone `spatial_core.cpp` should eventually be rewritten directly so the overlay no longer carries this corrective transform.

### 3. Remaining code delta must be recalculated after relink

The previous remaining difference was overwhelmingly `.text`. Exact Spatial wrappers/helpers/rear-axis behavior have changed materially, so the old section delta is stale until the new full DLL is linked and measured.

Never pad to match binary size.

### 4. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but its owning handler/xref is still unproven. Do not add it speculatively.

### 5. GitHub Actions source reconstruction remains broken

`recovery/archive.parts` contains only the first three 12KB base64 parts and is incomplete/truncated, so the current workflow cannot reproduce the local source base. Local clang-cl/lld-link builds remain authoritative for compile validation.

### 6. Runtime verification remains pending

The latest reconstructed API34-37 behavior has not yet been tested against the original DLL in a live WoW/Turtle client. Cooldown ordering/coalescing, UnitState lifecycle/event timing, and Spatial exact wrapper/calibration behavior still need runtime regression.

## Resolved / reduced issues

- Exact UnitState selector helper is integrated and locally compile-verified.
- Latest Cooldown + UnitState + earlier Spatial compile simultaneously.
- Strict callable API regression previously reached `118/118`, missing `0`, extra `0`.
- Broad dotted strings previously reached `129/129`, missing `0`, extra `0`.
- Exact final API37 build id restored.
- Spatial public Status/Get/Distance/Behind surfaces are disassembly-mapped.
- Unit.Distance exact modes/aliases/return codes recovered.
- Unit.Behind exact four-return tuple recovered.
- Spatial reach path corrected to final `object+0x08 -> descriptor+0x204/+0x208`.
- Spatial radius/reach bounds `[0,100]` and facing bounds `[-100,100]` recovered.
- Spatial geometry normalization corrected to target eight-iteration Newton sqrt.
- S5-R1F1 rear-axis score corrected from conventional facing-vector dot to final normalized-X calibration.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
