# LootFX Selector UI V0.3 / API32

## 目标

- UI 全中文。
- 保留科技感深色/青色风格。
- 右下角固定署名“太阳神殿”。
- 保留 FuBar 入口。
- UI 只开放尸体 M2 固定预设选择。
- 采集（草药/矿石/其他共用交互）固定恢复客户端原版 LootFX，不再提供修改入口。

## 升级兼容

V0.2 如果曾保存自定义采集模型，V0.3 在 `PLAYER_ENTERING_WORLD` 时主动调用 `LootFX.Gather.Stock`，恢复 `0x61FC9F` 原始调用并将 SavedVariables 中采集状态重置为 stock。

## 尸体选项

- 选择模型：打开固定 M2 预设列表。
- 默认旗帜：`Particles\\TaiYangCorpse\\LootFX.mdl`。
- 原版光：`Particles\\LootFX.mdl`。

已有场景对象不会原地换模型；新生成的可拾取尸体生效。
