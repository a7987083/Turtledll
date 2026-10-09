# AURA6-D2 No-Bridge Provider Branch

Branch: `dev/aura6-d2-nobridge-provider`
Parent: `dev/aura6-unified-caster-core` (AURA6-D1)
Stable rollback: `stable/aura5-fix5`
Frozen tag: `TYS_AURA5_FIX5_STABLE`

## Candidate

- DLL: `1.4.0-AURA6D2`
- API: `22`
- Build: `20260828-v140-aura6d2-nobridge-signal-tap`

## Change boundary

Only the NAMPOWER_EXTERNAL provider ingress is replaced:

```text
AURA6-D1:
Nampower -> Lua TaiYangAuraBridge -> External API -> CasterCore

AURA6-D2:
Nampower -> WoW SignalEventParam -> DLL observational tap -> CasterCore
```

No competing TYS Aura/EventRegistry/SPELL_GO hook is installed when Nampower is
present. CasterCore remains always on.

## Hard constraints

1. No TaiYangAuraBridge runtime dependency.
2. No Aura/ObjectManager polling, worker threads, periodic scan or timer.
3. SignalEventParam tap is observational and must preserve original varargs,
   registers, EFLAGS and event dispatch.
4. AURA_CAST stays Pending-only until a concrete ADD/STACK supplies rawSlot.
5. `aura_native.*` and `aura_duration.*` remain frozen.
6. API remains 22; old External ingress commands are disabled compatibility stubs.
7. FIX5 stable branch remains untouched.
