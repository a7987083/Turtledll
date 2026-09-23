# CHANGELOG_DEV

## 2026-09-23

### UnitState US1-R2 exact recovery

- Created/continued branch `recovery/unitstate-us1r2-exact`.
- Materialized and rechecked the final target DLL SHA lineage (`1d170507...`).
- Confirmed final UnitState world-tick reconcile at `0x100450AF` and snapshot helper at `0x100469FE`.
- Corrected descriptor access from historical `object+0x110 -> UnitFields` approximation to final-target `object+0x08 -> descriptor`.
- Confirmed descriptor absolute offsets for health, active power lanes, max health/power, packed power type, unit flags, and dynamic flags.
- Identified `+0x23C` as `UNIT_DYNAMIC_FLAGS`; dead policy includes `UNIT_DYNFLAG_DEAD (0x20)` in addition to zero health.
- Confirmed `lastChangedMask`: bit0 HEALTH, bit1 POWER, bit2 COMBAT.
- Confirmed Track/Untrack handlers and 128-record capacity.
- Confirmed `worldGeneration` at `0x100595F4`, advanced by the PLAYER_LEAVING_WORLD reset path.
- Mapped exact UnitState Status counter globals:
  - recordsChecked `0x1021B750`
  - objectUnavailable `0x1021B754`
  - descriptorFailures `0x1021B758`
  - healthEvents `0x1021B75C`
  - powerEvents `0x1021B760`
  - combatEvents `0x1021B764`
  - trackCalls `0x1021B768`
  - untrackCalls `0x1021B76C`
  - capacityFailures `0x1021B770`
- Corrected a prior recovery assumption: final `UnitState.Status` does not expose descriptorClears/descriptorEmptyPreserves/descriptorReconciles/descriptorUnbinds.
- Added `recovery/API36_UNITSTATE_STATUS_EXACT.md`.
- Continued cross-checking against Turtle/Tortoise 1.18.1 update-field definitions and historical UnitXP/API33 infrastructure.

### Maintenance

- Established the required five-file maintenance set for this long-running recovery project:
  - ROADMAP.md
  - CHANGELOG_DEV.md
  - HANDOFF.md
  - PROJECT_STATE.json
  - KNOWN_ISSUES.md

## 2026-09-22

- Recovered API37 Spatial historical range formulas and S5-R1F1 rear-axis geometry.
- Recovered UnitState active power descriptor semantics and API33-derived custom event bridge.
- Recovered Cooldown packet reset layouts, wrap-safe timing, kind/source classifier and transition helper/state machine.
