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
- UnitState US1-R2 public API/status semantics: high confidence; reconciliation/event internals are behavior-oriented reconstructions.
- Spatial S5-R1F1 public API/facing semantics: high confidence from final DLL and diagnostic addon; exact original math/return details require real-machine comparison.

Do not describe the recovered API34→37 C++ files as the original lost source.

## Verified recovery build

- Branch: `recovery/api37-complete`
- Commit: `f5b46d6c32a38154c75bac8ae30f41f96ec22261`
- Actions run: `36857295776` (passed)
- Recovered DLL SHA256: `fa3664ff368ea1b2365eccaa43086c6f6f55e125498daa45d7f1836f84a77f15`


## API35 Cooldown recovery stage (2026-10-03)

- Branch: `recovery/api35-cooldown-contract-parity`
- Sealed artifact commit: `e26f08a2338f0dff447e0e36efda899dfb165fd3`
- Actions run: `37036791370` / Recovery Build #75 (passed)
- DLL SHA256: `c19e094cc2af0e9f781d972dbfcfe4db03b91e3eafb69d348036015f77c8d678`
- Source ZIP SHA256: `3170c90607d98f198492a3621e3d99c2c59091dd2763c685db410ecc034585f4`
- Restored from API35 R2 disassembly: 0x24-byte entry layout, 128x8 dirty queue, duplicate-source priority, active-player GUID path, Spell DB cooldown classification, packet dirtying, reset counters, and deadline revalidation.
- Build verified; runtime / real-machine equivalence is not yet claimed.
