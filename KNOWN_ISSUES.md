# KNOWN_ISSUES

## Active blockers

### 1. Latest exact Spatial source is not yet full-linked

The newest Spatial source now matches the final target public Status/Get/Distance/Behind surfaces much more closely and corrects reach/facing/sqrt helper behavior. It compiles successfully as an object under the existing recovery toolchain.

However, the last full linked DLL still predates these newest Spatial helper corrections. Do not claim a new final candidate SHA until a complete API37 relink succeeds.

Last full candidate:

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous delta to target `6656`
- callable APIs `118/118`
- dotted strings `129/129`

### 2. Remaining code delta must be recalculated after relink

The previous remaining difference was overwhelmingly `.text`. Exact Spatial wrappers and helpers have now changed materially, so the old section delta must not be treated as current until the new full DLL is linked and measured.

Never pad to match binary size.

### 3. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but its owning handler/xref is still unproven. Do not add it speculatively.

### 4. GitHub Actions source reconstruction remains broken

`recovery/archive.parts` is incomplete/truncated, so the current workflow cannot reproduce the local source base. Local clang-cl/lld-link builds remain authoritative for compile validation.

### 5. Runtime verification remains pending

The latest reconstructed API34-37 behavior has not yet been tested against the original DLL in a live WoW/Turtle client. Cooldown ordering/coalescing, UnitState lifecycle/event timing, and Spatial exact wrapper values/errors still need runtime regression.

## Resolved / reduced issues

- Exact UnitState selector helper is integrated and locally compile-verified.
- Latest Cooldown + UnitState + earlier Spatial compile simultaneously.
- Strict callable API regression previously reached `118/118`, missing `0`, extra `0`.
- Broad dotted strings previously reached `129/129`, missing `0`, extra `0`.
- Exact final API37 build id restored.
- Spatial public Status/Get/Distance/Behind surfaces are now disassembly-mapped.
- Unit.Distance exact modes/aliases/return codes recovered.
- Unit.Behind exact four-return tuple recovered.
- Spatial reach path corrected to final `object+0x08 -> descriptor+0x204/+0x208`.
- Spatial radius/reach bounds `[0,100]` and facing bounds `[-100,100]` recovered.
- Spatial geometry normalization changed from approximate reconstruction x87 `fsqrt` to target eight-iteration Newton sqrt.
- Exact Spatial source object compile succeeded; SHA256 `fd7ecdd871e5dbbef58eab16cb88d5aa4a531296a3690b1183697b447e673554`.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
