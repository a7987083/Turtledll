# TaiYangDrinkWalk 0.3.0 — Tick 观测阶段

WoW 1.12.1 / Lua 5.0；基于实测通过的框架和物品分类版本扩展。**不会调用 DrinkWalk.UseItem，不会自动吃喝，不会控制移动。**

## 新增内容
- 接收玩家的 UNIT_MANA 与 UNIT_ENERGY 事件，比较相邻资源值，只读采样。
- Mana Tick 2.0 秒初始模型：700ms 最小样本间隔、450ms 候选容差，三次周期一致后标记 LOCKED；已锁定时容差 280ms。
- Energy Tick：过滤非 +14～22 的瞬时增加，使用 0.35 秒最小采样间隔；支持错过多个 Tick 时的间隔归一化，最多保存八个观测样本并采用中位数更新周期。
- PLAYER_ENTERING_WORLD 重置观测状态，以免跨世界沿用旧 Tick 锚点。
- 设置界面展示 Mana 和 Energy 的当前锁定状态（点击 Tick 状态按钮刷新）。
- /tsdw tick 显示独立采样状态，/tsdw status 同时显示 Tick 状态。

## 已知限制
- **本阶段仅迁移参考插件的 Tick 采样核心思路，不是完整 V0.5.4 算法移植。** 原版的 Mana 多层异常分类/鲁棒偏差、自适应窗口/Lead、Blade Rush 天赋及敏捷修正、HUD 动态条和实物确认仍未接入。
- 资源满值期间不会产生自然恢复样本，状态可能保持 UNSYNCED。
- 客户端事件不是服务器精确计时源；瞬时回蓝等特殊效果可能影响估计。
- Energy 初始周期为 2000ms，特殊天赋改变周期时，当前简化观测可能无法锁定；后续阶段再接入专门模型。
- 本次已提交与自动打包，但仍需用户在乌龟服中实机验证 UI 和 Tick 观测。

## 游戏命令
- /tsdw — 打开设置窗口
- /tsdw tick — 打印 Mana/Energy 观测结果
- /tsdw status — DLL、模块和观测状态
- /tsdw set 8079, 魔法晶水 — 配置允许选择的物品
- /tsdw scan — 只读扫描，按 Tooltip 分类并评分

**安全边界：** 无后台物品使用，Action.lua 仍为占位模块。
