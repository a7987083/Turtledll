# CAST1-R1-DIAG2 Remote Cast Start Diagnostic

Purpose: diagnose the real-client report where the first three CAST1 checks remain waiting while another addon visibly shows the target casting.

This build does **not** change Aura truth/state/timing source files and does not use SuperWoW as a CAST1 provider.
`UNIT_CASTEVENT` is observed only as an independent reference in the diagnostic UI.

## Version
- DLL: `1.4.0-AURA6D4-R4-CAST1R1-DIAG2`
- API: `30`
- Build: `20260830-v140-cast1r1-diag2-remote-start`
- Branch: `dev/cast1-r1-diag2-remote-start`

## One test
1. Target the same caster mob used in R1.
2. Let it begin Frostbolt (or any visible cast) at least twice.
3. Kick one cast once.
4. Take one screenshot of the entire TaiYangCastDiag window.

## Read these three diagnostic lines
- `Native: SMSG_START N / parsed N ...`
- `最后START: spell=... cast=... caster=... 当前target=...`
- `SuperWoW参考（不参与判定）: START N ...`

Interpretation:
- SuperWoW START > 0, Native SMSG_START = 0: packet funnel/opcode observation path is the suspect.
- Native SMSG_START > 0, parsed = 0: packet body/layout parsing is the suspect.
- Native parsed > 0, TYS_START = 0: start filtering/state creation is the suspect.
- TYS_START > 0 but first UI row still waits: event delivery/target GUID correlation in Lua is the suspect.
- caster GUID != current target GUID while the visible target is casting: target identity resolution/correlation is the suspect.

The four original acceptance rows still only use `TYS_CAST_*`; the SuperWoW reference can never make them pass.
