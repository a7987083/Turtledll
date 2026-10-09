# Branch: dev/aura6-d4-r4-final-lifecycle

Parent/frozen baseline:
- `stable/aura6-d4-r3`
- `1653e72ef09bc272cd6d9c373e2afbbe89603b33`

Purpose:
- final refresh lifecycle hardening;
- multi-caster same-SpellID refresh FIFO;
- stale old-slot REMOVE guard;
- world/map transient reset;
- retain R3 target PREDICTED timing and R2 AuraSourceCore architecture.

Out of scope:
- `C_UnitAuras` / `AuraUtil` compatibility;
- new Aura hooks;
- background scanning/polling;
- alternate provider modes.
