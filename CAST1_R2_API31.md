# CAST1-R2 Unified CastState — API 31

## Baseline

- Parent: CAST1-R1-FIX1 (`9500da4371ce04af2441da25277bd7012df8aa44`)
- Aura core remains frozen at AURA6-D4-R4.
- No new packet hook and no ObjectManager polling.
- Transport remains NativeBus + the existing `ClearCastingSpell` choke-point observer.

## Why R2 exists

R1 proved the lifecycle events in the real client, but its single `state` field became `STOPPED` after a successful or interrupted cast. That made read-side consumers lose the terminal reason.

R2 separates two concepts:

- `phase`: what the caster is doing now — `PENDING`, `CASTING`, `CHANNELING`, `IDLE`.
- `result`: durable terminal outcome — `NONE`, `SUCCESS`, `FAILED`, `INTERRUPTED`, `STOPPED`, `EXPIRED`.

A STOP event therefore no longer erases the reason that preceded it.

## New APIs

### `TaiYangShenDian("Cast.State.Status")`

Returns CastState core status and counts:

- `status = READY_UNIFIED_CAST_STATE`
- `authority`
- `phaseModel`
- `resultModel`
- `clock`
- `capacity`
- `used`
- `active`
- `casting`
- `channeling`
- `finishedRecent`
- `worldGeneration`
- `pendingPlayerCast`
- `pendingSpellId`
- `pendingSequence`

### `TaiYangShenDian("Cast.State.Get", unitToken)`

Reads the current/recent state for a normal 1.12 unit token such as `player` or `target`.

### `TaiYangShenDian("Cast.State.GetByGuid", guid)`

Reads by canonical GUID string, e.g. `0xF130...`.

### `TaiYangShenDian("Cast.State.List")`

Returns active tracked casts/channels only.

`TaiYangShenDian("Cast.State.List", "recent")` also includes recently completed records retained by the 15-second diagnostic/read-side window.

## State row

The row includes the existing API30 fields plus:

- `phase`
- `result`
- `casting`
- `channeling`
- `finished`
- `completedMs`
- `lastTransitionMs`
- `lastEvent`
- `worldGeneration`
- `pending`

Legacy `state` remains for API30 compatibility.

## Clock

R2 uses the client's engine millisecond clock (`OS_GET_ASYNC_TIME_MS`) when available, falling back to `GetTickCount` only if the engine function is unavailable. The low 32-bit time domain matches the client/Lua `GetTime()` convention used elsewhere in the Aura core.
