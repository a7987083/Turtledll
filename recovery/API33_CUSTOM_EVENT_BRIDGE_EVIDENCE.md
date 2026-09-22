# API33 custom-event bridge evidence

Source basis: authentic API33 handoff `TaiYangShenDian_ARX1_API33_DW1_LOS1_HANDOFF_20260830`.

Confirmed addresses and behavior from the authentic source:

- FrameScript CreateEvents: `0x00703D90`
- FrameScript SetEventCount: `0x007053B0`
- FrameScript event object data pointer: `0x00CEEF68`
- `SignalEventParam`: `0x00703F50`
- `SStrDupA`: `0x0064A620`
- expanded event table size: 700 slots
- event-entry stride: 16 bytes (`data[i * 4]` for the name pointer)
- CAST1-FIX1 dynamically searches for an existing event name first, then claims the first NULL slot from the low end; it never overwrites an occupied event name.
- `SignalEventParam(slot, format, ...)` is the authentic API33 custom-event delivery path used by CAST/Aura.

Recovery consequence:

API35 Cooldown and API36 UnitState do not require a new custom-event subsystem. They can reuse the existing expanded FrameScript table and dynamic low-end slot claim mechanism.

Final diagnostic plugin contracts:

- `TYS_UNIT_HEALTH_CHANGED`: `guid, oldHealth, newHealth, maxHealth, dead`
- `TYS_UNIT_POWER_CHANGED`: `guid, powerType, oldPower, newPower, maxPower, mask`
- `TYS_UNIT_COMBAT_CHANGED`: `guid, oldCombat, newCombat`
- `TYS_COOLDOWN_STARTED/CHANGED/READY`: `spellId, start, duration, end, remaining, enable, kind, source`

Integration status:

- Generic reconstructed bridge: `recovered/api37-s5r1f1/src/custom_event_bridge.{h,cpp}`.
- It preserves the authentic API33 event-name claim strategy and SignalEventParam transport.
- UnitState/Cooldown transition call sites are still to be wired and build-validated.
- US1-R2 post-stock-handler scheduling is a separate question from event delivery and remains under recovery.
