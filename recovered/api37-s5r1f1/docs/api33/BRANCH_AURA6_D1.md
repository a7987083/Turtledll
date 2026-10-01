# AURA6-D1 Development Branch

Branch: `dev/aura6-unified-caster-core`
Parent stable branch: `stable/aura5-fix5`
Frozen tag: `TYS_AURA5_FIX5_STABLE`

## Frozen baseline

- DLL: `1.4.0-AURA5D1-FIX5`
- API: `22`
- Build: `20260828-v140-aura5d1-fix5-slot-correlated-external-caster`
- Real-machine accepted behavior:
  - TYS_NATIVE caster/isMine works.
  - NAMPOWER_EXTERNAL shows both self (`我`) and other (`他`).
  - target A -> B -> A restores caster without waiting for Aura refresh.
  - external refresh no longer contaminates sibling Aura slots.

## Current AURA6-D1 candidate

- DLL: `1.4.0-AURA6D1`
- API: `22`
- Build: `20260828-v140-aura6d1-unified-caster-core`

Implemented architecture:

```
NativeProvider -----------+
                         +--> Always-On CasterCore
NampowerExternalProvider-+       |- Pending correlation
                                 |- ActiveBinding
                                 |- OwnerMemory
                                 |- Match / Snapshot
```

`CasterCore` is provider-independent. Hook arbitration exists only in provider
selection. Nampower mode still installs no competing TYS Aura / EventRegistry /
SPELL_GO hooks; its existing Lua bridge is only an ingress adapter and owns no
caster truth.

## Hard constraints

1. `aura_native.*` and `aura_duration.*` remain byte-identical to FIX5.
2. No Aura polling, ObjectManager polling, worker threads, or periodic scans.
3. No competing TYS Aura/SPELL_GO hooks when Nampower owns them.
4. API remains 22.
5. FIX5 remains the rollback oracle until real-machine parity passes.
6. Nampower AURA_CAST remains Pending-only until ADD/STACK proves rawSlot.
7. Same SpellID sibling slots cannot be overwritten by a refresh on another slot.

## Required real-machine parity tests

- Native: self/other caster attribution.
- External: self/other caster attribution.
- A -> B -> A immediate restore.
- STACK keeps/updates only the exact correlated Aura instance.
- REMOVE clears active binding correctly.
- RawSlot reuse does not inherit unrelated owner.
- Same SpellID on separate slots does not cross-contaminate.
- Refresh only affects the concrete correlated Aura instance.
