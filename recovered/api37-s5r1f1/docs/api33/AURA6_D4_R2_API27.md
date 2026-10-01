# AURA6-D4-R2 / API27

## Build
- DLL: `1.4.0-AURA6D4-R2`
- API: `27`
- Build: `20260829-v140-aura6d4-r2-aura-source-core`
- Parent: AURA6-D4-R1 NativeBus (`c2d02b7`)

## New API27 names
- `TaiYangShenDian("Aura.Source.Status")`
- `TaiYangShenDian("Aura.Source.Match", guid, spellId, rawSlot)`
- `TaiYangShenDian("Aura.Source.Snapshot", unit)`

Existing `Aura.Caster.Status/Match/Snapshot` names remain stable and read the same R2 core; there is not a second caster cache.

## R2 architecture

```text
NativeBus / SMSG_SPELL_GO
          |
          v
  PendingApplication[]
       FIFO / one-shot
          |
OnAuraAdded/Stack/Removed
          |
          v
      AuraSourceCore
(targetGuid, spellId, casterGuid)
          |
       rawSlot binding
          |
   Aura.State / Aura.Get/List
```

### Rules
1. Aura identity is `(targetGuid, spellId, casterGuid)`.
2. `OnAuraAdded` seats the **oldest** matching unconsumed application (FIFO).
3. A Pending application is consumed once; `usedSlotMask` no longer exists.
4. `Aura.Source.Match` / `Aura.Get/List` never consume Pending.
5. On STACK, an already-bound slot owner wins. A fresh cast from another caster cannot steal that slot.
6. Genuine REMOVE invalidates the Aura instance. There is no separate OwnerMemory resurrection path.
7. UnitFields remains Aura-presence authority. An all-empty descriptor is treated as a possible visibility teardown and does not erase the instance cache.
8. A populated contradictory descriptor reconciles stale instances without guessing between multiple same-spell casters.
9. R2 does not yet promote target duration into `Aura.Get/List`; target time remains `UNKNOWN`.

## Fixed-size state
- Pending capacity: 512
- Aura instance capacity: 2048
- Correlation window: 3000 ms
- No polling thread / ObjectManager scan / Aura OnUpdate scan.
