# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

This branch is the current API34-37 behavior-equivalent recovery line. API33 remains the last source-authentic baseline.

## Source-authentic baseline

`TaiYangShenDian_ARX1_API33_DW1_LOS1_HANDOFF_20260830.zip`

- API 33
- exact rebuild SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`

## Final target

`taiyangshendian_API37_S5_R1F1.dll`

- API 37
- SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`
- size `380928`
- known callable API count `118`
- broad dotted-string count `129`

## Current exact-surface recovery candidate

Latest local compile:

- SHA256 `6bd0239cd15e66486c47267f70ef9dc6f31cc70b6878e12f08ffa49cfaabb393`
- size `373760`
- target delta `7168`
- strict callable APIs: `118/118`, missing `0`, extra `0`
- broad dotted strings: `129/129`, missing `0`, extra `0`
- runtime verified: **no**

Do not pad the remaining binary-size delta. API34-37 source is reconstructed behavior-equivalent C++, not the original lost source text.

## Cooldown CD1-R2

Recovered and integrated:

- engine query `0x006E2EA0`
- uint32 wrap-safe timing
- kinds NONE/GCD/SPELL/UNKNOWN
- sources 1..7
- STARTED/CHANGED/READY events
- CLEAR one / CHEAT all reset semantics
- SpellRec timing fields `+0x4C`, `+0x50`, `+0x274`, `+0x278`
- successful init status `READY_NATIVEBUS_ENGINE_QUERY`
- exact Status/Get policy fields, next-wake state, last-change/source state
- `onWorldLeaving()` clears records and advances nonzero world generation
- existing lifecycle funnel used; no new hook

## UnitState US1-R2

Recovered and integrated:

- `GUID -> 0x00464870 -> object -> object+0x08 descriptor`
- health/power/max-power/flags/dynamic-flags exact descriptor layout
- dynamic dead mask `0x20`, combat mask `0x00080000`
- Health/Power/Combat custom events
- category lastChangedMask `1/2/4`
- capacity `128`
- successful init status `READY_TRACKED_UPDATEOBJECT_GATE`
- Get works for untracked selectors and exposes final table surface: visible/fieldsValid/tracked/initialized, flags, active power, all five power lanes and code
- Untrack is idempotent for NOT_TRACKED
- Clear returns `true,"CLEARED"`
- worldGeneration advances on existing PLAYER_LEAVING_WORLD funnel

Remaining UnitState gap: reproduce exact selector helper around client resolver `0x00515940`, including player/target/mouseover/pet/partyN/raidN and exact errors. `UNIT_RESOLVER_UNAVAILABLE` still needs xref ownership before implementation.

## Spatial S5-R1F1

Recovered historical range/reach formulas and final rear-axis dot semantics. No 105° rule is implemented; the observed ~105° value came from unfinished testing and is not a confirmed threshold.

## Build/CI

Local compile is currently authoritative. GitHub Actions remains blocked by incomplete/truncated `recovery/archive.parts` source reconstruction.

## Evidence priority

1. final target DLL disassembly
2. historical project branches/source handoffs
3. Aug 25-31 diagnostic plugins
4. Turtle/Tortoise 1.18.1 for server/protocol cross-check only
5. other 1.12 client references

## Maintenance

Keep all five files synchronized: `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md`.
