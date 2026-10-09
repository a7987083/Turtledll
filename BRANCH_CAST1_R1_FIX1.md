# CAST1-R1-FIX1 — Dynamic Event Slots

Parent: CAST1-R1-DIAG2 (`924f28a`).
Aura baseline remains frozen at `stable/aura6-d4-r4` / `24d3e75`.

## Root cause fixed

The original CAST1 fixed custom events at 590..599. SuperWoW already owns that
high range (`UNIT_HEALTH_GUID` 590 through `KEY_UP` 599; `UNIT_CASTEVENT` 600),
so Lua could not reliably subscribe to `TYS_CAST_*` even though NativeBus had
already parsed the cast and incremented native counters.

FIX1 treats 590..599 only as internal logical event kinds. At FrameScript event
table build time, each TYS event name searches for an existing same-name slot or
claims the first NULL entry from the low end. Occupied slots are never overwritten.
This follows the proven conflict-avoidance strategy studied from ClassicAPI while
remaining an independent implementation.

Also fixed: remote failure packet parsing no longer speculatively reads a reason
byte. `SMSG_SPELL_FAILED_OTHER` ends after guid+spellId; the old optional read
caused NativeBus `cursorOverruns=1` during Kick.

Public API remains 30.
