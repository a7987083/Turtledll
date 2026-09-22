# HANDOFF

## Baseline

The last source-authentic handoff is `TaiYangShenDian_ARX1_API33_DW1_LOS1_HANDOFF_20260830.zip`.

## Final target

`taiyangshendian_API37_S5_R1F1.dll`

- Version: `1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1-F1-CD1R2-US1R2-S5R1F1`
- API version: 37
- Build id: `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration`
- SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Recovery confidence

- API33 and earlier source: source-authentic.
- Foundation F1 public contract: high confidence; original internal counters/storage not source-authentic.
- Cooldown CD1-R2 public API and engine helper address: high confidence; some internal state/event implementation reconstructed.
- UnitState US1-R2 public API/status semantics: high confidence; exact original reconciliation/event internals reconstructed.
- Spatial S5-R1F1 public API/facing semantics: high confidence from final DLL and diagnostic addon; exact original math/return details require real-machine comparison.

Do not describe the recovered API34→37 C++ files as the original lost source.
