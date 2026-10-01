# Branch: LootFX Selector API32

This branch starts from the live-validated `CAST1-R2 + Corpse LootFX Stable` commit `34a92b7de12979f20029d2482a58a3a5e42c561e`.

Goal: allow a normal 1.12 addon to select the model used for the corpse LootFX route and the shared gathering LootFX route without reintroducing corpse slots, ObjectManager scanning, or a reconcile loop.

## Frozen behavior

Without the addon/config API being called:

- lootable corpse -> isolated default flag M2;
- looted corpse -> native lifetime removes the flag;
- herbs -> stock sparkle;
- mining nodes -> stock sparkle.

## API32 delta

New commands are under `LootFX.*`. The old cast/aura APIs and implementations are untouched.

The gathering hook at `0x61FC9F` is installed lazily only for a custom gathering model and is physically restored when stock gathering is selected.

## Current classification boundary

Real-client A/B testing proved `0x61FC9F` is shared by gathering/world-interaction LootFX. This release intentionally labels it `Gather` rather than claiming herb/mining separation that has not yet been independently proven.

## UI v0.2 fixed-preset update

- DLL/API unchanged (`LFX1`, API32).
- Removed full M2 scanner UI from the selector addon.
- Added fixed model preset matrix based on the selected models.
- Reworked panel to dark/cyan technical-control style.
- Moved `太阳神殿` signature to lower-right.
- Added FuBar entry using the existing `!Libs` Ace2/FuBarPlugin-2.0 stack.
- FuBar left click toggles the selector panel.

## UI v0.3 中文尸体专用更新

- DLL/API 不变：`1.4.0-AURA6D4-R4-CAST1R2-LFX1` / API32。
- 主 UI、预设窗口、按钮、状态提示、FuBar 文案全部改为中文。
- 主界面只保留尸体模型选择，不再显示采集区域。
- 采集模型修改入口从插件层删除；插件只允许调用 `LootFX.Gather.Stock`。
- 为兼容 V0.2 遗留设置，进入世界时主动恢复 `0x61FC9F` 原版调用，并重置 SavedVariables 的采集状态。
- 保留深色/青色科技感 UI、右下角“太阳神殿”署名和 FuBar 入口。
