# TYS LootFX Selector v0.2.0 UI/FuBar update

DLL/API are unchanged from LFX1:
- DLL: `1.4.0-AURA6D4-R4-CAST1R2-LFX1`
- API: `32`

## UI changes
- Replaced free-text input and full M2 scanner with a fixed model preset matrix.
- New dark/cyan technical-control-panel visual style.
- `太阳神殿` signature moved to the lower-right corner.
- Added FuBar plugin entry through the existing `!Libs` Ace2/FuBar stack.
- FuBar left-click toggles the LootFX Selector panel.

## Native behavior
No native routing logic changed in this update.
- Corpse route: `0x61FA6A`
- Gather route: `0x61FC9F` only when a custom gather model is active
- API remains 32
- `spell_cast_core.cpp/.h` unchanged

## Fixed presets
1. Dire Maul Crystal Generator
2. Horde CTF Flag
3. Alliance CTF Flag
4. Neutral CTF Flag
5. Green Horde Flag
6. Gold Alliance Flag
7. Gold Horde Flag
8. Alliance CTF World Flag
9. Alliance CTF Flag Lion
10. Well Of Souls
11. Nerf State
12. Valentines Heart
13. Creature Spell Portal
14. Creature Portal Blue
15. Creature Portal Purple
