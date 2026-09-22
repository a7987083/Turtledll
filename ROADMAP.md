# ROADMAP

## Current

- [x] Recover API33 full source baseline.
- [x] Reconstruct API34 Foundation F1 contract.
- [x] Reconstruct API35 Cooldown CD1-R2 command surface and engine-query path.
- [x] Reconstruct API36 UnitState US1-R2 tracked-state surface.
- [x] Reconstruct API37 Spatial S5-R1F1 command surface and facing-axis semantics.
- [x] Build recovered x86 DLL locally.
- [x] Match final DLL command-string set with zero missing/extra entries under the verification rule.

## Next validation

- [ ] Real-machine `Foundation.Status` field-by-field comparison.
- [ ] Real-machine `Cooldown.Get/List` timing and reset packet reconciliation.
- [ ] Verify UnitState event emission and active-power semantics against original API36 DLL.
- [ ] Verify `Spatial.Get`, `Unit.Distance`, `Unit.Behind` return shapes against final API37 DLL.
- [ ] Backstab S5-R2 calibration regression using `TaiYangBackstabDiag`.
- [ ] Replace inferred opcode/signature details with disassembly-confirmed equivalents where needed.
