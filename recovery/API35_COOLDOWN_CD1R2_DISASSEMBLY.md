# API35 / Cooldown CD1-R2 — disassembly confirmation

Target binary: `taiyangshendian_API37_S5_R1F1.dll`

- SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`
- PE32 / i386
- Image base: `0x10000000`

## Confirmed command dispatcher

Direct string-reference and call-target tracing in the main Lua command dispatcher gives:

- `Cooldown.Status` -> handler `0x10042F0A`
- `Cooldown.Get` -> handler `0x10043C4E`
- `Cooldown.List` -> handler `0x100442E1`

These are code-reference confirmations, not catalog/string-only inference.

## Confirmed engine query

The cooldown implementation performs an executable/readiness check for the client engine routine at:

`0x006E2EA0`

A query path around `0x10044191` checks the address and then calls `0x006E2EA0` directly. The call writes multiple output values used to construct cooldown state, including an active/remaining decision and time/deadline-derived fields.

Observed query call shape is consistent with the recovered CD1-R2 model: local-player spell id query, engine-derived start/duration-like values, then active-deadline reconciliation.

## Confirmed public event/string contract

The final binary contains and references:

- `TYS_COOLDOWN_STARTED`
- `TYS_COOLDOWN_CHANGED`
- `TYS_COOLDOWN_READY`
- `SMSG_CLEAR_COOLDOWN`
- `SMSG_COOLDOWN_CHEAT`

The API catalog states: NativeBus dirty SpellIDs + engine query `0x006E2EA0` + active-deadline revalidation + clear/cheat reset reconciliation; no new hook/thread/spellbook scan.

## Current recovery status

- `Cooldown.Status` dispatch: **disassembly-confirmed**
- `Cooldown.Get` dispatch: **disassembly-confirmed**
- `Cooldown.List` dispatch: **disassembly-confirmed**
- Engine query address `0x006E2EA0`: **disassembly-confirmed**
- Exact query output structure/field semantics: **in progress**
- `SMSG_CLEAR_COOLDOWN` reset handler path: **in progress**
- `SMSG_COOLDOWN_CHEAT` reset handler path: **in progress**
- Event emission ordering/timing: **pending runtime validation**
