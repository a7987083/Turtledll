# AURA6-D4-R1 — NativeBus foundation

Branch: `dev/aura6-d4-r1-native-core`
Base: AURA6-D3 `99fb628`
Version: `1.4.0-AURA6D4-R1`
API: `26`
Build: `20260829-v140-aura6d4-r1-native-bus`

## Purpose

D4-R1 removes the runtime compatibility/provider architecture and installs one
TaiYang native transport foundation inspired by the public architecture of
ClassicAPI, but implemented independently in this source tree.

The three shared funnels are:

- incoming packet dispatch: `0x00537AA0`
- outgoing packet send: `0x005379A0`
- world tick: `0x0066FD50`

Feature code subscribes callbacks. It does not own an individual opcode handler.
Every packet subscriber receives an independent body cursor. The original packet
cursor is restored before the engine continues.

## Removed in R1

- Nampower module detection
- provider arbitration / provider switching
- Nampower SignalEventParam tap
- `Aura.Caster.External.Cast`
- `Aura.Caster.External.Aura`
- direct `SPELL_GO` leaf hook at `0x006E7A70`
- late-Nampower suppression/restart branches

No special behavior is selected based on another DLL being present.

## New API

`TaiYangShenDian("Aura.NativeBus.Status")`

Returns a table with:

- `status`
- `architecture`
- `incomingHookInstalled`
- `outgoingHookInstalled`
- `worldTickHookInstalled`
- subscriber counts
- incoming/outgoing packet counters
- world-tick counter
- callback count
- cursor-overrun count
- last incoming/outgoing opcode
- the three hook addresses

## Aura.Caster change

`Aura.Caster` now subscribes to NativeBus incoming packets and parses
`SMSG_SPELL_GO (0x132)` there. `Aura.Caster.Status` exposes
`perOpcodeSpellGoHookInstalled=false` and `packetSubscriberInstalled=true`.

The D3 CasterCore state machine is intentionally retained only as a temporary
R1 consumer. FIFO instance seating / unified AuraSourceCore is D4-R2 work.

## Not done in R1

- target PREDICTED duration promotion
- combo-point send snapshot consumption
- unified AuraSourceCore
- C_UnitAuras compatibility surface
- replacement of the existing custom Aura event-table mechanism

R1 is a transport/ownership checkpoint, not the final D4 Aura model.
