# API36 UnitState US1-R2 exact disassembly notes

Target: `taiyangshendian_API37_S5_R1F1.dll`
SHA-256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Dispatcher / public handlers

- `UnitState.Status` -> `0x10045A02`
- `UnitState.Get` -> `0x1004654F`
- `UnitState.Track` -> `0x1004715F`
- `UnitState.Untrack` -> `0x10047589`
- `UnitState.List` -> `0x100476DD`
- `UnitState.Clear` -> `0x1004775C`

## NativeBus initialization

Initializer `0x10044F05` confirms US1-R2 reuses the existing NativeBus funnels rather than adding a new packet hook/thread:

- fast GUID helper `0x00464870`
- unit-token object resolver `0x00515940`
- incoming subscriber callback `0x10045068`
- world-tick subscriber callback `0x100450AF`
- custom event registration via the existing dynamic FrameScript event bridge

Incoming callback `0x10045068` treats only:

- `SMSG_UPDATE_OBJECT = 0x00A9`
- `SMSG_COMPRESSED_UPDATE_OBJECT = 0x01F6`

as dirty signals. It does not parse UPDATE_OBJECT bodies and does not decompress compressed update packets.

The world-tick callback `0x100450AF` performs the tracked-GUID reconcile after the stock client has processed the packet, matching the frozen contract string `post-handler descriptor reconcile`.

## Exact selector resolver contract

Selector helper: `0x100466AF`.

`UnitState.Get`, `UnitState.Track`, and `UnitState.Untrack` first require Lua argument 2 to be a **string**. Numeric selectors are not accepted by these final handlers.

The selector helper recognizes these unit tokens case-insensitively:

- `player`
- `target`
- `mouseover`
- `pet`
- `party1..party4`
- `raid1..raid40`

Recognized tokens call client resolver `0x00515940`. This resolver returns an object pointer, not a GUID scalar. The target then validates/reads the live 64-bit GUID at `object+0x30/+0x34`; a zero/unreadable result is not accepted.

Exact selector error surface:

- malformed/missing non-string public argument -> `BAD_SELECTOR`
- recognized unit token but `0x00515940` unavailable -> `RESOLVE_UNIT_UNAVAILABLE`
- recognized token but resolver/object/GUID unavailable -> `UNIT_NOT_FOUND`
- non-token text that is not a valid GUID literal -> `GUID_INVALID`
- success -> internal status `OK`

Non-token text follows a separate hexadecimal GUID parser:

- trim leading spaces/tabs only
- optional `0x` / `0X`
- 1..16 hexadecimal digits
- trim trailing spaces/tabs only
- reject trailing non-whitespace garbage
- reject zero

The public handlers preserve the helper's precise error string rather than collapsing every selector failure to `BAD_SELECTOR`:

- `Get` failure -> `(nil, error)`
- `Track` failure -> `(nil, error)`
- `Untrack` failure -> `(false, error)`

The recovery overlay now patches the earlier simplified `0x00515970`/numeric-selector path to this final selector contract before compilation.

## Exact descriptor snapshot path

Snapshot helper: `0x100469FE`.

The final DLL does **not** read a separate `object+0x110` UnitFields pointer. It resolves the object with `0x00464870`, validates the object range, verifies the live GUID at `object+0x30/+0x34`, accepts Unit/Player types, then reads the descriptor pointer from:

```text
object + 0x08 -> descriptor
```

The descriptor range is validated once from `descriptor+0x58` through `descriptor+0x23F` (start `+0x58`, length `0x1E8`). This is the concrete implementation behind `ONE_OBJECT_RANGE_PLUS_ONE_DESCRIPTOR_RANGE_PER_SNAPSHOT`.

Confirmed absolute descriptor offsets used by the target DLL:

