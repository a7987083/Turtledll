# API37 Spatial S5-R1F1 public surface / helper disassembly

Target: `taiyangshendian_API37_S5_R1F1.dll` SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`.

This document records final-target evidence. Reconstructed C++ is behavior-equivalent recovery, not the lost original source.

## Public handlers

- `Spatial.Status` -> `0x1000649D`
- `Spatial.Get` -> `0x10006623`
- `Unit.Distance` -> `0x100077F0`
- `Unit.Behind` -> `0x10007AD8`

## Status surface

Confirmed fields/values:

- `stage = S5-R1F1`
- `rangePolicy = EXPLICIT_QUERY_NO_BACKGROUND_WORK`
- `behindPolicy = CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI`
- `behindDotSemantics = POSITIVE_REAR_NEGATIVE_FRONT`
- `facingCalibration = RAW_MOVEMENT_AXIS_TREATED_AS_REAR_FROM_S5R2_LIVE_SAMPLES`
- `losPolicy = REUSE_UNIT_INSIGHT_LOS1_EXPLICIT`
- `serverBackstabPolicy = S5R2_OBSERVER_CALIBRATES_CAST_RESULTS`
- `newHook = false`
- `thread = false`
- `timer = false`
- `objectManagerScan = false`
- `boundingRadiusIndex = 129`
- `combatReachIndex = 130`

`objectManagerScan=false` describes the public/background architecture: there is no periodic ObjectManager scan. It does not forbid the explicit GUID lookup helper from using a bounded fallback walk when the fast lookup misses.

## Spatial.Get

Build check first. Errors return `(nil, code)`:

- `UNSUPPORTED_BUILD`
- `UNIT_NOT_VISIBLE`
- `SPATIAL_DATA_UNAVAILABLE`

Behind sampling is optional for `Spatial.Get`; failure does not fail the geometry result.

Confirmed table fields:

`actorGuid`, `targetGuid`, `distance3d`, `distance2d`, `zDelta`, `actorBoundingRadius`, `actorCombatReach`, `targetBoundingRadius`, `targetCombatReach`, `rangedEdgeGap`, `chainsEdgeGap`, `meleeZEligible`, `meleeBaseReach`, `meleeBaseGap2d`, `behindKnown`, `behind`, `behindDot`, `targetFacing`, `behindSemantics`, `behindDotSemantics`, `serverBehind`, `los`.

Confirmed fixed strings:

- `behindSemantics = CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI`
- `behindDotSemantics = POSITIVE_REAR_NEGATIVE_FRONT`
- `serverBehind = UNVERIFIED`
- `los = USE Unit.InSight EXPLICITLY`

Numeric float outputs are rounded to four decimal places using the target's `10000.0` and `+/-0.5` rounding path.

## Exact selector / GUID object-resolution path

Pair resolver: `0x1000B527`; selector helper: `0x1000B975`; GUID parser: `0x1000BA3C`; GUID object resolver: `0x1000BB5D`.

Public actor/target selectors are **string-only**. Recognized unit tokens are case-insensitive:

`player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40`.

Token selectors call client resolver `0x00515940` with the string in ECX. The resolver returns an object pointer. The target validates/read its nonzero GUID from `object+0x30/+0x34`.

Non-token selectors use the separate GUID parser:

- trim leading spaces/tabs
- optional `0x` / `0X`
- 1..16 hex digits
- trailing spaces/tabs only
- nonzero GUID

### Fast GUID lookup ABI

The target's `0x00464870` call is **not** a `__fastcall(uint64)` call. `0x1000BB5D` pushes GUID high then GUID low and calls `0x00464870`; no caller stack cleanup follows. Equivalent C declaration for recovery is:

```cpp
using FastGuidLookup = unsigned long (__stdcall *)(unsigned long low, unsigned long high);
```

This calling convention/order is binary-confirmed and the recovery post-overlay now uses it.

### Exact fallback walk after fast miss/reject

If the fast path is unavailable/misses/fails validation, target helper `0x1000BB5D` performs a bounded explicit ObjectManager walk:

- global ObjectManager pointer: `0x00B41414`
- manager head field: `+0xAC`
- manager next-link base offset: `+0xA4`
- start at `*(manager+0xAC)`
- each candidate validates an object range of `0x38` bytes
- compare GUID from `candidate+0x30/+0x34`
- next candidate = `*(candidate + nextBase + 4)`
- stop if next == current, next becomes zero, an invalid link is encountered, or after at most 4096 candidates

This is an explicit-query fallback only; it is not background ObjectManager polling.

The recovery `post_overlay_exact.py` now mirrors this path.

## Unit.Distance

Default mode: `CENTER3D`.

The string-comparison helper used by this handler is `0x100057EE`. It folds ASCII `A-Z` to lowercase before comparison, so mode matching is **ASCII case-insensitive**.

Mode aliases:

- `CENTER3D`, `GAUSSIAN` -> center 3D distance
- `CENTER2D` -> center XY distance
- `RANGED`, `RANGED_EDGE` -> ranged edge gap
- `CHAINS`, `CHAINS_EDGE` -> bounding-radius edge gap
- `MELEE`, `MELEE_BASE_GAP` -> melee base gap when vertical separation is eligible

Success returns two values:

- normal modes: `(round4(value), "OK")`
- melee: `(round4(value), "SERVER_INSPIRED_BASE_NO_LEEWAY")`

Errors:

`UNSUPPORTED_BUILD`, `UNIT_NOT_VISIBLE`, `SPATIAL_DATA_UNAVAILABLE`, `MELEE_Z_SEPARATION`, `BAD_MODE`.

### Exact geometry output mapping and formulas

Helper `0x1000B5A4` returns center-distance/Z outputs plus actor/target UnitFields radius/reach values. Mapping its stack outputs at the `Unit.Distance` and `Spatial.Get` call sites confirms:

- `distance2d = sqrt((actor.x-target.x)^2 + (actor.y-target.y)^2)`
- `distance3d = sqrt(distance2d^2 + (actor.z-target.z)^2)`
- `zDelta = abs(actor.z-target.z)`
- actor bounding radius = actor descriptor `+0x204`
- actor combat reach = actor descriptor `+0x208`
- target bounding radius = target descriptor `+0x204`
- target combat reach = target descriptor `+0x208`

Derived modes:

- `RANGED/RANGED_EDGE = max(0, distance3d - actorCombatReach - targetCombatReach)`
- `CHAINS/CHAINS_EDGE = max(0, distance3d - actorBoundingRadius - targetBoundingRadius)`
- melee eligibility: `zDelta < 6.0`
- `actorMeleeReach = max(1.5, actorCombatReach)`
- `targetMeleeReach = max(1.5, targetCombatReach)`
- `meleeBaseReach = max(5.0, actorMeleeReach + targetMeleeReach + 1.333333373069763)`
- `MELEE/MELEE_BASE_GAP = max(0, distance2d - meleeBaseReach)`

These equations agree with older UnitXP lineage but are independently confirmed from final API37 target call graph/machine code.

## Unit.Behind

Errors: `UNSUPPORTED_BUILD`, `UNIT_NOT_VISIBLE`, `BEHIND_UNAVAILABLE`.

Success returns exactly four values:

`(behindBool, "CLIENT_GEOMETRY", round4(behindDot), round4(targetFacing))`.

### Final S5-R1F1 calibrated rear-axis computation

Target helper: `0x1000B7CB`.

Order:

1. read actor/target positions;
2. validate target type/movement/facing and enforce facing in `[-100,100]`;
3. compute `dx = actor.x-target.x`, `dy = actor.y-target.y`;
4. compute `distance2d` with the eight-iteration Newton path;
5. write `targetFacing` output;
6. if `distance2d <= 0.0001`, succeed with `behind=false`, dot `0`;
7. otherwise compute calibrated score.

Final machine code zeroes the Y-axis coefficient before accumulation:

- `behindDot = dx / distance2d`
- `behind = behindDot > 0`

`targetFacing` is validated and returned to Lua, but in S5-R1F1 it is not multiplied into the calibrated score. This is directly supported by `xorps xmm1,xmm1; mulss normalizedDy,xmm1; addss normalizedDx`.

This is client calibration behavior, not server Backstab truth. The S5-R2 ~105° observation remains unfinished experimental data only and is not a threshold.

## Other internal helpers

- ASCII case-insensitive comparator: `0x100057EE`
- geometry helper: `0x1000B5A4`
- behind helper: `0x1000B7CB`
- position helper: `0x1000BD44`
- reach helper: `0x1000BE24`

### Reach helper

Final target validates unit/player type, reads descriptor from `object+0x08`, then:

- bounding radius: `descriptor+0x204`
- combat reach: `descriptor+0x208`

Both values finite and `[0,100]`.

### Distance sqrt helper

Geometry and behind normalization use eight Newton iterations initialized with `max(value,1.0)`, not x87 `fsqrt`.

### Facing validation

Facing is `object+0x118 -> movement+0x1C`, finite and `[-100,100]`.

## Recovery implementation status

`apply_overlay.py` patches selector/public-surface/Behind/mode behavior. `post_overlay_exact.py` then patches binary-confirmed fast-GUID ABI and the explicit ObjectManager fallback walk, plus the same `0x00464870` ABI correction in UnitState.

Both Python overlay scripts pass CI syntax checking. Full latest API37 relink is still blocked in GitHub Actions because the checked-in XZ source archive is truncated before the overlay/build stages. Do not assign a new full-candidate SHA until complete relink succeeds.
