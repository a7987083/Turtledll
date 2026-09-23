# ROADMAP

## Current recovery baseline

- [x] API33 source-authentic baseline recovered and exact rebuild proven.
- [x] API34 Foundation F1 public contract/counters substantially disassembly-confirmed.
- [x] API35 Cooldown CD1-R2 engine query, wrap-safe clock, reset semantics, kind/source classifier and STARTED/CHANGED/READY state machine recovered.
- [x] API36 UnitState US1-R2 exact descriptor path recovered: `object+0x08 -> descriptor`.
- [x] API36 exact health/power/dead/combat fields and event payloads recovered.
- [x] API36 exact `lastChangedMask`, Track/Untrack/List/Clear handlers, 128-slot capacity and `worldGeneration` leave-world semantics recovered.
- [x] API36 exact `UnitState.Status` public field surface mapped from final DLL.
- [x] API37 Spatial S5-R1F1 range/combatReach/boundingRadius/facing core recovered.
- [x] Final command-string set previously matched with zero missing/extra under the verification rule.

## Immediate next work

- [ ] Align recovered `unit_state_core.cpp` Status output exactly to the final DLL; remove invented public descriptor-counter fields.
- [ ] Restore the `PLAYER_LEAVING_WORLD` lifecycle funnel using the existing historical NativeBus/event infrastructure; do not add a new hook.
- [ ] Verify `reconcilePending`, `architecture`, `authority`, `compressedPolicy`, and event-slot Status values against final DLL.
- [ ] Integrate exact Cooldown classifier/transition helpers into `cooldown_core.cpp` rather than leaving them as standalone helpers.
- [ ] Rebuild the current recovery source after CI source-archive reconstruction is repaired.

## Runtime validation

- [ ] Real-machine `Foundation.Status` field-by-field comparison.
- [ ] Real-machine `Cooldown.Get/List` timing, CLEAR/CHEAT reset and deadline transition regression.
- [ ] Real-machine UnitState Track/Get/events/world-leave/re-enter regression against original API36/API37 DLL.
- [ ] Verify `Spatial.Get`, `Unit.Distance`, `Unit.Behind` return shapes against final API37 DLL.
- [ ] Continue Backstab S5-R2 special-target calibration; experimental ~105° observations are not a confirmed threshold and must not enter the Spatial core.
