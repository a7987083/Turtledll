# KNOWN_ISSUES

## Active blockers / open verification

### 1. Runtime verification remains pending

The latest API34-37 reconstruction is full-linked and statically accepted, but has not yet been compared against the original API37 DLL in a live WoW/Turtle client.

Current candidate:

- recovery-source commit `2b8c65e8fa2c0701b5c1c13b39a9a54775d360bc`
- latest exact-contract verification commit `1c127fc8d34d101a61afd307df948ae9a56c77f5`
- Recovery Build #58 / run id `36262396903`
- SHA256 `b30b23ef3df5c24cb285bdb77d10bda4de8c18a1b8a8b99e6b3e50cf1d293f60`
- size `359424`
- callable APIs `118/118`, missing `0`, extra `0`, exact set equality
- dotted strings `129/129`, missing `0`, extra `0`, exact set equality
- CI static contract: `STATIC_CONTRACT=PASS`

Run #57 and #58 DLLs are byte-identical. Run #58 added verification only; it did not change runtime code.

Required runtime regression still includes Cooldown ordering/coalescing/reset/deadline, UnitState selector/lifecycle/event timing, Spatial selector/mode/Behind/literal-GUID fallback, and world lifecycle boundaries.

### 2. Residual internal PE delta remains

The current candidate and target match their public API string sets, imports, exports, PE architecture/alignment/subsystem, zero timestamp and `.data` size. They are not byte-identical and the following section deltas remain:

- `.text`: candidate `0x44759`, target `0x489C0`, target larger by `0x4267` / 16999 bytes
- `.rdata`: candidate `0x9BC7`, target `0xA4FB`, target larger by `0x934` / 2356 bytes
- `.data`: exact `0x4C00`
- `.reloc`: candidate `0x4710`, target `0x4FB0`, target larger by `0x8A0` / 2208 bytes
- full file size: target larger by `21504` bytes

Do not use file size as a behavior-completeness metric by itself. Do not pad. Investigate this delta only where runtime or disassembly identifies a concrete behavior mismatch.

### 3. Stored UnitState/Spatial core source is older than final transformed source

`prepare_exact_base.py`, `apply_overlay.py`, and `post_overlay_exact.py` currently transform the source-authentic baseline plus stored reconstructed modules before compilation. This is auditable and CI-verified, but verified corrections should eventually be folded into the stored reconstructed core files after runtime acceptance.

Important transformed behaviors include:

- later-version engine/opcode constants absent from API33 base
- UnitState string-only selector + precise errors
- `0x00464870` fast GUID ABI `__stdcall(low32,high32)`
- Spatial literal-GUID bounded ObjectManager fallback
- Unit.Distance case-insensitive modes
- S5-R1F1 degenerate XY Behind handling and normalized-X calibration
- Cooldown/UnitState target public ready Status values
- LLVM 18 clang-cl `/Fo` output syntax normalization

### 4. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains the literal `UNIT_RESOLVER_UNAVAILABLE`, but the current static pass found no absolute 32-bit VA reference and no direct code xref to the literal. It is distinct from confirmed selector error `RESOLVE_UNIT_UNAVAILABLE`.

Current policy: do not add the literal or invent an owning branch merely to reduce binary/string delta. Revisit only if a target code path or runtime behavior proves ownership.

## Resolved issues

- Exact API33 LOS1 source baseline rebuilds byte-identically at SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- CI source archive is complete; XZ reconstruction/extraction passes.
- API35 Cooldown engine helper/opcode constants missing from API33 header are restored in exact-base adaptation.
- API36 UpdateObject opcode constants missing from API33 header are restored in exact-base adaptation.
- LLVM 18 clang-cl `/Fo:foo.obj` incompatibility is fixed by final `/Fo:` -> `/Fo` normalization.
- Current API37 source full-links successfully in Recovery Build #56, #57 and #58.
- Exact target inventories are stored in `recovery/API37_CALLABLE_118.txt` and `recovery/API37_DOTTED_129.txt`.
- `recovery/verify_static_contract.py` is wired into Recovery Build and CI-enforces exact set equality.
- Recovery Build #58 verifier passed:
  - `dotted: actual=129 expected=129 missing=0 extra=0`
  - `callable: actual=118 expected=118 missing=0 extra=0`
  - `STATIC_CONTRACT=PASS`
- KERNEL32 imported function set: `40/40`, exact set equality.
- Exported name set: `4/4`, exact set equality.
- Cooldown target public ready Status `READY_COOLDOWN_CORE_C1R2` recovered.
- UnitState target public ready Status `READY_UNITSTATE_US1R2` recovered.
- UnitState selector final contract is overlay-corrected.
- Exact UnitState descriptor snapshot path and event/lifecycle behavior are binary-confirmed.
- Spatial public Status/Get/Distance/Behind surfaces are disassembly-mapped.
- Unit.Distance exact mode aliases and ASCII case-insensitive matching recovered.
- Ranged/chains/melee formulas independently target-confirmed.
- Spatial reach path corrected to final `object+0x08 -> descriptor+0x204/+0x208`.
- Spatial radius/reach bounds `[0,100]`, facing bounds `[-100,100]`, Newton sqrt recovered.
- Final S5-R1F1 degenerate XY Behind behavior recovered.
- Final S5-R1F1 calibrated rear score corrected to normalized X delta.
- Fast GUID lookup `0x00464870` ABI corrected to `__stdcall(low32,high32)`.
- Spatial target ObjectManager fallback recovered: `0x00B41414`, `+0xAC` head, `+0xA4` next-base, 4096 bound.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
