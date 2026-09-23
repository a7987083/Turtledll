# API36 UnitState US1-R2 lifecycle / counter disassembly notes

Target: `taiyangshendian_API37_S5_R1F1.dll`
SHA-256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Reconcile loop branch semantics

World-tick reconcile: `0x100450AF`.
Snapshot helper: `0x100469FE`.

The call site distinguishes three states rather than treating the snapshot helper as a simple `object present` boolean:

1. helper returns false -> `descriptorFailures++` (`0x1021B758`).
2. helper returns true and snapshot object-present byte is false -> `objectUnavailable++` (`0x1021B754`) and the cached record live/valid byte is cleared.
3. helper returns true and object + descriptor-present bytes are true -> snapshot is authoritative and normal compare/event reconciliation proceeds.

The `objectUnavailable` path is therefore not a descriptor read failure. It is the authoritative no-live-object result returned by the snapshot helper.

## Change category mask

The main compare block proves that `UnitState.Status.lastChangedMask` is a category mask:

```text
0x01 health/dead contract changed
0x02 active-power contract changed
0x04 combat boolean changed
```

Assembly pattern in `0x100451A2..0x1004524F`:

- health/dead comparison seeds `eax` as 0 or 1;
- non-zero detailed power mask ORs `eax` with `0x02`;
- combat transition adds/ORs category bit `0x04`.

The power event payload still carries its separate detailed mask:

```text
0x01 powerType
0x02 power value
0x04 maxPower
```

These two masks must not be conflated.

## Event counters

After `SignalEventParam` succeeds, the final DLL increments:

```text
0x1021B75C healthEvents
0x1021B760 powerEvents
0x1021B764 combatEvents
```

This confirms counters represent actual public event emission paths, not merely detected changes.

## PLAYER_LEAVING_WORLD / worldGeneration

Central FrameScript event path: `0x100056B4`.

The `PLAYER_LEAVING_WORLD` string comparison reaches the reset fan-out at `0x10005725..0x10005742`. UnitState reset routine is called at:

```text
0x10005742 -> call 0x100459BE
```

`0x100459BE` iterates all 128 records (stride `0x58`) and clears per-record live/dirty binding state. It then advances global:

```text
worldGeneration = 0x100595F4
```

with non-zero wrap-safe progression. The adjacent `PLAYER_ENTERING_WORLD` branch does not call this UnitState reset routine.

Therefore `worldGeneration` is a world-leave invalidation generation, not an UPDATE_OBJECT generation counter.

## Exact record capacity

Tracked-record storage spans `0x2C00` bytes with record stride `0x58`:

```text
0x2C00 / 0x58 = 128 records
```

This independently confirms the public `capacity = 128` contract.

## Remaining bookkeeping work

The final binary has public counters/keys for `descriptorReconciles`, `descriptorClears`, `descriptorEmptyPreserves`, and `descriptorUnbinds`. Their names are confirmed, but their exact individual increment sites are distributed through common stats/bookkeeping code and still need a final mapping before the recovery core should claim source-level identity for those four counters.

Do not synthesize these four conditions merely from their names. The main state behavior is already confirmed; only diagnostic bookkeeping remains pending.
