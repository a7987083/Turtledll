# KNOWN_ISSUES

## Active blockers

### 1. Latest selector/Spatial corrections are not full-linked yet

The newest overlay now corrects UnitState selector semantics plus Spatial selector/mode/Behind semantics, but the last full DLL predates these changes.

Last full linked candidate:

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous target delta `6656`
- callable APIs `118/118`
- dotted strings `129/129`

Do not claim a newer/final DLL SHA until a complete relink succeeds.

### 2. UnitState selector implementation in stored source is older than final target; overlay carries the correction

Final target `UnitState.Get/Track/Untrack` are string-only and use selector helper `0x100466AF`:

- case-insensitive player/target/mouseover/pet/party1..4/raid1..40
- token object resolver `0x00515940`
- live GUID at object `+0x30/+0x34`
- separate optional-`0x`, 1..16-digit hexadecimal GUID path
- precise `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID` errors

The checked-in `unit_state_core.cpp` still contains the earlier simplified selector implementation; `apply_overlay.py` transforms it before build. This should eventually be folded directly into the stored source once the full source tree is conveniently materialized.

### 3. Spatial GUID object-resolution fallback is not fully recovered

The final pair/selector path is now known to be string-only and token resolution uses `0x00515940`. For literal GUIDs the target helper around `0x1000BB5D` first uses fast GUID lookup `0x00464870` and appears to retain an ObjectManager traversal fallback if fast lookup fails.

The current overlay reproduces the public selector/token/hex contract and direct fast lookup, but not yet that fallback traversal. Do not claim internal parity until this is mapped or shown irrelevant.

### 4. CI source archive is truncated

`recovery/archive.parts` contains only three 12KB base64 pieces and decodes to an incomplete XZ stream. Workflow logs fail at reconstruction with:

```text
xz: Unexpected end of input
```

The workflow now syntax-checks `apply_overlay.py` before reconstruction, and that syntax step passes. The archive itself must be replaced as one self-consistent stream; appending chunks from another compression run would be invalid.

### 5. Previous PE delta is stale

The previous `6656`-byte file-size delta and `.text` delta were measured before the latest selector/Spatial corrections. Recalculate only after a successful full relink. Never pad to match size.

### 6. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but its owning handler/xref is still unproven. It is distinct from the confirmed UnitState selector error `RESOLVE_UNIT_UNAVAILABLE`. Do not add it speculatively.

### 7. Runtime verification remains pending

The latest reconstructed API34-37 behavior has not yet been compared against the original DLL in a live WoW/Turtle client. Cooldown ordering/coalescing, UnitState selector/lifecycle/event timing, and Spatial selector/mode/Behind behavior still require runtime regression.

## Resolved / reduced issues

- Exact UnitState descriptor snapshot path and event/lifecycle behavior are binary-confirmed.
- UnitState selector target contract has now been rechecked and overlay-corrected.
- Strict callable API regression previously reached `118/118`, missing `0`, extra `0`.
- Broad dotted strings previously reached `129/129`, missing `0`, extra `0`.
- Exact final API37 build id restored.
- Spatial public Status/Get/Distance/Behind surfaces are disassembly-mapped.
- Unit.Distance exact mode aliases and ASCII case-insensitive matching recovered.
- Ranged/chains/melee formulas independently target-confirmed.
- Spatial reach path corrected to final `object+0x08 -> descriptor+0x204/+0x208`.
- Spatial radius/reach bounds `[0,100]`, facing bounds `[-100,100]`, Newton sqrt path recovered.
- Final S5-R1F1 degenerate XY Behind behavior recovered.
- Final S5-R1F1 calibrated rear score corrected to normalized X delta.
- CI overlay Python syntax validation passes.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
