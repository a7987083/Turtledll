# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 public contract/counters substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 engine query, wrap-safe clock, CLEAR/CHEAT reset semantics, kind/source classifier and STARTED/CHANGED/READY state machine recovered.
- [x] API35 exact Cooldown classifier/source/transition helpers integrated into `cooldown_core.cpp`.
- [x] API35 Cooldown custom events wired through the recovered API33 FrameScript event bridge.
- [x] API35 Cooldown implementation converted to the no-STL/no-default-lib recovery toolchain and compile-tested locally; compile-test DLL SHA256 `828e7c683ad6c623c37f1b38a95fc5dd74e0242bc7946af13254dad863fa3907`.
- [x] API36 UnitState US1-R2 exact descriptor path recovered: `object+0x08 -> descriptor`.
- [x] API36 exact health/power/dead/combat fields and event payloads recovered.
- [x] API36 exact `lastChangedMask`, Track/Untrack/List/Clear handlers, 128-slot capacity and `worldGeneration` leave-world semantics recovered.
- [x] API36 exact `UnitState.Status` public field surface mapped and implemented in recovered source.
- [x] Existing historical `PLAYER_LEAVING_WORLD` lifecycle funnel patched to call `TysUnitStateCore::onWorldLeaving()`; no new hook added.
- [x] UnitState/custom-event recovery sources converted to the no-STL/no-default-lib recovery toolchain.
- [x] Interim local API37 DLL compiled successfully with the exact UnitState overlay; SHA256 `d44302b3227c5a13ae9132db4d1b080512dede81820f32a60aeb0782f4c92099`.
- [x] API37 Spatial S5-R1F1 range/combatReach/boundingRadius/facing core recovered at source/evidence level.
- [x] Final command-string set previously matched with zero missing/extra under the verification rule.

## Immediate next work

- [ ] Integrate/compile-test the latest Spatial S5-R1F1 implementation in the same no-STL/no-default-lib overlay.
- [ ] Produce one full current API37 local build containing latest UnitState + Cooldown + Spatial simultaneously.
- [ ] Re-run API/string/static regression against final target DLL after the full build.
- [ ] Repair or replace the truncated `recovery/archive.parts` CI reconstruction input so GitHub Actions builds the same current source.
- [ ] Continue using historical project branches first; use Turtle/Tortoise 1.18.1 only as a protocol/server-semantics cross-check where original client-side evidence is missing.

## Runtime validation

- [ ] Real-machine `Foundation.Status` field-by-field comparison.
- [ ] Real-machine `Cooldown.Get/List` timing, STARTED/CHANGED/READY, CLEAR/CHEAT reset and deadline-source regression.
- [ ] Real-machine UnitState Track/Get/events/world-leave/re-enter regression against original API36/API37 DLL.
- [ ] Verify `Spatial.Get`, `Unit.Distance`, `Unit.Behind` return shapes against final API37 DLL.
- [ ] Continue Backstab S5-R2 special-target calibration; experimental ~105° observations are not a confirmed threshold and must not enter the Spatial core.
