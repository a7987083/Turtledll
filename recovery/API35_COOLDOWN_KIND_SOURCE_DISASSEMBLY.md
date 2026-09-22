# API35 Cooldown CD1-R2 — kind/source classifier recovery

Target: `taiyangshendian_API37_S5_R1F1.dll` (same frozen CD1-R2 core carried into API37)

## Confirmed helper

The internal cooldown snapshot/query helper begins at `0x10044191` and calls the vanilla client cooldown query helper at `0x006E2EA0`.

It also resolves the SpellRec for the queried spell and reads these fields:

- `SpellRec + 0x4C` — RecoveryTime
- `SpellRec + 0x50` — CategoryRecoveryTime
- `SpellRec + 0x274` — StartRecoveryCategory
- `SpellRec + 0x278` — StartRecoveryTime

The helper stores a one-byte `kind` in the snapshot record.

## Exact kind values

The Lua-facing `kindName` mapping is directly visible in the final DLL through the pointer table used by `Cooldown.Get` / `Cooldown.List`:

- `0 = NONE`
- `1 = GCD`
- `2 = SPELL`
- `3 = UNKNOWN`

The table is used at `0x10043F8E..0x10043FA0` and again at `0x10044619..0x1004462D`.

## Recovered classification logic

Disassembly at `0x1004428F..0x100442DC` yields the following behavior-equivalent classifier:

```cpp
if (!active)
    kind = NONE;
else if (source == SPELL_COOLDOWN || source == COOLDOWN_EVENT)
    kind = SPELL;
else if (spellRec && (recoveryTime != 0 || categoryRecoveryTime != 0))
    kind = SPELL;
else if (spellRec && startRecoveryTime != 0)
    kind = GCD;
else
    kind = UNKNOWN;
```

Important: `SMSG_SPELL_COOLDOWN` and `SMSG_COOLDOWN_EVENT` force `SPELL` classification even when the static SpellRec recovery fields do not by themselves prove a long spell cooldown.

## Exact source values

The final DLL has a seven-entry source-name table at `0x1004AE24`. The values are:

1. `SMSG_SPELL_GO`
2. `SMSG_SPELL_COOLDOWN`
3. `SMSG_COOLDOWN_EVENT`
4. `SMSG_CLEAR_COOLDOWN`
5. `SMSG_COOLDOWN_CHEAT`
6. `DEADLINE_RECHECK`
7. `EXPLICIT_ENGINE_QUERY`

This exactly matches the CD1-R2 diagnostic addon's numeric `sourceName()` contract.

## Explicit-query proof

`Cooldown.Get` calls the internal helper with `EDX = 7` at `0x10043CE8..0x10043CED`, proving source `7` is the explicit query path.

## Current confidence

- kind numeric mapping: confirmed from final DLL
- source numeric mapping: confirmed from final DLL
- SpellRec field use: confirmed from final DLL disassembly
- classifier branch logic: confirmed from final DLL disassembly
- STARTED/CHANGED/READY transition state machine: still being traced separately

This evidence supersedes the earlier scaffold behavior that exposed no meaningful `kindName` classification.