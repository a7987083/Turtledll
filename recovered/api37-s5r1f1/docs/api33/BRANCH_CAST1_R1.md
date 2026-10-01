# CAST1-R1 branch

- Branch: `dev/cast1-r1-native-spellcast-core`
- Base: `24d3e75` (`AURA6-D4-R4 FINAL LIFECYCLE STABLE`)
- Purpose: add Native SpellCast lifecycle without modifying the frozen Aura truth/source/timing cores.
- API: 30
- Candidate until real-client acceptance.

Design rules:
- shared NativeBus only for packet transport;
- no per-opcode spell-handler detours;
- no ObjectManager/unit polling;
- only direct engine observation point: `CGUnit::ClearCastingSpell` for Turtle/SuperWoW interruption behavior;
- if that entry already has a normal JMP detour, follow the existing chain rather than intentionally replacing it;
- no Nampower provider/compatibility mode;
- modern cast API compatibility is deferred.


## FIX1
Current candidate continues on `dev/cast1-r1-fix1-dynamic-events`.
It replaces fixed CAST custom-event slots with dynamic NULL-slot claims and fixes
the false cursor-overrun on `SMSG_SPELL_FAILED_OTHER`. Public API remains 30.
