# CHANGELOG_DEV

## 2026-09-24

### Exact-surface API37 recovery

- Built latest UnitState + Cooldown + Spatial together successfully under the i686 clang-cl no-STL/no-default-lib recovery toolchain.
- Strict callable API regression now matches final target exactly: `118` vs `118`, missing `0`, extra `0`.
- Broad dotted-string regression remains exact: `129` vs `129`, missing `0`, extra `0`.
- Refined UnitState.Get from the final DLL: selector queries no longer require prior Track; public fields are `guid`, `visible`, `fieldsValid`, `tracked`, `initialized`, health/dead/combat, `unitFlags`, `dynamicFlags`, active power plus power1..5/maxPower1..5, and `code`.
- Refined UnitState.Untrack: untracked GUID is idempotent success `true,"NOT_TRACKED"`; successful removal returns `true,"UNTRACKED"`.
- Refined UnitState.Clear: clears the full record table and returns `true,"CLEARED"`.
- Corrected UnitState success status to `READY_TRACKED_UPDATEOBJECT_GATE`; preserved exact failure status strings found in the final DLL.
- Refined Cooldown.Status/Get surface and corrected successful initialization status to `READY_NATIVEBUS_ENGINE_QUERY`.
- Added Cooldown `worldGeneration`, `lastChangeMs`, `lastChangedSource`, source name, next-wake state and exact status policy strings.
- Added `TysCooldownCore::onWorldLeaving()` and wired it beside UnitState through the existing PLAYER_LEAVING_WORLD lifecycle funnel; no new hook introduced.
- Current local candidate: SHA256 `6bd0239cd15e66486c47267f70ef9dc6f31cc70b6878e12f08ffa49cfaabb393`, size `373760`; final target remains `380928`, leaving `7168` bytes to investigate without padding.
- Remaining known behavioral gap is exact unit-selector resolver semantics around client helper `0x00515940`; `UNIT_RESOLVER_UNAVAILABLE` remains unexplained and must be xrefed before implementation.

### Earlier 2026-09-24 combined build

- First all-latest combined build SHA256 was `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`, size `371200`.
- Spatial x87 clang-cl issue was resolved by restoring multiline MS-style `__asm` blocks.

## 2026-09-23

- Cooldown classifier/source/transition/custom-event recovery integrated and compile-tested.
- UnitState descriptor/status/events/lifecycle recovery integrated and compile-tested.

## Maintenance

Keep `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, and `KNOWN_ISSUES.md` synchronized after actual recovery/build/validation work.
