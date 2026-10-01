# ARX1/API33 + DrinkWalk Native DW1

Date: 2026-08-30

## Purpose
Restore the validated DrinkWalk native actuator that was accidentally omitted while preserving the complete ARX1/API33 mainline.

## Restored API family
- `DrinkWalk.Status`
- `DrinkWalk.ResolveItem`
- `DrinkWalk.UseItem`

## Native chain
`PackBagSlot (0x004F9820) -> GetItemBySlot (0x006228A0) -> CGItem::UseItem (0x005D8D00)`

## Constraints
- No scheduler
- No new hooks
- No packet forging
- Explicit calls only
- Existing Aura / Cast / LootFX / AutoRange ARX1 functionality is unchanged
- API version remains 33 because this is restoration of a previously validated API family, not a new API contract
