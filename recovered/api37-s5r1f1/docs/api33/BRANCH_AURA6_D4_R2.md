# Branch — AURA6-D4-R2 AuraSourceCore

Branch: `dev/aura6-d4-r2-aura-source`
Parent: `c2d02b7` (AURA6-D4-R1 NativeBus real-machine PASS)

## Purpose
Replace the temporary D1/D3 `CasterCore + Pending + Binding + OwnerMemory` model with one native `AuraSourceCore` instance cache.

## Removed from runtime
- `src/aura_caster_core.*`
- newest-pending seating
- `usedSlotMask`
- separate OwnerMemory table / restore path
- read-side Pending consumption

## Added
- `src/aura_source_core.*`
- `(targetGuid, spellId, casterGuid)` identity
- FIFO one-shot seating
- bound-slot protection on STACK
- retained-instance handling for descriptor visibility teardown
- `Aura.Source.*` API27 names

## Explicitly not in R2
- target PREDICTED duration
- combo-point duration capture
- server-specific duration modifier rules
- modern `C_UnitAuras` facade

Those belong to later stages after R2 real-machine validation.
