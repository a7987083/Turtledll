# AURA6-D3 Unified Aura State Service Branch

Branch: `dev/aura6-d3-unified-aura-state`
Parent: `dev/aura6-d2-nobridge-provider`
Stable rollback: `stable/aura5-fix5`
Previous real-machine candidate: `AURA6-D2`

## Candidate

- DLL: `1.4.0-AURA6D3`
- API: `23`
- Build: `20260828-v140-aura6d3-unified-aura-state`

## Purpose

D3 adds one unified read surface without replacing any accepted D2 provider,
caster, duration, or Aura lifecycle path.

```text
NativeProvider -----------+
                          |
NampowerProvider ---------+--> Always-On CasterCore
                          |
UnitFields ---------------+--> Unified Aura State --> Query API
                          |
Self BuffBar expiration --+
```

The service deliberately does not create a second authoritative Aura cache.
Current UnitFields remains the source of truth for whether an Aura exists and
which spell occupies a raw slot.

## API23

```lua
TaiYangShenDian("Aura.State.Status")
TaiYangShenDian("Aura.Get", "target", rawSlot)
TaiYangShenDian("Aura.List", "target")
TaiYangShenDian("Aura.List", "player")
```

Existing API22 commands remain available unchanged.

## Unified row contract

Every currently-present Aura row can contain:

```text
unit / guid / backend
present / state=PRESENT
spellId / rawSlot / luaSlot
stacks / auraLevel
isBuff / isDebuff / type / hidden

casterKnown
casterGuid
isMine / isOther
casterQuality
casterSource
casterResolution
casterAgeMs

durationMs          reserved; 0 until an authoritative per-slot original duration exists
durationKnown       false in D3
expirationMs
remainingMs
hasTimer
timeQuality          EXACT or UNKNOWN
timeSource

lastDeltaKnown
lastDelta            ADD / STACK / SNAPSHOT_ONLY
lastDeltaAgeMs
stateGeneration
```

### Time quality

- Player Aura with a live client BuffBar expiration: `EXACT`.
- Player Aura without a live timed value: `UNKNOWN` (`SELF_NO_TIMED_VALUE`).
- Target Aura: `UNKNOWN` in D3; no fake DBC duration is presented as exact.
- `PREDICTED` is reserved for a later target-duration stage.

### Caster quality

Caster data comes only from the accepted Always-On CasterCore. D3 does not own
a second caster table and does not infer ownership from spellId alone.

## Delta metadata

Both D2 providers call `TysAuraState::observeAura()` after the normal CasterCore
observation. The service writes one direct-mapped metadata cell keyed by
GUID+rawSlot+spellId. This has no loop and does not decide Aura existence.

REMOVE is recorded for diagnostics/counters but a current Aura query never uses
a prior REMOVE as lifecycle provenance. If UnitFields says the Aura is present,
UnitFields wins.

## Frozen behavior

D3 does not alter:

1. Nampower no-Bridge SignalEventParam provider semantics.
2. Native SPELL_GO provider semantics.
3. Pending -> concrete rawSlot correlation.
4. ActiveBinding / OwnerMemory attribution.
5. A -> B -> A caster restore behavior.
6. Same-spell sibling-slot isolation.
7. AURA4-D2 exact self duration hook/event behavior.
8. Aura ADD/REMOVE/STACK event payloads.

`src/aura_native.*`, `src/aura_duration.*`, and `src/aura_caster_core.*` remain
unchanged from AURA6-D2.

## Performance constraints

- no Aura OnUpdate scan
- no ObjectManager poll
- no worker thread
- no periodic snapshot
- no new Aura/SPELL_GO hook
- `Aura.List` scans at most 48 raw slots only when explicitly called
- hot-path D3 work is one fixed hash-cell metadata update per Aura delta

## Real-machine acceptance

Both backends must retain D2 behavior, then API23 is checked:

1. `Core.Version` => AURA6D3 / API23 / expected Build.
2. `Aura.State.Status` => ready, no polling, no Aura hook ownership.
3. `Aura.List("target")` rows match existing Aura.Snapshot spell/slot/stacks.
4. Known caster rows match existing Aura.Caster.Snapshot (`我/他`).
5. `Aura.List("player")` timed Auras show EXACT remaining time and decrement.
6. Target rows stay UNKNOWN time quality in D3.
7. A->B->A immediate caster restore still passes.
8. Same-spell refresh does not contaminate sibling slot/target caster.
