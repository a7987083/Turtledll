# TaiYangShenDian current source baseline

Date: 2026-08-30
Branch: `dev/los-paircache50-api33`

## Current DLL
- Version: `1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1`
- API: `33`
- Build: `20260830-v140-arx1-api33-dw1-los-paircache50`

## Frozen native bases
- Aura: `AURA6-D4-R4 FINAL LIFECYCLE STABLE`
- Cast: `CAST1-R2 Unified CastState`
- LootFX: `LFX1` corpse/gather split
- AutoRange native surface: `ARX1 / API33`
- DrinkWalk: `DW1` native item actuator restored and user runtime-confirmed

## LOS1
`Unit.InSight` remains an explicit-call general API. LOS1 adds a 50 ms symmetric native pair cache so independent addons can share the same recent LOS result without repeating `CWorld_Intersect`. The cache uses 128 fixed direct-mapped slots, no heap allocation, no background cleanup, no timer, no thread and no new hook.

See:
- `AUTORANGE_ARX1_API33.md`
- `BRANCH_ARX1_DW1_LOS1_API33.md`
- `tests/run_los_paircache50_regression.sh`

## Compatibility principle
Do not add polling or movement hooks merely to keep LOS fresh. Callers decide when LOS is needed; the DLL only bounds duplicate collision work. Existing Aura/Cast/event hooks stay frozen unless a separately reproduced defect requires changes.
