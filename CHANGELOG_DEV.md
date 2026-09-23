# CHANGELOG_DEV

## 2026-09-23

### Cooldown CD1-R2 exact integration

- Integrated the previously disassembly-confirmed kind/source classifier and STARTED/CHANGED/READY transition state machine into `recovered/api37-s5r1f1/src/cooldown_core.cpp`.
- Added source-preserving dirty reconciliation: packet causes 1..5 are not overwritten by later deadline/explicit rechecks.
- Added local-player filtering for `SMSG_SPELL_GO`, `SMSG_SPELL_COOLDOWN`, `SMSG_COOLDOWN_EVENT`, `SMSG_CLEAR_COOLDOWN`, and `SMSG_COOLDOWN_CHEAT`.
- Restored SpellRec-based classification fields using the existing client Spell DB view: RecoveryTime `+0x4C`, CategoryRecoveryTime `+0x50`, StartRecoveryCategory `+0x274`, StartRecoveryTime `+0x278`.
- Wired `TYS_COOLDOWN_STARTED`, `TYS_COOLDOWN_CHANGED`, and `TYS_COOLDOWN_READY` through the recovered API33 FrameScript custom-event bridge.
- Restored CLEAR/CHEAT diagnostic transitions including ready and SPELL->GCD counters.
- Converted `cooldown_classifier.h`, `cooldown_transition.h`, and the integrated Cooldown core to the existing no-STL/no-default-lib recovery toolchain.
- Local compile test succeeded. Compile-test DLL SHA256: `828e7c683ad6c623c37f1b38a95fc5dd74e0242bc7946af13254dad863fa3907`.
- Cross-checked server-side reset semantics against `tortoise-wow/tortoise-wow` 1.18.1 source: the server distinguishes `RemoveSpellCooldown` from `RemoveAllSpellCooldown`; this is supporting protocol evidence only, not a replacement for final DLL behavior.

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

- Maintained the required five-file project state set:
  - ROADMAP.md
  - CHANGELOG_DEV.md
  - HANDOFF.md
  - PROJECT_STATE.json
  - KNOWN_ISSUES.md

## 2026-09-22

- Recovered API37 Spatial historical range formulas and S5-R1F1 rear-axis geometry.
- Recovered UnitState active power descriptor semantics and API33-derived custom event bridge.
- Recovered Cooldown packet reset layouts, wrap-safe timing, kind/source classifier and transition helper/state machine.
