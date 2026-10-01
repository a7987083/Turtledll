# AURA6-D4-R4 / API 29

- DLL: `1.4.0-AURA6D4-R4`
- API: `29`
- Build: `20260829-v140-aura6d4-r4-final-lifecycle`
- Parent stable R3: `1653e72ef09bc272cd6d9c373e2afbbe89603b33`

## Scope
R4 is the final Aura lifecycle hardening stage. It does not add a second Aura cache and does not add modern `C_UnitAuras` compatibility.

### Refresh lifecycle
A repeat `(targetGuid, spellId, casterGuid)` cast updates the existing AuraInstance and writes one short-lived `refreshOnly` FIFO candidate. That candidate may only move/confirm the already-existing identity; it can never resurrect an Aura that was removed before the callback arrives.

This covers:
- same-slot refresh;
- refresh that moves to a new rawSlot;
- multiple casters using the same SpellID;
- delayed REMOVE for the old rawSlot after a refresh/reseat.

### Timing on refresh
If fresh duration evidence exists, the existing PREDICTED timer is replaced. If the refresh is observed but duration evidence is unavailable, the old prediction is cleared to `UNKNOWN` rather than carrying stale time forward.

### World boundary
`PLAYER_LEAVING_WORLD` clears transient Aura source state:
- AuraSourceCore PendingApplication entries;
- AuraSourceCore AuraInstance entries;
- ComboDuration outgoing snapshots;
- AuraState delta metadata.

Hooks/subscriptions remain process-lifetime; the next world starts from an empty transient state.

## API 29 additive status fields
`Aura.Source.Status` adds:
- `refreshPendingWrites`
- `refreshBindings`
- `staleRemoveIgnores`
- `worldResets`

`Aura.State.Status` adds:
- `worldResetCount`

Existing `Aura.Get`, `Aura.List`, `Aura.Source.Match`, NativeBus and R3 timing fields remain compatible.

## Invariants retained
- UnitFields is the sole authority for Aura presence.
- AuraSourceCore is the sole caster/target-timing instance store.
- Query paths never consume Pending.
- No Aura OnUpdate scan, ObjectManager polling, timer thread, or periodic Aura scan.
- No Nampower/provider arbitration path.
- No second TargetDuration store.