```text
+0x58  health                     (UNIT_FIELD_HEALTH)
+0x5C  power1                     (UNIT_FIELD_POWER1)
+0x60  power2
+0x64  power3
+0x68  power4
+0x6C  power5
+0x70  maxHealth                  (UNIT_FIELD_MAXHEALTH)
+0x74  maxPower1                  (UNIT_FIELD_MAXPOWER1)
+0x78  maxPower2
+0x7C  maxPower3
+0x80  maxPower4
+0x84  maxPower5
+0x90  packed power type byte in bits 24..31
+0xB8  UNIT_FIELD_FLAGS; combat = bit 19 / mask 0x00080000
+0x23C UNIT_DYNAMIC_FLAGS
```

Turtle/Tortoise 1.18.1 `UpdateFields.h` independently names update-field index 143 as `UNIT_DYNAMIC_FLAGS`; `143 * 4 = 0x23C`. Its `SharedDefines.h` defines `UNIT_DYNFLAG_DEAD = 0x0020`, exactly matching the target DLL's extraction of bit 5 from descriptor `+0x23C`.

Therefore the target's derived `dead` state is:

```text
health == 0  OR  (UNIT_DYNAMIC_FLAGS & 0x20) != 0
```

Active power is selected only from current power type 0..4, matching `ACTIVE_POWER_ONLY_TYPE_VALUE_MAX`.

## Snapshot authority / empty descriptor behavior

`0x100469FE` first marks an otherwise valid resolved object as present, then attempts to obtain/validate its descriptor. A zero descriptor pointer or unreadable descriptor does not fabricate zero HP/power values. The caller distinguishes object-unavailable from descriptor-unavailable and keeps the previous authoritative cached snapshot when the descriptor itself is transiently absent, matching:

`UNITFIELDS_PRESENCE_AUTHORITY; EMPTY_DESCRIPTOR_PRESERVES_CACHE`.

## Change detection / public event conditions

Main reconcile loop is inside `0x100450AF`.

For an existing cached record the DLL compares old vs new snapshot and independently derives three public changes.

### Health event

Change when current health, max health, or derived dead state changes. Event: `TYS_UNIT_HEALTH_CHANGED`.

Payload:

```text
guid, oldHealth, newHealth, maxHealth, dead
```

### Power event

Power mask:

```text
bit0 = active power type changed
bit1 = active power value changed
bit2 = active max-power changed
```

Only active power is compared/emitted. Event: `TYS_UNIT_POWER_CHANGED`.

Payload:

```text
guid, powerType, oldPower, newPower, maxPower, mask
```

### Combat event

Combat is descriptor `+0xB8`, bit 19 (`0x00080000`). Event: `TYS_UNIT_COMBAT_CHANGED`.

Payload:

```text
guid, oldCombat, newCombat
```

The event counters increment only after their corresponding `SignalEventParam` path is taken.

## worldGeneration

Global counter: `0x100595F4`.

Reset routine `0x100459BE` clears per-record live/dirty state and advances `worldGeneration` with nonzero progression. It is called by the central `PLAYER_LEAVING_WORLD` path, not ordinary UPDATE_OBJECT traffic.

## Important corrections to earlier recovery scaffolds

1. Historical UnitXP `object+0x110 -> UnitFields` is useful lineage evidence but is not the final API37 snapshot path. Final snapshot uses `object+0x08 -> descriptor`.
2. Earlier recovery used `0x00515970` as if a unit-string helper returned a GUID and also accepted numeric Lua selectors. Final UnitState handlers are string-only and use `0x00515940` as an object resolver for recognized unit tokens, with a separate hexadecimal GUID parser for non-token strings.

## Confidence

- selector token set / resolver address / handler string-only contract / error branches: **binary-confirmed**
- descriptor pointer and offsets: **binary-confirmed**
- `+0x23C = UNIT_DYNAMIC_FLAGS`, dead bit `0x20`: **binary + Turtle 1.18.1 source confirmed**
- UPDATE_OBJECT/COMPRESSED_UPDATE_OBJECT dirty gate: **binary-confirmed**
- post-stock-handler world-tick reconcile: **binary-confirmed architecture/call order**
- health/power/combat event separation and payload contracts: **binary + diagnostic-plugin confirmed**
- `worldGeneration` write site and PLAYER_LEAVING_WORLD linkage: **binary-confirmed**
