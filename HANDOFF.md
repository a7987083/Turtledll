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


## API37 Spatial recovery stage (2026-10-03)

- Branch: `recovery/api37-spatial-layout-parity`
- Sealed artifact commit: `85e0b23530a27e534aadcd24310c90ba8e52a619`
- Actions run: `37039732206` / Recovery Build #83 (passed)
- DLL SHA256: `2e69b2c0de64039c4b324402dbafe0d3f307b00fb314f52a53498053d849fcb0`
- Source ZIP SHA256: `c2e901cc80e9578cfc4d5771ae059e2491c3b2e7b9ad1e8eea2ca8d96208ed86`
- Restored from API37 S5-R1/R1F1 binary and diagnostic evidence: Unit token resolver path, object+0x08 descriptor base, Spatial.Status field contract, Unit.Distance mode/return-code contract, Unit.Behind failure code, and S5-R1F1 rear-axis semantics.
- Build verified; runtime / real-machine equivalence is not yet claimed.


## API34 Foundation F1 recovery stage (2026-10-03)

- Branch: `recovery/api34-foundation-f1-parity`
- Sealed artifact commit: `3f71b7e7e0bd1781346c96739594d377c16c9e38`
- Actions run: `37043137581` / Recovery Build #84 (passed)
- DLL SHA256: `b55c81c285c7f1ec6b1a3371c5f51a1d284e881e94a73ca2930c363d8162ab5e`
- Source ZIP SHA256: `7133509ed18e583eb689ebfb7168f0f4142157de60b37f2c3665b74a14897edf`
- GUID counters were verified against API34 F1 disassembly at the fast/fallback resolver update sites. Fast candidate classification was corrected: odd/unreadable candidates count as misses; readable candidates with unreadable/mismatched GUID count as rejected.
- LOS pair-cache stats order and increment conditions were verified against API34 F1 disassembly and the source-authentic API33 cache core; no additional cache algorithm changes were required.
- Build verified; runtime / real-machine equivalence is not yet claimed.


## Final API37 public-contract parity stage (2026-10-03)

- Branch: `recovery/final-api37-parity`
- Sealed artifact commit: `49b3b3e6e3e12a6a7baf076782d9bc468928e64f`
- Actions run: `37045364959` / Recovery Build #92 (passed)
- DLL SHA256: `a137a50cffa95846d7378e84b256f71b31e4be6fee23b4f2e341b4148b083576`
- Source ZIP SHA256: `c1099d3ce08f57c4d5cb52886335cc1771823fc9df21b5c003cd0afa8870b4ae`
- Public-contract parity work includes API35 Cooldown.Status/Get, API36 UnitState.Status/Get/Track/Untrack/Clear, shared selector parsing, lazy subscription status, and previously sealed Foundation/Spatial corrections.
- Current recovered .text virtual size: `0x43FD1`; original target .text: `0x489C0`; remaining .text delta: 18,927 bytes.
- Current recovered .rdata virtual size: `0xA16B`; original target .rdata: `0xA4FB`; remaining .rdata delta: 912 bytes.
- Static/CI contract parity is substantially improved. Byte-identical or runtime-equivalent recovery is NOT yet claimed. Next stage is function-level internal implementation diff.
