# CAST1-R1 Native SpellCast Core — API30

## Version
- DLL: `1.4.0-AURA6D4-R4-CAST1R1-FIX1`
- API: `30`
- Build: `20260830-v140-cast1r1-fix1-dynamic-events`
- Base: frozen `AURA6-D4-R4 FINAL LIFECYCLE STABLE` (`24d3e75`)

## Architecture
CAST1-R1 reuses the single-owner `NativeBus` introduced by AURA6-D4-R1:
- incoming PacketDispatch `0x00537AA0`
- outgoing SendObserver `0x005379A0`
- WorldTick `0x0066FD50`

It does **not** install per-opcode spell packet hooks and does not scan ObjectManager.
The only new direct engine observation point is `CGUnit::ClearCastingSpell`
(`0x0060D040`), because Turtle/SuperWoW can terminate remote casts by calling
that method directly without a reliably broadcast failure packet. If that
entry is already a standard JMP detour, CAST1 follows the existing chain and
hooks its current destination instead of deliberately replacing the entry JMP.

## New API
### `TaiYangShenDian("Cast.Status")`
Returns the transport/hook state plus monotonic diagnostic counters.
Important fields:
- `status`
- `incomingSubscriber`
- `outgoingSubscriber`
- `worldTickSubscriber`
- `clearCastingHook`
- `customEventsReady`
- `dynamicEventSlots = true`
- `eventSlotSent`, `eventSlotStart`, `eventSlotSuccess`, `eventSlotFailed`, `eventSlotInterrupted`, `eventSlotStop`, `eventSlotChannelStart`, `eventSlotChannelUpdate`, `eventSlotChannelStop`, `eventSlotDelayed`
- `perOpcodeHooks = false`
- `objectManagerPolling = false`
- `sent`, `starts`, `success`, `failed`, `interrupted`, `stops`
- `channelStarts`, `channelUpdates`, `channelStops`, `channelRestamps`
- `delayed`
- `castResultAccepted`, `castResultRejected`
- `parseFailure`

### `TaiYangShenDian("Cast.Get", unit)`
`unit` is a normal 1.12 unit token such as `"player"` or `"target"`.
Returns nil when no recent record exists, otherwise:
- `known`, `active`
- `casterGuid`, `targetGuid`
- `spellId`, `spellName`
- `kind = CAST | CHANNEL`
- `state = CASTING | CHANNELING | SUCCESS | FAILED | INTERRUPTED | STOPPED`
- `startMs`, `endMs`, `durationMs`, `remainingMs`
- `delayMs`, `failureCode`, `sequence`, `successSeen`

## New custom events
The names below are dynamically assigned to free FrameScript event slots at runtime.
Their numeric slots are deliberately **not fixed**; occupied slots are never overwritten.
This avoids collisions with SuperWoW's fixed high-range events.

- `TYS_CAST_SENT`
- `TYS_CAST_START`
- `TYS_CAST_SUCCESS`
- `TYS_CAST_FAILED`
- `TYS_CAST_INTERRUPTED`
- `TYS_CAST_STOP`
- `TYS_CHANNEL_START`
- `TYS_CHANNEL_UPDATE`
- `TYS_CHANNEL_STOP`
- `TYS_CAST_DELAYED`

Payload:
`arg1=casterGuid, arg2=targetGuid, arg3=spellId, arg4=kind, arg5=startMs, arg6=endMs, arg7=durationMs, arg8=reason/value`

## Semantics
- `SMSG_SPELL_START` is the authoritative cast-start/cast-time observation.
- Pure instant spells do not emit `TYS_CAST_START`; their completion is `TYS_CAST_SUCCESS`.
- `SMSG_SPELL_GO` emits success and closes ordinary casts.
- remote failure packets are parsed only through their common `guid + spellId` prefix; no speculative trailing byte read is performed.
- `SMSG_CAST_RESULT` status `0` means accepted; status `2` carries a failure result.
- `SMSG_SPELL_DELAYED` extends the current cast end time.
- local `MSG_CHANNEL_START/UPDATE` drives local channel lifecycle.
- remote instant channels can be identified from Spell.dbc channel attributes + `SMSG_SPELL_START`.
- `ClearCastingSpell` is used as the Turtle/SuperWoW remote interruption choke point.
- active channels end with `TYS_CHANNEL_STOP`; CAST1 does not fabricate a separate
  `TYS_CAST_INTERRUPTED` event for channel termination.
- the WorldTick callback only expires CAST1's own fixed 64-record table; it never enumerates game objects.

## Deliberate R1 limits
- No full `UNIT_SPELLCAST_*` compatibility adapter yet.
- No direct `Spell_C_SpellFailed` hook, to avoid competing with Nampower-style spell-failure hooks. Therefore purely client-side rejected casts that never produce a server packet may not emit `TYS_CAST_FAILED` in R1.
- Remote cast-then-channel second phases may be protocol-limited when the server does not broadcast a channel-start packet to observers.
