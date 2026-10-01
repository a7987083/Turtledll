# AutoRange ARX1 / API33

Baseline: `1.4.0-AURA6D4-R4-CAST1R2-LFX1` / API32 / commit `ebd07508bb09d2b6a06cae9cb5f7d271258cbe51`.

Current build: `1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1` / API33.

## New APIs

### `TaiYangShenDian("Unit.Guid", unitToken)`
Returns:
- success: `guid, "OK"` where guid is canonical uppercase 16-hex without `0x`.
- failure: `nil, code`.

Accepted tokens follow the existing TYS unit-token resolver (`player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40`).

Purpose: replace the SuperWoW extension where `UnitExists("player")` returned a second GUID value.

### `TaiYangShenDian("Unit.InSight", from, to)`
`from` / `to` may be a supported unit token or a GUID string.

Returns:
- `true, "OK"`: clear line of sight.
- `false, "BLOCKED"`: world collision blocks the segment.
- `nil, code`: unresolved/non-visible/unsupported object or LOS unavailable.

Implementation:
- explicit call only; no hook, worker, or timer.
- uses stock WoW 1.12.1 build 5875 `CWorld_Intersect` at `0x00672170`.
- query flags `0x100111` (chosen instead of the older `0x100171` path after review of later UnitXP_SP3 Turtle fixes).
- position uses the object's virtual GetPosition path already used by GroundProbe.
- collision height is read from CMovement when available; LOS tries an equal-height ray first and a second height-aware ray only if required.
- LOS1 adds a DLL-side symmetric `(guidA,guidB)` pair cache with a fixed 50 ms TTL.
- cache is allocation-free and direct-mapped (128 slots); cache collisions only reduce hit rate and never change LOS correctness.
- the native cache is a rate limiter, not a poller: no request means zero LOS work.
- multiple addons asking the same pair within 50 ms share one collision result.

### `TaiYangShenDian("GroundProbe.UnitStateByGuid", guid)`
Returns a Lua table.

Visible Unit/Player fields:
- `guid`
- `visible`
- `objectType`
- `entry`
- `x`, `y`, `z` when readable
- `deadKnown`
- `dead`
- `health`
- `maxHealth`
- `dynamicFlags`
- `code`

Not visible returns a table with `visible=false`, `deadKnown=false`, `code="GUID_NOT_VISIBLE"`.

Death semantics use WoW 1.12.1 UpdateFields: health (`0x16`), max health (`0x1C`) and dynamic flags (`0x8F`); dead is true when health is zero or dynamic flag `0x20` is set.

## Architecture / conflict constraints

ARX1 adds zero hooks. It does not modify NativeBus, Aura R4, Cast1-R2, or LootFX. `Unit.Guid` and `UnitStateByGuid` are read-only explicit queries. `Unit.InSight` only does work when Lua asks for LOS. LOS1 returns a valid <=50 ms cached pair result when available; otherwise it calls the stock client collision function and stores the result.

## Reference strategy

Third-party implementations and 1.12 server/update-field definitions were used to cross-check addresses, flags and field semantics. ARX1 retains the TYS architecture and does not import third-party source files or runtime providers.
