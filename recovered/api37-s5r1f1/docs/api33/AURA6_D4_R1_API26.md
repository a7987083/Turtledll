# AURA6-D4-R1 API26

## Version

- DLL: `1.4.0-AURA6D4-R1`
- API: `26`
- Build: `20260829-v140-aura6d4-r1-native-bus`

## Added

### `Aura.NativeBus.Status`

Read-only diagnostic table for the shared incoming/outgoing/world-tick funnels.
No polling is started by this API.

## Changed

### `Aura.Caster.Status`

Native-only. Caster evidence is sourced from `SMSG_SPELL_GO` observed at the
shared packet-dispatch funnel. There is no per-opcode SPELL_GO hook.

## Removed

The following compatibility ingress APIs no longer exist:

- `Aura.Caster.External.Cast`
- `Aura.Caster.External.Aura`

No external DLL/provider is detected or selected by the Aura runtime.

## Unchanged from AURA6-D3

- `Aura.Status`
- `Aura.Diagnostics`
- `Aura.Snapshot`
- `Aura.Duration.Status`
- `Aura.Duration.Snapshot`
- `Aura.Caster.Match`
- `Aura.Caster.Snapshot`
- `Aura.State.Status`
- `Aura.Get`
- `Aura.List`

Target duration remains `UNKNOWN` in the formal Aura state API in R1.
