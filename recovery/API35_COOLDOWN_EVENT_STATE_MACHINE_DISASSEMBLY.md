# API35 Cooldown CD1-R2 — STARTED / CHANGED / READY state machine

Target: final API37 S5-R1F1 DLL carrying the frozen CD1-R2 cooldown core.

## Internal functions

- `0x10044191` — query/classify one spell cooldown snapshot
- `0x10044926` — remember/merge dirty spell source
- `0x10044998` — query one dirty spell, reconcile tracked record, select event
- `0x10044CCD` — emit cooldown custom event through `SignalEventParam(0x00703F50)`

Tracked cooldown records are 0x24 bytes each, capacity 128.

## Event IDs

The reconciler selects three consecutive custom event IDs:

- `0x258` -> `TYS_COOLDOWN_STARTED`
- `0x259` -> `TYS_COOLDOWN_CHANGED`
- `0x25A` -> `TYS_COOLDOWN_READY`

The event bridge at `0x10044CCD` resolves these IDs to the dynamically claimed event slots and emits the eight-value payload already observed by `TaiYangCooldownDiag_CD1_R2_API35`.

## Recovered transition semantics

Behavior-equivalent logic from `0x10044998`:

```cpp
newState = queryAndClassify(spellId, source);
if (!newState.queryOk)
    return;

old = findTracked(spellId);

if (!old) {
    if (!newState.active)
        return;                    // do not create inactive records
    old = allocateOrEvict();
    store(newState);
    emit STARTED;
    return;
}

if (!newState.active) {
    if (!old.active)
        return;                    // inactive -> inactive: no event
    old.active = false;
    old.source = source;
    old.remaining = 0;
    emit READY;
    return;
}

// new state is active
if (old.active &&
    old.start == newState.start &&
    old.duration == newState.duration &&
    old.enable == newState.enable &&
    old.kind == newState.kind) {
    return;                        // no externally visible change
}

const bool oldWasSpell = (old.kind == SPELL);
store(newState);

// A long SPELL cooldown becoming GCD after CLEAR/CHEAT is tracked specially,
// but the public event is still CHANGED.
if (oldWasSpell && newState.kind == GCD) {
    if (source == CLEAR_COOLDOWN)
        ++clearToGcd;
    else if (source == COOLDOWN_CHEAT)
        ++resetToGcd;
}

emit CHANGED;
```

The binary also keeps dedicated counters for CLEAR/CHEAT READY transitions and for CLEAR/CHEAT SPELL->GCD transitions.

## Source merge priority

`0x10044926` maintains a pending dirty-spell list. Source values 1..5 are packet-derived; 6/7 are deadline/explicit-query sources.

Recovered behavior:

- packet source 1..5 is not overwritten later by DEADLINE(6) or EXPLICIT(7)
- among packet sources already pending for the same spell, the later/higher-priority packet reason can replace the earlier reason according to the binary's numeric ordering
- a previously stored DEADLINE/EXPLICIT reason can be replaced by a packet-derived reason

This preserves the causal packet source for the event when a world-tick reconcile occurs after multiple dirty signals.

## Confidence

- STARTED/CHANGED/READY event selection: confirmed from final DLL disassembly
- no-event equality condition: confirmed
- inactive records are not newly allocated: confirmed
- SPELL -> GCD after CLEAR/CHEAT special counters: confirmed
- public event remains CHANGED for SPELL -> GCD: confirmed
- source merge behavior: confirmed at instruction level; exact policy is documented behavior-equivalently rather than source-identically

Together with `API35_COOLDOWN_KIND_SOURCE_DISASSEMBLY.md`, the main CD1-R2 state machine is now substantially recovered. Remaining work is wiring the recovered classifier/reconciler into the reconstructed C++ and rebuilding/runtime-testing it.