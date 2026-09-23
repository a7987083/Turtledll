# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 public contract/counters substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 exact classifier/source/STARTED-CHANGED-READY logic integrated and compile-tested.
- [x] API36 UnitState US1-R2 exact descriptor/status/lifecycle/event behavior integrated and compile-tested.
- [x] API37 Spatial S5-R1F1 range/reach/rear-axis behavior converted to the no-STL/no-default-lib toolchain and compile-tested.
- [x] Overlay now copies latest UnitState + Cooldown + Spatial together.
- [x] First all-latest local API37 combined build succeeded: SHA256 `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`, size `371200` bytes.
- [x] Combined build contains all critical Foundation/Cooldown/UnitState/Spatial API/event strings.
- [x] Broad dotted-string regression matches target exactly: `129` vs `129`, zero missing and zero extra.

## Immediate next work

- [ ] Run the stricter callable-API catalog regression against the known final count `118` rather than relying only on the 129 dotted-string set.
- [ ] Compare target/recovery PE sections and remaining code-size delta (`380928` target vs `371200` recovery; delta `9728` bytes) to identify still-missing behavior rather than padding.
- [ ] Repair/replace truncated `recovery/archive.parts` so GitHub Actions reproduces the successful local combined build.
- [ ] Package the combined DLL as a stage candidate after static regression is complete.

## Runtime validation

- [ ] Real-machine `Foundation.Status` comparison.
- [ ] Real-machine Cooldown STARTED/CHANGED/READY, CLEAR/CHEAT and deadline-source regression.
- [ ] Real-machine UnitState Track/Get/events/world-leave/re-enter regression.
- [ ] Real-machine `Spatial.Get`, `Unit.Distance`, `Unit.Behind` return-shape regression.
- [ ] Keep S5-R2 ~105° only as unfinished experimental observation; it is not a confirmed threshold and must not enter the Spatial core.
