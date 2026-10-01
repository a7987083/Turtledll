# CAST1-R1-FIX1 real-client test

Only repeat the same three actions:
1. Select a casting enemy and let it begin one cast.
2. On the next cast, interrupt with Kick.
3. Successfully use one player skill.

Pass conditions shown by the Chinese panel:
- target cast start: passed
- Kick recognized as interrupt: passed
- cast stops after interrupt: passed
- player successful skill: passed
- dynamic event slots: normal
- event registration: 10/10
- parse failures: 0
- cursor overruns: 0
- interrupt entry: normal

The actual slot numbers are intentionally dynamic and may vary with other DLLs.
They must not be assumed to be 590..599.
