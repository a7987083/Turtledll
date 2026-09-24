# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 exact surface integrated and compile-tested.
- [x] API36 UnitState US1-R2 descriptor/status/events/lifecycle integrated and compile-tested.
- [x] API37 Spatial S5-R1F1 public Status/Get/Distance/Behind surfaces recovered from final DLL disassembly.
- [x] Spatial exact modes/aliases, return counts, error codes and four-decimal output rounding recovered.
- [x] Spatial final reach helper recovered: `object+0x08 -> descriptor+0x204/+0x208`, values finite and in `[0,100]`.
- [x] Spatial facing validation recovered: `object+0x118 -> movement+0x1c`, finite and in `[-100,100]`.
- [x] Spatial distance normalization aligned to target eight-iteration Newton sqrt path.
- [x] Exact-wrapper/reach/sqrt Spatial source compiles with the existing i686 clang-cl no-STL/no-default-lib flags.
- [x] Latest UnitState + Cooldown + prior Spatial version compiled together successfully.
- [x] Strict callable API regression: target `118`, recovery `118`, missing `0`, extra `0`.
- [x] Broad dotted-string regression: target `129`, recovery `129`, missing `0`, extra `0`.
- [x] Exact final API37 build id restored through `apply_overlay.py`.
- [x] Last full linked candidate SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272` bytes.

## Immediate next work

- [ ] Full relink with the newly exact Spatial wrapper/reach/sqrt source; assign a new candidate SHA only after link succeeds.
- [ ] Re-run strict `118/118` callable API and `129/129` dotted-string regression on that new DLL.
- [ ] Recalculate section deltas against target and continue function-level Spatial helper comparison; never pad.
- [ ] Treat `UNIT_RESOLVER_UNAVAILABLE` cautiously: owner/xref remains unproven.
- [ ] Repair truncated `recovery/archive.parts` so GitHub Actions reproduces the local build.

## Runtime validation

- [ ] Foundation.Status comparison.
- [ ] Cooldown events/reset/deadline/world-leave regression.
- [ ] UnitState selector/Get/Track/Untrack/Clear/events/world-leave regression.
- [ ] Spatial.Get exact table/value/error regression.
- [ ] Unit.Distance mode/alias/return-code regression.
- [ ] Unit.Behind return tuple/facing regression.
- [ ] S5-R2 ~105° remains unfinished experimental observation only; never encode it as a threshold.
