# HANDOFF

## Active recovery branch

`recovery/unitstate-us1r2-exact`

This branch is the current work area for exact API36 UnitState recovery. Do not merge to main until source integration/build verification is complete.

## Baseline

The last source-authentic handoff is `TaiYangShenDian_ARX1_API33_DW1_LOS1_HANDOFF_20260830.zip`.

- API: 33
- exact rebuild SHA256: `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`

## Final target

`taiyangshendian_API37_S5_R1F1.dll`

- Version: `1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1-F1-CD1R2-US1R2-S5R1F1`
- API version: 37
- Build id: `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration`
- SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`
- Size: 380928 bytes

## Current recovered state

### Cooldown CD1-R2

Recovered/confirmed:

- engine query `0x006E2EA0`
- uint32 wrap-safe clock/deadline behavior
- CLEAR single-spell reset and CHEAT all-cooldown reset semantics
- kind mapping: NONE/GCD/SPELL/UNKNOWN
- source mapping 1..7
- STARTED/CHANGED/READY transition state machine
- API33-derived dynamic custom event bridge

Runtime verification is still pending.

### UnitState US1-R2

Binary-confirmed final path:

`GUID -> 0x00464870 -> object -> object+0x08 descriptor -> exact absolute descriptor fields`

Confirmed fields:

- health `+0x58`
- power1..5 `+0x5C..+0x6C`
- maxHealth `+0x70`
- maxPower1..5 `+0x74..+0x84`
- packed power type `+0x90` high byte
- unit flags `+0xB8`, combat mask `0x00080000`
- dynamic flags `+0x23C`, dead bit `0x20`

Confirmed public event payloads:

- `TYS_UNIT_HEALTH_CHANGED(guid, oldHealth, newHealth, maxHealth, dead)`
- `TYS_UNIT_POWER_CHANGED(guid, powerType, oldPower, newPower, maxPower, powerMask)`
- `TYS_UNIT_COMBAT_CHANGED(guid, oldCombat, newCombat)`

`lastChangedMask` is category-level: bit0 HEALTH, bit1 POWER, bit2 COMBAT.

`worldGeneration` lives at `0x100595F4` and advances on `PLAYER_LEAVING_WORLD` reset, not per UPDATE_OBJECT.

Important correction: final `UnitState.Status` does **not** expose `descriptorClears`, `descriptorEmptyPreserves`, `descriptorReconciles`, or `descriptorUnbinds`. See `recovery/API36_UNITSTATE_STATUS_EXACT.md`.

### Spatial S5-R1F1

Range core and rear-axis client geometry have been reconstructed from historical source + final DLL/plugin evidence. Do not encode the experimental ~105° Backstab observation as a global threshold; that test was incomplete and special-target-specific.

## Build/CI

The previous recovered DLL build predates the latest exact-source edits. Current source is not yet rebuilt because the repository CI reconstruction archive remains incomplete/truncated. Do not represent the old recovery DLL as containing the latest UnitState/Cooldown changes.

## Evidence priority

1. final target DLL disassembly
2. user's historical GitHub/source handoffs
3. Aug 25-31 diagnostic plugins
4. Turtle/Tortoise 1.18.1 server source for protocol/server semantics
5. ClassicAPI/SuperWoW/Nampower for 1.12 client internals

External reference code must not override behavior that is directly confirmed by the final DLL.

## Project maintenance rule

After every actual recovery/code/build/CI/validation step, update all five state files:

- `ROADMAP.md`
- `CHANGELOG_DEV.md`
- `HANDOFF.md`
- `PROJECT_STATE.json`
- `KNOWN_ISSUES.md`

Do not describe reconstructed API34-37 C++ as the original lost source.
