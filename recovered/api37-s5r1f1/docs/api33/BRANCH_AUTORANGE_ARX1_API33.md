# Branch: dev/autorange-arx1-api33

Parent baseline: `ebd07508bb09d2b6a06cae9cb5f7d271258cbe51` (`dev/lootfx-selector-api32`).

Scope is strictly AutoRange independence support:
1. `Unit.Guid`
2. `Unit.InSight`
3. `GroundProbe.UnitStateByGuid`

Frozen modules remain unchanged from parent: Aura R4, NativeBus, Cast1-R2, LootFX, Visual, DBC, DreamAvatar/DreamWeapon.

No new hook, event table, worker thread, timer, ObjectManager polling loop, or native LOS cache is introduced.
