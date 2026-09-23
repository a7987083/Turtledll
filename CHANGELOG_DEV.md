# CHANGELOG_DEV

## 2026-09-24

### Build-id alignment and binary-delta investigation

- Restored the exact final API37 build id: `20260831-v140-api37-foundation-f1-cd1r2-us1r2-stage5-spatial-s5r1f1-facing-axis-calibration`.
- Added the build-id patch to `recovered/api37-s5r1f1/apply_overlay.py` so rebuilt source bases receive it automatically.
- Local clang-cl/lld-link rebuild succeeded; SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`, size `374272`.
- `.rdata` moved from `0xa4ab` to `0xa4ef`; final target is `0xa4fb`, leaving only `0x0c` bytes of meaningful rdata delta.
- File-size delta remains `6656` because PE file alignment does not change for this string-only correction.
- Final target string `UNIT_RESOLVER_UNAVAILABLE` has no direct absolute address xref, nor an absolute xref to any suffix position of the string, in a raw image scan. Ownership remains unproven and is not being invented.
- API36→API37 stage comparison shows the final Spatial addition grows target `.text` by roughly the same magnitude as the remaining recovery `.text` gap, so next recovery work is focused on exact Spatial implementation/wrappers rather than padding.

### UnitState selector exact recovery

- Disassembled final selector helper at `0x100466AF`.
- Confirmed case-insensitive unit-token recognition for `player`, `target`, `mouseover`, `pet`, `party1..4`, and `raid1..40`.
- Confirmed recognized tokens use client resolver `0x00515940`, then validate/read object GUID at `+0x30/+0x34`.
- Confirmed resolver failures return `RESOLVE_UNIT_UNAVAILABLE` or `UNIT_NOT_FOUND`.
- Confirmed non-token selector path parses a trimmed optional-`0x` hexadecimal GUID with at most 16 digits; invalid/zero/trailing-garbage input returns `GUID_INVALID`.
- Integrated equivalent selector behavior locally and rebuilt successfully.
- Strict callable API check remains exact: `118/118`, missing `0`, extra `0`.

### Exact-surface API37 recovery

- UnitState.Get works without prior Track and exposes final table fields.
- UnitState.Untrack/Clear return semantics aligned.
- Cooldown successful status aligned to `READY_NATIVEBUS_ENGINE_QUERY`; UnitState to `READY_TRACKED_UPDATEOBJECT_GATE`.
- Cooldown and UnitState both reset through existing PLAYER_LEAVING_WORLD funnel.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
