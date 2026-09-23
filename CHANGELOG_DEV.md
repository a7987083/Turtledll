# CHANGELOG_DEV

## 2026-09-24

### UnitState selector exact recovery

- Disassembled final selector helper at `0x100466AF`.
- Confirmed case-insensitive unit-token recognition for `player`, `target`, `mouseover`, `pet`, `party1..4`, and `raid1..40`.
- Confirmed recognized tokens use client resolver `0x00515940`, then validate/read object GUID at `+0x30/+0x34`.
- Confirmed resolver failures return `RESOLVE_UNIT_UNAVAILABLE` or `UNIT_NOT_FOUND`.
- Confirmed non-token selector path parses a trimmed optional-`0x` hexadecimal GUID with at most 16 digits; invalid/zero/trailing-garbage input returns `GUID_INVALID`.
- Integrated equivalent selector behavior locally and rebuilt successfully.
- Current DLL SHA256 `c97218fcf4c375d385cf088f6883c03c23fe974ae0fe7c302614b7f6609ddb05`, size `374272`.
- Strict callable API check remains exact: `118/118`, missing `0`, extra `0`.
- Remaining file-size delta is `6656` bytes; `.text` accounts for most of it. No padding will be used.

### Exact-surface API37 recovery

- UnitState.Get now works without prior Track and exposes final table fields.
- UnitState.Untrack/Clear return semantics aligned.
- Cooldown successful status aligned to `READY_NATIVEBUS_ENGINE_QUERY`; UnitState to `READY_TRACKED_UPDATEOBJECT_GATE`.
- Cooldown and UnitState both reset through existing PLAYER_LEAVING_WORLD funnel.
- Exact-surface predecessor candidate SHA256 was `6bd0239cd15e66486c47267f70ef9dc6f31cc70b6878e12f08ffa49cfaabb393`, size `373760`.

### Earlier combined build

- First all-latest candidate SHA256 `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`, size `371200`.
- Spatial x87 clang-cl transcription issue resolved using multiline MS-style `__asm`.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after recovery/build/validation work.
