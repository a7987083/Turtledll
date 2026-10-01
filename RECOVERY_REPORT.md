# API37 recovery report

## Result

The `recovery/api37-complete` branch contains a complete buildable tree under
`recovered/api37-s5r1f1/`. GitHub Actions run `36857295776` builds it with
clang-cl/lld-link and passes the repository's PE and API contract checks.

## Binary comparison

| Property | Original final DLL | Recovered build |
|---|---:|---:|
| SHA256 | `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae` | `fa3664ff368ea1b2365eccaa43086c6f6f55e125498daa45d7f1836f84a77f15` |
| Size | 380,928 bytes | 355,840 bytes |
| Format | PE32 i386 DLL | PE32 i386 DLL |
| Imports | KERNEL32 | KERNEL32 |
| Exports | `FirstEnterWorld`, `Load`, `TaiYangShenDianNativeStatus`, `_DllMain@12` | same names and ordinals |
| API version | 37 | 37 |

The binary is behavior-oriented source recovery, not a byte-identical rebuild.
Compiler layout, RVAs and SHA256 differ. The original post-API33 source was not
present in the supplied archive or repository history.

## Recovered layers

- Source-authentic API33 handoff and all earlier modules.
- Foundation F1 counters and fast-GUID/LOS instrumentation.
- Cooldown CD1-R2 engine queries, packet dirtying, deadline reconciliation,
  transition classification, and three dynamic custom events.
- UnitState US1-R2 tracked-GUID reconciliation and health/power/combat events.
- Spatial S5-R1F1 explicit snapshot, distance and calibrated behind test.
- Direct-source CI build, PE verification and downloadable DLL artifact.

## Remaining validation boundary

The reconstructed API34-37 internals require A/B testing in the authorized
WoW 1.12.1/Turtle WoW runtime. In particular: cooldown reset timing, descriptor
lifetimes, event payload parity, and the final behind-axis boundary should be
compared with the supplied original DLL. A static build cannot prove those
runtime-dependent behaviors.
