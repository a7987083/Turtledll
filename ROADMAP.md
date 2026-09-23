# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 public contract/counters substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 classifier/source/STARTED-CHANGED-READY logic integrated and compile-tested.
- [x] API36 UnitState US1-R2 descriptor/status/lifecycle/event behavior integrated and compile-tested.
- [x] API37 Spatial S5-R1F1 range/reach/rear-axis behavior converted to the no-STL/no-default-lib toolchain and compile-tested.
- [x] Latest UnitState + Cooldown + Spatial build together successfully.
- [x] Strict callable API regression matches the final target: `118` vs `118`, missing `0`, extra `0`.
- [x] Broad dotted-string regression matches the final target: `129` vs `129`, missing `0`, extra `0`.
- [x] UnitState.Get public table aligned to final DLL (`visible/fieldsValid/tracked/initialized`, flags, active power, power1..5/maxPower1..5, code).
- [x] UnitState.Untrack idempotent `NOT_TRACKED` and UnitState.Clear `true,"CLEARED"` returns aligned.
- [x] Cooldown success status aligned to `READY_NATIVEBUS_ENGINE_QUERY`; UnitState success status aligned to `READY_TRACKED_UPDATEOBJECT_GATE`.
- [x] Cooldown and UnitState now both reset through the existing `PLAYER_LEAVING_WORLD` lifecycle funnel.
- [x] Current exact-surface local candidate: SHA256 `6bd0239cd15e66486c47267f70ef9dc6f31cc70b6878e12f08ffa49cfaabb393`, size `373760` bytes.

## Immediate next work

- [ ] Recover the remaining exact unit-selector helper semantics around client resolver `0x00515940` (player/target/mouseover/pet/partyN/raidN and exact error codes).
- [ ] Identify the owner/xref of remaining target string `UNIT_RESOLVER_UNAVAILABLE`; do not invent behavior.
- [ ] Inspect the remaining target/recovery size delta (`380928` vs `373760`, delta `7168`) for real behavior; never pad.
- [ ] Repair/replace truncated `recovery/archive.parts` so GitHub Actions reproduces the current local build.

## Runtime validation

- [ ] Real-machine Foundation.Status comparison.
- [ ] Real-machine Cooldown STARTED/CHANGED/READY, CLEAR/CHEAT, deadline and world-leave regression.
- [ ] Real-machine UnitState Get/Track/Untrack/Clear/events/world-leave/re-enter regression.
- [ ] Real-machine Spatial.Get / Unit.Distance / Unit.Behind return-shape regression.
- [ ] Keep S5-R2 ~105° only as unfinished experimental observation; it is not a confirmed threshold and must not enter Spatial core.
