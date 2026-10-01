# Branch — CAST1-R2 Unified CastState

- Branch: `dev/cast1-r2-unified-cast-state`
- Parent stable candidate: `stable/cast1-r1-fix1` / `9500da4371ce04af2441da25277bd7012df8aa44`
- DLL: `1.4.0-AURA6D4-R4-CAST1R2`
- API: `31`
- Build: `20260830-v140-cast1r2-unified-cast-state`

## Scope

- Preserve R1-FIX1 NativeBus and dynamic event-slot architecture.
- Add durable terminal result alongside live phase.
- Add canonical `Cast.State.*` read APIs.
- Use the client engine millisecond clock for read-side timestamps.
- Preserve world-reset isolation via `worldGeneration`.
- No Aura core changes.
- No new direct hook.
- No ObjectManager polling.
