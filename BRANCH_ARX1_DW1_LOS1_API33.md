# ARX1/API33 + DrinkWalk DW1 + LOS1 Pair Cache

Date: 2026-08-30

## Purpose
Keep the proven dynamic `Unit.InSight` behavior while bounding duplicate `CWorld_Intersect` work when multiple addons query the same unit pair.

## LOS1 implementation
- API contract unchanged: `TaiYangShenDian("Unit.InSight", from, to)`
- API version remains **33**.
- symmetric GUID-pair cache: `(A,B) == (B,A)`
- fixed TTL: **50 ms**
- fixed direct-mapped storage: **128 slots**
- no heap allocation
- no cache housekeeping loop
- no timer
- no thread
- no new hook
- no ObjectManager background scan
- only successful LOS results are cached; resolution/type/LOS errors are not cached.

## Runtime model
```text
Lua asks Unit.InSight(A,B)
        |
        +-- cache hit, age < 50 ms --> return cached result
        |
        `-- cache miss/expired ------> CWorld_Intersect
                                      -> store result
                                      -> return
```

The cache is a rate limiter, not a refresh engine. If no addon asks for LOS, the DLL performs zero LOS collision queries.

## Reference behavior retained
- WoW 1.12.1 build 5875 `CWorld_Intersect`: `0x00672170`
- LOS flags: `0x100111`
- collision-height aware two-ray test
- dynamic LOS was accepted in game before LOS1: keeping the same target selected while moving behind/away from a wall or pillar changed visible/blocked state without retargeting.

## Frozen modules
Aura6-D4-R4, Cast1-R2, LootFX, DrinkWalk actuator, visual core, MoonMarker, profiler, DBC and DreamAvatar/DreamWeapon modules are not changed by LOS1.
