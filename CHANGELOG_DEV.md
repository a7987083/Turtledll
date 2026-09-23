# CHANGELOG_DEV

## 2026-09-24

### Full latest API37 combined build

- Reconstructed a local build tree from `TaiYangShenDian_API37_S5R1F1_RECOVERED_SOURCE_20260922.zip` and overlaid the latest recovered UnitState, Cooldown, Spatial, classifier/transition helpers and API33-derived custom-event bridge.
- Patched the existing historical `PLAYER_LEAVING_WORLD` funnel to call `TysUnitStateCore::onWorldLeaving()`; no new lifecycle hook was introduced.
- Added `custom_event_bridge.cpp` to the no-default-lib link path.
- First compile attempt exposed a clang-cl inline-assembly formatting error in Spatial x87 `fsqrt/fsincos`; corrected it by restoring multiline MS-style `__asm` blocks from the compile-verified Spatial record.
- Rebuild then succeeded.
- Combined DLL: SHA256 `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`, size `371200` bytes, PE32 i386 / Windows 5.01.
- Target remains SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`, size `380928` bytes; current size delta is `9728` bytes.
- Verified critical Foundation/Cooldown/UnitState/Spatial API strings plus all six recovered `TYS_COOLDOWN_*` / `TYS_UNIT_*` events.
- Broad dotted-string comparison against the final target is exact: `129` vs `129`, zero missing, zero extra.
- Updated `apply_overlay.py` so future overlay builds copy latest Cooldown + UnitState + Spatial together.

## 2026-09-23

### Cooldown CD1-R2 exact integration

- Integrated binary-confirmed kind/source classification, STARTED/CHANGED/READY transitions, packet-source preservation, SpellRec fields and custom events.
- Compile-test DLL SHA256: `828e7c683ad6c623c37f1b38a95fc5dd74e0242bc7946af13254dad863fa3907`.

### UnitState US1-R2 exact recovery

- Recovered final descriptor path `object+0x08 -> descriptor`, exact health/power/combat/dead fields, category mask, Track/Untrack, 128-slot capacity, Status surface and `worldGeneration` leave-world lifecycle.
- Compile-test DLL SHA256: `d44302b3227c5a13ae9132db4d1b080512dede81820f32a60aeb0782f4c92099`.

### Maintenance

- Continue maintaining `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md` after each recovery/build/validation step.
