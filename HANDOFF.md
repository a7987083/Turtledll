# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

This branch now contains the latest compile-verified UnitState, Cooldown and Spatial recovery sources plus an overlay script that applies all three together.

## Source-authentic baseline

Last source-authentic handoff: `TaiYangShenDian_ARX1_API33_DW1_LOS1_HANDOFF_20260830.zip`.

- API 33
- exact rebuild SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`

## Final target

`taiyangshendian_API37_S5_R1F1.dll`

- API 37
- SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`
- size `380928`

## Current combined recovery build

A local all-latest overlay build succeeded with UnitState + Cooldown + Spatial simultaneously:

- SHA256 `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`
- size `371200`
- PE32 i386, Windows 5.01
- target size delta `9728` bytes

Static regression completed so far:

- critical Foundation/Cooldown/UnitState/Spatial command strings present
- `TYS_COOLDOWN_STARTED`, `TYS_COOLDOWN_CHANGED`, `TYS_COOLDOWN_READY` present
- `TYS_UNIT_HEALTH_CHANGED`, `TYS_UNIT_POWER_CHANGED`, `TYS_UNIT_COMBAT_CHANGED` present
- broad dotted-string set: recovery `129`, target `129`, missing `0`, extra `0`

Do not yet call this byte-identical or runtime-verified. API34-37 C++ remains behavior-equivalent reconstruction, not the lost original source text.

## Important implementation notes

- UnitState: exact `object+0x08 -> descriptor`, active-power semantics, dynamic dead flag, Track/Untrack, 128 capacity, exact Status surface, existing PLAYER_LEAVING_WORLD lifecycle funnel.
- Cooldown: engine query `0x006E2EA0`, uint32 wrap-safe timing, kind/source 1..7, STARTED/CHANGED/READY, CLEAR/CHEAT reset semantics, SpellRec classification and API33-derived custom events.
- Spatial: historical range/reach formulas plus S5-R1F1 rear-axis dot semantics. Do not encode experimental ~105° observations; test was unfinished.

## Build issue encountered and resolved

The combined build initially failed because the locally transcribed Spatial x87 inline assembly placed `fld/fsqrt/fstp` and `fld/fsincos/fstp` on a single line. clang-cl rejected that syntax. Restoring the multiline MS-style `__asm` form from the prior compile-verified Spatial implementation fixed the build.

## Remaining work

1. Run stricter callable API regression against known final count `118`.
2. Inspect the remaining `9728`-byte target/recovery size delta for missing behavior; do not pad.
3. Repair `recovery/archive.parts` so GitHub Actions reproduces the local combined build.
4. Perform live client regression.

## Evidence priority

1. final target DLL disassembly
2. historical project branches/source handoffs
3. Aug 25-31 diagnostic plugins
4. Turtle/Tortoise 1.18.1 only for protocol/server-semantic cross-checks
5. other 1.12 client references

## Maintenance

Keep all five files synchronized: `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md`.
