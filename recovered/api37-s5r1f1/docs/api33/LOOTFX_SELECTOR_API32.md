# LootFX Selector API32

Base: CAST1-R2 Unified CastState + Corpse LootFX Stable

- Base commit: `34a92b7de12979f20029d2482a58a3a5e42c561e`
- DLL: `1.4.0-AURA6D4-R4-CAST1R2-LFX1`
- API: `32`
- Build: `20260830-v140-cast1r2-lootfx-selector-api32`

## Native routes

- Corpse stock LootFX direct CreateModel callsite: `0x0061FA6A`
- Gathering/world-interaction LootFX direct CreateModel callsite: `0x0061FC9F`
- CreateModel entry: `0x00707350`

The corpse route remains installed at process initialization because it is the already-live-tested stable corpse flag feature.
The gathering route is **lazy**: it is patched only after `LootFX.Gather.Set(path)` selects a non-stock model. `LootFX.Gather.Stock()` restores the original 5-byte direct CALL, leaving no gathering hook active.

No ObjectManager scan, slot pool, GUID list, reconcile loop, or periodic polling is introduced.

## API commands

All calls use the single global dispatcher:

```lua
TaiYangShenDian(command, ...)
```

### `LootFX.Status`
Returns one diagnostics string.

### `LootFX.Corpse.Get`
Returns:

```text
path, custom
```

### `LootFX.Corpse.Set(path)`
Sets the corpse LootFX model path. Accepts relative `.m2`, `.mdl`, or `.mdx` model paths. `.m2` is canonicalized to the 1.12 model-loader `.mdl` entry form.

Returns:

```text
ok, code, normalizedPath
```

### `LootFX.Corpse.Default()`
Restores the validated default corpse flag:

```text
Particles\TaiYangCorpse\LootFX.mdl
```

### `LootFX.Corpse.Stock()`
Restores stock corpse sparkle:

```text
Particles\LootFX.mdl
```

### `LootFX.Gather.Get`
Returns:

```text
path, gatherHookActive
```

### `LootFX.Gather.Set(path)`
Selects a custom model for the currently proven shared gathering/world-interaction LootFX route (`0x61FC9F`). This currently affects herbs, mining nodes, and other objects that use the same stock LootFX route (for example some chests).

The gather hook is installed only when a non-stock path is selected.

Returns:

```text
ok, code, normalizedPath
```

### `LootFX.Gather.Stock()`
Restores `Particles\LootFX.mdl` and removes the gathering callsite patch.

## Model discovery

The addon reuses the existing explicit-call M2 scanner APIs:

```text
AutoRange.M2Scan.Start
AutoRange.M2Scan.Step
AutoRange.M2Scan.Status
AutoRange.M2Scan.Get
AutoRange.M2Scan.ArchiveGet
```

Scanning only occurs when the user opens/uses the model browser; it is not a background game-state scan.

## Compatibility

CAST1-R2 `src/spell_cast_core.cpp` and `src/spell_cast_core.h` remain byte/source unchanged from commit `c5855262c5ed768f2b981e9da6c0bd6d9e38b1f7`.
