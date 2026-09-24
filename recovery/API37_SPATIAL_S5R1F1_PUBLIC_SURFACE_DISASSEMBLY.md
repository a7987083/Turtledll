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

## Unit.Distance

Default mode: `CENTER3D`.

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

## Unit.Behind

Errors: `UNSUPPORTED_BUILD`, `UNIT_NOT_VISIBLE`, `BEHIND_UNAVAILABLE`.

Success returns exactly four values:

`(behindBool, "CLIENT_GEOMETRY", round4(behindDot), round4(targetFacing))`.

The S5-R2 ~105 degree observation is unfinished experimental data only and is not implemented as a threshold.

## Internal helpers

- pair resolver: `0x1000B527`
- geometry helper: `0x1000B5A4`
- behind helper: `0x1000B7CB`
- position helper: `0x1000BD44`
- reach helper: `0x1000BE24`

### Reach helper exact path

The final target does **not** use the older historical `object+0x110 -> attr +0x1EC/+0x1F0` path here.

It validates unit/player type, reads descriptor pointer from `object+0x08`, then reads:

- bounding radius: `descriptor + 0x204`
- combat reach: `descriptor + 0x208`

Both values must be finite and in `[0, 100]`.

### Distance sqrt helper behavior

Geometry and behind distance normalization use an inlined eight-iteration Newton square-root approximation initialized with `max(value, 1.0)`, rather than the earlier reconstruction's x87 `fsqrt`. The recovery source now mirrors this path.

### Facing validation

Facing comes from the movement object path `object+0x118 -> +0x1C` and is rejected if NaN or outside `[-100, 100]`.

## Current compile validation

The exact-wrapper/reach/sqrt Spatial source compiles with the existing i686 `clang-cl` no-STL/no-default-lib recovery flags. Full latest DLL relink is still pending; do not assign a new full-candidate SHA until that relink succeeds.
