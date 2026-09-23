# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

API33 is the last source-authentic baseline. API34-37 is behavior-equivalent reconstruction from final DLL disassembly, historical branches/source, diagnostic plugins and supporting references.

## Baseline / target

- API33 exact rebuild SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- Final API37 target SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`, size `380928`, callable APIs `118`, broad dotted strings `129`.

## Current local candidate

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- remaining file-size delta `6656`
- callable APIs `118/118`, missing `0`, extra `0`
- broad dotted strings `129/129`, missing `0`, extra `0`
- exact final build id restored
- runtime verified: no

## Current module state

### Cooldown CD1-R2

Engine query `0x006E2EA0`, uint32 timing, kind/source, STARTED/CHANGED/READY, reset semantics, SpellRec classification, exact Status/Get policy surface and world-leave reset are integrated. Successful init status is `READY_NATIVEBUS_ENGINE_QUERY`.

### UnitState US1-R2

Descriptor/status/events/lifecycle and public Get/Track/Untrack/Clear surfaces are integrated. Successful init status is `READY_TRACKED_UPDATEOBJECT_GATE`.

Final selector helper recovered from `0x100466AF`: case-insensitive `player`, `target`, `mouseover`, `pet`, `party1..4`, `raid1..40`; recognized tokens use client resolver `0x00515940`; non-token input follows optional-`0x`, max-16-hex-digit GUID parsing.

`UNIT_RESOLVER_UNAVAILABLE` remains unowned. A raw image scan found no direct absolute address reference to the string or to any suffix position. Do not assign it to an API unless a different reference form is proven.

### Spatial S5-R1F1

Historical range/reach formulas and final rear-axis dot behavior are integrated. The ~105° live-test observation was unfinished and is not a threshold.

## Remaining binary delta

- target `.text` `0x489c0`; recovery `.text` `0x47148`
- target `.rdata` `0xa4fb`; recovery `.rdata` `0xa4ef`
- target `.data` `0x4c00`; recovery `.data` `0x4c00`
- target `.reloc` `0x4fb0`; recovery `.reloc` `0x4df0`

The remaining gap is overwhelmingly code. API36→API37 stage comparison indicates the magnitude closely tracks the original Spatial module addition, so continue exact Spatial/wrapper recovery rather than padding.

## CI / runtime

GitHub Actions reconstruction remains blocked by truncated `recovery/archive.parts`; local clang-cl/lld-link compilation is authoritative for now. Live-client regression remains pending.

## Evidence priority

1. final target DLL disassembly
2. historical project branches/source handoffs
3. Aug 25-31 plugins
4. Turtle/Tortoise 1.18.1 for protocol/server semantics only
5. other 1.12 client references

## Maintenance

Keep all five files synchronized: `ROADMAP.md`, `CHANGELOG_DEV.md`, `HANDOFF.md`, `PROJECT_STATE.json`, `KNOWN_ISSUES.md`.
