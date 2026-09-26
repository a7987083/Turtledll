# CHANGELOG_DEV

## 2026-09-27

### CI/archive/build recovery completed

- Confirmed the complete authentic API33 LOS1 source archive is now present in `recovery/archive.parts`; CI reconstructs and extracts it successfully.
- Added `prepare_exact_base.py` compatibility constants required by later reconstructed modules:
  - Cooldown query helper `0x006E2EA0`
  - `SMSG_SPELL_COOLDOWN=0x0134`
  - `SMSG_COOLDOWN_EVENT=0x0135`
  - `SMSG_CLEAR_COOLDOWN=0x01DE`
  - `SMSG_COOLDOWN_CHEAT=0x01E1`
  - `SMSG_UPDATE_OBJECT=0x00A9`
  - `SMSG_COMPRESSED_UPDATE_OBJECT=0x01F6`
- Fixed GitHub Runner LLVM 18 clang-cl object naming. Historical `/Fo:foo.obj` produced colon-prefixed output on the runner; final post-overlay now normalizes `/Fo:` to `/Fo` after all compile lines are inserted.
- CI run #56 was the first successful current full link; archive, overlays, compilation/link, contract smoke check and artifact upload all passed.

### Target Status surface correction

- Target disassembly showed Cooldown.Status selects `READY_COOLDOWN_CORE_C1R2` when incoming and world-tick subscription flags are both ready; otherwise it returns the internal status buffer.
- Target disassembly showed the equivalent UnitState.Status rule with `READY_UNITSTATE_US1R2`.
- Added both exact surface corrections in `post_overlay_exact.py` without replacing the internal readiness/error state machine.
- CI run #57 passed all workflow stages after these corrections.

### Current full-linked candidate

- Source commit: `2b8c65e8fa2c0701b5c1c13b39a9a54775d360bc`
- CI run: `Recovery Build #57` / run id `36261981451`
- SHA256: `b30b23ef3df5c24cb285bdb77d10bda4de8c18a1b8a8b99e6b3e50cf1d293f60`
- Size: `359424` bytes
- Runtime verified: **no**

Static regressions against final target `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`:

- strict callable API: `118/118`, missing `0`, extra `0`, exact set equality
- broad dotted strings: `129/129`, missing `0`, extra `0`, exact set equality
- exports: exact 4-name set (`FirstEnterWorld`, `Load`, `TaiYangShenDianNativeStatus`, `_DllMain@12`)
- imports: KERNEL32 only; imported function set `40/40` exact
- `.data`: exact `0x4C00`
- timestamp: zero on both
- PE format/alignment/subsystem/image base match

Current section deltas:

- `.text`: `0x4267` bytes smaller than target
- `.rdata`: `0x934` bytes smaller than target
- `.reloc`: `0x8A0` bytes smaller than target
- full file: `21504` bytes smaller than target

These deltas are not treated as missing behavior by themselves. No padding is permitted.

### `UNIT_RESOLVER_UNAVAILABLE` reassessment

- Final target contains the literal `UNIT_RESOLVER_UNAVAILABLE`.
- Static scan found no absolute 32-bit VA reference to the literal and no direct code xref in the current disassembly pass.
- It remains unresolved and may be an orphan/string-pool residue. It is not added to recovery merely for string parity.
- Confirmed `RESOLVE_UNIT_UNAVAILABLE` remains present and is separate.

## 2026-09-24

### Fast GUID ABI + ObjectManager fallback recovery

- Rechecked final target helper `0x1000BB5D` and corrected the recovery ABI for client fast GUID lookup `0x00464870`.
- Target pushes GUID high then GUID low and the callee returns with `ret 8`; recovery now models the function as `__stdcall(low32, high32)`, not `__fastcall(uint64)`.
- The same ABI correction is applied to UnitState snapshot resolution.
- Recovered the target's bounded explicit ObjectManager fallback used when Spatial literal-GUID fast lookup misses/rejects:
  - global ObjectManager pointer `0x00B41414`
  - head `manager+0xAC`
  - next-link base offset `manager+0xA4`
  - next object `*(current + nextBase + 4)`
  - exact GUID check at `object+0x30/+0x34`
  - object-range validation through `0x38`
  - maximum 4096 candidates and cycle/zero/bad-link termination
- This is an explicit-query fallback and does not contradict `objectManagerScan=false` / no background polling.
- Added `recovered/api37-s5r1f1/post_overlay_exact.py`; workflow executes it after `apply_overlay.py`.
- Updated Spatial evidence documentation to record fast lookup ABI, fallback layout and call graph.

### Selector + Spatial fidelity correction

- Final `UnitState.Get`, `Track`, and `Untrack` require argument 2 to be a Lua string; numeric selectors are not accepted.
- Unit tokens are case-insensitive `player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40` and use client object resolver `0x00515940`.
- Non-token selector is optional `0x`, 1..16 hexadecimal digits, surrounding spaces/tabs only, nonzero.
- Exact UnitState selector errors preserved: `BAD_SELECTOR`, `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`.
- Spatial public selectors are string-only with the same token/hex split.
- Unit.Distance mode matching helper `0x100057EE` is ASCII case-insensitive.
- Final ranged/chains/melee formulas independently confirmed from API37 target helper/call sites.
- Final Behind order corrected: targetFacing validation occurs before degenerate-distance handling; `distance2d <= 0.0001` succeeds with `behind=false`, dot `0`.
- Final S5-R1F1 calibrated score is normalized X delta and remains client calibration only.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
