# AURA6-D4-R1 real-client test

Install the DLL and `TaiYangAuraR1Diag`, enter the world, then run:

`/aura6r1`

Expected idle/core result:

- DLL `1.4.0-AURA6D4-R1`, API `26`
- NativeBus incoming/outgoing/world-tick hooks = true
- incoming packet count grows while online
- outgoing packet count grows when the client sends actions
- world tick grows continuously in world
- cursor overrun remains 0
- Caster `perOpcodeSpellGoHookInstalled=false`
- Caster `packetSubscriberInstalled=true`

Cast an aura/debuff several times, then `/aura6r1` again:

- `spellGoCount` increases
- `spellGoTargetObservations` increases for targeted casts
- `parseFailure` should remain 0
- Aura Add/Remove/Stack counters continue to change normally

Compatibility is deliberately not selected or switched at runtime. Optional
coexistence checks with other DLLs are crash/hook-chain tests only:

1. TYS only
2. TYS + Nampower
3. TYS + SuperWoW
4. TYS + Nampower + SuperWoW

For each case, verify that NativeBus stays online and packet cursor overruns stay
0. A failure is treated as a hook-ownership issue to solve structurally, never
by restoring provider arbitration.
