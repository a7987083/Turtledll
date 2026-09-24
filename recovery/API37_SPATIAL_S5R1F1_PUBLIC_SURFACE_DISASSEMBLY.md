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

Helper `0x1000B5A4` returns the center-distance/Z outputs plus actor/target UnitFields radius/reach values. Mapping its stack outputs at the `Unit.Distance` and `Spatial.Get` call sites confirms:

- `distance2d = sqrt((actor.x-target.x)^2 + (actor.y-target.y)^2)`
- `distance3d = sqrt(distance2d^2 + (actor.z-target.z)^2)`
- `zDelta = abs(actor.z-target.z)`
- actor bounding radius = actor descriptor `+0x204`
- actor combat reach = actor descriptor `+0x208`
- target bounding radius = target descriptor `+0x204`
- target combat reach = target descriptor `+0x208`

The derived modes are exactly:

- `RANGED/RANGED_EDGE = max(0, distance3d - actorCombatReach - targetCombatReach)`
- `CHAINS/CHAINS_EDGE = max(0, distance3d - actorBoundingRadius - targetBoundingRadius)`
- melee eligibility: `zDelta < 6.0`
- `actorMeleeReach = max(1.5, actorCombatReach)`
- `targetMeleeReach = max(1.5, targetCombatReach)`
- `meleeBaseReach = max(5.0, actorMeleeReach + targetMeleeReach + 1.333333373069763)`
- `MELEE/MELEE_BASE_GAP = max(0, distance2d - meleeBaseReach)`

These equations agree with the older UnitXP lineage, but they are now independently confirmed from the final API37 target call graph and machine code rather than inherited by assumption.

## Unit.Behind

Errors: `UNSUPPORTED_BUILD`, `UNIT_NOT_VISIBLE`, `BEHIND_UNAVAILABLE`.

Success returns exactly four values:

`(behindBool, "CLIENT_GEOMETRY", round4(behindDot), round4(targetFacing))`.

### Final S5-R1F1 calibrated rear-axis computation

The target helper at `0x1000B7CB` was rechecked instruction-by-instruction. This corrects the earlier reconstruction that used `cos(targetFacing)` / `sin(targetFacing)` in a conventional forward-vector dot product.

The target order is:

1. read actor/target positions;
2. validate target type/movement/facing and enforce facing in `[-100,100]`;
3. compute `dx = actor.x-target.x`, `dy = actor.y-target.y`;
4. compute `distance2d` with the eight-iteration Newton path;
5. write `targetFacing` output;
6. if `distance2d <= 0.0001`, **succeed** with `behind=false`, dot `0`;
7. otherwise compute the calibrated score.

The final instruction sequence zeroes the Y-axis coefficient before accumulation, so:

- **`behindDot = dx / distance2d`**
- `behind = behindDot > 0`

`targetFacing` is therefore validated and returned to Lua, but in S5-R1F1 it is not multiplied into the calibrated score. This is directly supported by `xorps xmm1,xmm1; mulss normalizedDy,xmm1; addss normalizedDx` in the target.

This is a client calibration artifact, not server Backstab truth. It does not create a 105° rule. The S5-R2 ~105° observation remains unfinished experimental data only.

## Internal helpers

- ASCII case-insensitive comparator: `0x100057EE`
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

Geometry and behind distance normalization use an inlined eight-iteration Newton square-root approximation initialized with `max(value, 1.0)`, rather than the earlier reconstruction's x87 `fsqrt`.

### Facing validation

Facing comes from `object+0x118 -> movement+0x1C` and is rejected if NaN or outside `[-100,100]`.

## Recovery implementation status

`apply_overlay.py` now applies exact-target corrections for:

- normalized-X S5-R1F1 rear-axis scoring;
- target-facing validation before the degenerate-distance branch;
- `distance2d <= 0.0001` success with `behind=false`, dot `0`;
- ASCII case-insensitive Unit.Distance mode matching.

The ranged/chains/melee formulas in the current recovery are now confirmed against the target and require no speculative change.

Full latest API37 relink is still pending because GitHub Actions reconstruction fails before compilation on the incomplete XZ source archive. Do not assign a new full-candidate SHA until a complete relink succeeds.
