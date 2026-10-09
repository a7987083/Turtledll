Current unified development baseline
====================================
DLL: 1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1
API: 33
Build: 20260830-v140-arx1-api33-dw1-los-paircache50
Commit: fdc7be75b8cfcb78c8ae2ee3832f04eeca4dec9e

LOS1 delta only:
- 50 ms symmetric native pair cache for Unit.InSight
- 128 fixed slots, allocation-free
- no hook/thread/timer/background scan
- existing CWorld_Intersect/two-ray LOS algorithm unchanged
- included dynamic 50 ms LOS acceptance addon already replaces the old static API33 panel
