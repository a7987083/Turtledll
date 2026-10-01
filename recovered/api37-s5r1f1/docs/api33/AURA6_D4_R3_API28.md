# AURA6-D4-R3 / API 28

## Version
- DLL: `1.4.0-AURA6D4-R3`
- API: `28`
- Build: `20260829-v140-aura6d4-r3-target-duration`
- Parent stable baseline: AURA6-D4-R2 / API27

## R3 scope
R3 directly promotes observed target Aura timing into the existing unified `Aura.Get` / `Aura.List` rows. It does not create a second TargetDuration cache.

### Time quality
- Player Aura with live client BuffBar expiration: `EXACT`
- Target Aura applied by the local player, ordinary duration: `PREDICTED / LOCAL_CAST_MODIFIED`
- Target combo-duration Aura applied by the local player: `PREDICTED / LOCAL_COMBO_SCALED`
- Target Aura applied by another caster when a finite base duration is available: `PREDICTED / REMOTE_BASE`
- Application not observed / no finite duration / elapsed estimate while UnitFields still says present: `UNKNOWN`

### Combo-duration path
`CMSG_CAST_SPELL` is observed at the existing NativeBus send funnel. Combo points are captured before the server consumes them. The matching local `SMSG_SPELL_GO` consumes that capture and applies the 1.12 SpellDuration combo interpolation, then local duration SpellMods.

### Presence authority
UnitFields remains authoritative for Aura existence. A predicted expiration reaching zero never removes an Aura. If the Aura still exists in UnitFields, only the timing quality degrades to `UNKNOWN`.

## Additive Aura row field
R3 adds `name` to `Aura.Get` / `Aura.List` rows using the localized Spell.dbc name. Existing fields remain compatible.

## Existing commands used
- `TaiYangShenDian("Aura.Get", unit, rawSlot)`
- `TaiYangShenDian("Aura.List", unit)`
- `TaiYangShenDian("Aura.State.Status")`
- `TaiYangShenDian("Aura.Source.Status")`
- `TaiYangShenDian("Aura.NativeBus.Status")`

No separate `Aura.TargetDuration.*` API is introduced.
