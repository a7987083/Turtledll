# API36 US1-R2 change mask + Track/Untrack evidence

Target SHA-256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Exact public handlers

Dispatcher string XREFs in the final API37 DLL resolve:

```text
UnitState.Track   -> 0x1004715F
UnitState.Untrack -> 0x10047589
UnitState.List    -> 0x100476DD
UnitState.Clear   -> 0x1004775C
```

`Track` increments the global track-call counter at `0x1021B768`; `Untrack` increments `0x1021B76C`.

The tracked-record stride is `0x58`, total storage `0x2C00`, therefore the final target has exactly 128 slots (`0x2C00 / 0x58 = 128`).

`Track` first resolves the Lua argument to a GUID, returns an existing slot when present, otherwise locates a free 0x58-byte record, zeroes it, marks it used, stores GUID low/high, and increments the tracked count. The capacity-failure counter is `0x1021B770`.

`Untrack` resolves the GUID, locates the matching used record, zeroes the complete 0x58-byte record, decrements tracked count when nonzero, and returns the success path. This is explicit untracking; it is distinct from descriptor/object disappearance during reconcile.

## Exact `lastChangedMask`

The earlier scaffold mixed the detailed power-change mask into `lastChangedMask`. Final-binary disassembly at `0x100451A2..0x1004524F` proves that is incorrect.

There are **two different masks**:

### Power event payload mask

Used only by `TYS_UNIT_POWER_CHANGED`:

```text
bit0 (0x01) active power type changed
bit1 (0x02) active power value changed
bit2 (0x04) active max-power changed
```

This remains the frozen `BIT0_TYPE_BIT1_VALUE_BIT2_MAX` contract.

### Aggregate `lastChangedMask`

The status-level `lastChangedMask` is an independent aggregate category mask:

```text
bit0 (0x01) HEALTH category changed
bit1 (0x02) POWER category changed (any power payload bit)
bit2 (0x04) COMBAT category changed
```

Pseudo-code matching the final binary:

```cpp
uint32_t aggregate = healthChanged ? 0x01u : 0u;
if (powerPayloadMask != 0)
    aggregate |= 0x02u;
if (combatChanged)
    aggregate |= 0x04u;

if (aggregate != 0) {
    lastChangedGuid = guid;
    lastChangedMask = aggregate;
}
```

Therefore recovery code must **not** use `0x08/0x10` for health/combat and must **not** OR the detailed power payload bits directly into `lastChangedMask`.

## Descriptor result branches

Snapshot helper `0x100469FE` has a useful three-state contract:

1. Object cannot be resolved / is invalid / GUID mismatches:
   - helper returns success (`AL=1`) with snapshot byte0 = 0 (object not present).
   - caller increments `objectUnavailable` (`0x1021B754`) and clears the cached-live byte.

2. Object is valid, but descriptor pointer is null or descriptor range is unreadable:
   - helper returns failure (`AL=0`).
   - caller increments `descriptorFailures` (`0x1021B758`).
   - it does not manufacture a zero-health snapshot.

3. Object + descriptor valid:
   - byte0 = object present
   - byte1 = descriptor snapshot present
   - caller performs normal old/new reconcile and event comparison.

This is more precise than the earlier scaffold's direct `descriptor == 0 -> descriptorEmptyPreserves++` approximation. The exact higher-level attribution of the historical diagnostic fields `descriptorEmptyPreserves`, `descriptorUnbinds`, `descriptorClears`, and `descriptorReconciles` still needs mapping to their final bookkeeping structure before those counters are declared exact.

## Recovery status

Binary-confirmed now:

- 128-slot tracked storage and 0x58 record stride
- Track/Untrack handlers and call counters
- capacity-failure counter
- exact aggregate `lastChangedMask`
- exact detailed power-event mask
- object-unavailable vs descriptor-failure control flow

Still pending before merging this branch to main:

- map the four descriptor diagnostic bookkeeping fields exactly
- wire PLAYER_LEAVING_WORLD/worldGeneration reset into the reconstructed NativeBus surface
- compile/regression-test the branch
