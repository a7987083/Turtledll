# HANDOFF

## Active branch

`recovery/unitstate-us1r2-exact`

API33 is the last source-authentic baseline. API34-37 is behavior-equivalent reconstruction from final DLL disassembly, historical branches/source, diagnostic plugins and supporting references.

## Baseline / target

- API33 exact rebuild SHA256 `aa598a044ee3236c31a87d25d1646cf35a3cf9c3e85a120eea542645af96ba3e`.
- Final API37 target SHA256 `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`, size `380928`, callable APIs `118`, broad dotted strings `129`.

## Last full linked candidate

- SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`
- size `374272`
- previous known size delta `6656`
- callable APIs `118/118`, missing `0`, extra `0`
- dotted strings `129/129`, missing `0`, extra `0`
- exact target build id restored
- runtime verified: no

A newer Spatial source revision is compile-verified at object level but has **not yet been full-linked**, so do not assign a newer DLL SHA until relink succeeds.

## Current module state

### Cooldown CD1-R2

Engine query `0x006E2EA0`, uint32 timing, kind/source, STARTED/CHANGED/READY, reset semantics, SpellRec classification, exact Status/Get policy surface and world-leave reset are integrated. Successful init status is `READY_NATIVEBUS_ENGINE_QUERY`.

### UnitState US1-R2

Descriptor/status/events/lifecycle and public Get/Track/Untrack/Clear surfaces are integrated. Final selector helper at `0x100466AF` supports player/target/mouseover/pet/party1..4/raid1..40 through resolver `0x00515940`, plus GUID text parsing.

`UNIT_RESOLVER_UNAVAILABLE` remains unowned; do not invent an API association.

### Spatial S5-R1F1

Final public handlers are mapped:

- `Spatial.Status 0x1000649D`
- `Spatial.Get 0x10006623`
- `Unit.Distance 0x100077F0`
- `Unit.Behind 0x10007AD8`

Exact public surfaces now recovered:

- Status fields/policies/no-background-work flags and UnitFields indices 129/130.
- Get exact table fields, semantic strings and errors.
- four-decimal numeric rounding.
- Distance exact modes/aliases, two-return success contract, `BAD_MODE` and `MELEE_Z_SEPARATION` errors.
- Behind exact four-return tuple `(bool,"CLIENT_GEOMETRY",dot,facing)` and `BEHIND_UNAVAILABLE`.

Internal helper corrections:

- pair resolver `0x1000B527`
- geometry helper `0x1000B5A4`
- behind helper `0x1000B7CB`
- position helper `0x1000BD44`
- reach helper `0x1000BE24`
- final reach path is `object+0x08 -> descriptor+0x204/+0x208`, not historical `object+0x110`.
- radius/reach accepted range `[0,100]`.
- facing path `object+0x118 -> +0x1c`, accepted range `[-100,100]`.
- target uses eight Newton sqrt iterations from `max(value,1.0)` for geometry normalization.

Exact Spatial source compiles with the existing i686 no-STL/no-default-lib clang-cl flags. Object SHA256 `fd7ecdd871e5dbbef58eab16cb88d5aa4a531296a3690b1183697b447e673554`.

See `recovery/API37_SPATIAL_S5R1F1_PUBLIC_SURFACE_DISASSEMBLY.md`.

The S5-R2 ~105° observation was unfinished testing only; it is not a threshold and must not enter the Spatial core.

## Next build step

Full relink API37 with latest Cooldown + UnitState + exact Spatial source, then rerun:

1. callable API `118/118`
2. dotted strings `129/129`
3. section-size delta
4. target-vs-recovery static surface checks

Never pad to match binary size.

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
