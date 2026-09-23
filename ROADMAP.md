# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 exact surface integrated and compile-tested.
- [x] API36 UnitState US1-R2 descriptor/status/events/lifecycle integrated and compile-tested.
- [x] API37 Spatial S5-R1F1 range/reach/rear-axis behavior integrated and compile-tested.
- [x] Latest UnitState + Cooldown + Spatial compile together successfully.
- [x] Strict callable API regression: target `118`, recovery `118`, missing `0`, extra `0`.
- [x] Broad dotted-string regression: target `129`, recovery `129`, missing `0`, extra `0`.
- [x] UnitState.Get/Untrack/Clear public surfaces aligned to final DLL.
- [x] Cooldown and UnitState success statuses and world-leave lifecycle aligned.
- [x] UnitState selector helper recovered from final DLL: case-insensitive player/target/mouseover/pet, party1..4, raid1..40 via client resolver `0x00515940`; GUID text has separate 64-bit hex path and exact resolver/not-found/invalid error branches.
- [x] Exact final API37 build id restored through `apply_overlay.py` and locally rebuilt.
- [x] Meaningful `.rdata` delta reduced from `0x50` to `0x0c` without padding.
- [x] Current build-id-aligned local candidate SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272` bytes.

## Immediate next work

- [ ] Treat `UNIT_RESOLVER_UNAVAILABLE` cautiously: direct/suffix absolute-address scan found no code xref in the final target; do not invent ownership unless another reference form is proven.
- [ ] Inspect remaining PE delta: target `380928` vs recovery `374272` = `6656` bytes. Remaining section delta is overwhelmingly `.text` (`0x489c0` vs `0x47148`); `.rdata` is now `0xa4fb` vs `0xa4ef`.
- [ ] Use API36→API37 stage delta to focus on real Spatial implementation/wrapper differences instead of padding.
- [ ] Repair truncated `recovery/archive.parts` so GitHub Actions reproduces the local build.

## Runtime validation

- [ ] Foundation.Status comparison.
- [ ] Cooldown events/reset/deadline/world-leave regression.
- [ ] UnitState selector/Get/Track/Untrack/Clear/events/world-leave regression.
- [ ] Spatial.Get / Unit.Distance / Unit.Behind regression.
- [ ] S5-R2 ~105° remains unfinished experimental observation only; never encode it as a threshold.
