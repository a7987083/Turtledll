# API36 UnitState US1-R2 exact disassembly notes

Target: `taiyangshendian_API37_S5_R1F1.dll`
SHA-256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Dispatcher / public handlers

- `UnitState.Status` -> `0x10045A02`
- `UnitState.Get` -> `0x1004654F`
- `UnitState.List` -> `0x100476DD`
- `UnitState.Clear` -> `0x1004775C`

## NativeBus initialization

Initializer `0x10044F05` confirms US1-R2 reuses the existing NativeBus funnels rather than adding a new packet hook/thread:

- fast GUID helper `0x00464870`
- unit token/GUID helper `0x00515940` lineage check
- incoming subscriber callback `0x10045068`
- world-tick subscriber callback `0x100450AF`
- custom event registration via the existing dynamic FrameScript event bridge

Incoming callback `0x10045068` treats only:

- `SMSG_UPDATE_OBJECT = 0x00A9`
- `SMSG_COMPRESSED_UPDATE_OBJECT = 0x01F6`

as dirty signals. It does not parse UPDATE_OBJECT bodies and does not decompress compressed update packets.

The world-tick callback `0x100450AF` performs the tracked-GUID reconcile after the stock client has processed the packet, matching the frozen contract string `post-handler descriptor reconcile`.

## Exact descriptor snapshot path

Snapshot helper: `0x100469FE`.

The final DLL does **not** read a separate `object+0x110` UnitFields pointer. It resolves the object with `0x00464870`, validates the object range, verifies the live GUID at `object+0x30/+0x34`, accepts Unit/Player types, then reads the descriptor pointer from:

```text
object + 0x08 -> descriptor
```

The descriptor range is validated once from `descriptor+0x58` through `descriptor+0x23F` (start `+0x58`, length `0x1E8`). This is the concrete implementation behind `ONE_OBJECT_RANGE_PLUS_ONE_DESCRIPTOR_RANGE_PER_SNAPSHOT`.

Confirmed absolute descriptor offsets used by the target DLL:

```text
+0x58  health
+0x5C  power1
+0x60  power2
+0x64  power3
+0x68  power4
+0x6C  power5
+0x70  maxHealth
+0x74  maxPower1
+0x78  maxPower2
+0x7C  maxPower3
+0x80  maxPower4
+0x84  maxPower5
+0x90  packed power type byte in bits 24..31
+0xB8  unit flags; combat = bit 19 / mask 0x00080000
+0x23C additional unit state field used by the frozen dead/state policy
```

Active power is selected only from the current power type (0..4), matching `ACTIVE_POWER_ONLY_TYPE_VALUE_MAX`.

## Snapshot authority / empty descriptor behavior

`0x100469FE` first marks an otherwise valid resolved object as present, then attempts to obtain/validate its descriptor. A zero descriptor pointer or unreadable descriptor does not fabricate zero HP/power values. The caller distinguishes object-unavailable from descriptor-unavailable and keeps the previous authoritative cached snapshot when the descriptor itself is transiently absent, matching:

`UNITFIELDS_PRESENCE_AUTHORITY; EMPTY_DESCRIPTOR_PRESERVES_CACHE`.

## Change detection / public event conditions

Main reconcile loop is inside `0x100450AF`.

For an existing cached record the DLL compares old vs new snapshot and independently derives three public changes:

### Health event

Health-change is true when any of the frozen health contract members changes (current health, max health, or the derived dead state). The event is emitted through `SignalEventParam(0x00703F50)` using the registered `TYS_UNIT_HEALTH_CHANGED` slot.

Payload order is behaviorally confirmed as:

```text
guid, oldHealth, newHealth, maxHealth, dead
```

### Power event

Power-change mask is exactly the frozen public contract:

```text
bit0 = active power type changed
bit1 = active power value changed
bit2 = active max-power changed
```

Only the active power lane is compared/emitted. Event: `TYS_UNIT_POWER_CHANGED`.

Payload:

```text
guid, powerType, oldPower, newPower, maxPower, mask
```

### Combat event

Combat is derived from descriptor `+0xB8`, bit 19 (`0x00080000`). A boolean transition emits `TYS_UNIT_COMBAT_CHANGED` with:

```text
guid, oldCombat, newCombat
```

The three event counters are incremented only after their corresponding `SignalEventParam` path is taken.

## worldGeneration

Global world-generation counter is at `0x100595F4`.

Reset routine `0x100459BE` clears per-record live/dirty state and increments `worldGeneration` with wrap-safe nonzero progression.

The only write to `0x100595F4` in the target DLL is this reset routine. It is called from the central event path for `PLAYER_LEAVING_WORLD`; the adjacent path handles `PLAYER_ENTERING_WORLD` separately. Therefore the target uses `worldGeneration` to invalidate/unbind cached unit snapshots across world-leave transitions rather than incrementing on every ordinary UPDATE_OBJECT packet.

## Important correction to the earlier recovery scaffold

Earlier recovered code used the historical UnitXP-style `object+0x110 -> UnitFields` representation. That representation is useful for historical cross-checking but is **not** the exact final API37 implementation path. The final target uses `object+0x08 -> descriptor` and absolute descriptor offsets listed above. Recovery code should be aligned to this final binary path.

## Confidence

- descriptor pointer and offsets: **binary-confirmed**
- UPDATE_OBJECT/COMPRESSED_UPDATE_OBJECT dirty gate: **binary-confirmed**
- post-stock-handler world-tick reconcile: **binary-confirmed architecture/call order**
- health/power/combat event separation and payload contracts: **binary + diagnostic-plugin confirmed**
- `worldGeneration` write site and PLAYER_LEAVING_WORLD linkage: **binary-confirmed**
- semantic meaning of descriptor `+0x23C` beyond its participation in dead/state derivation: **not yet named with source-level certainty**
