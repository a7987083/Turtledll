# TaiYangDrinkWalk 0.5.2 — 饮料专用与执行确认修正

对照原版《走喝的(2)》V0.5.4 的 ConsumableNeedScore、NativeUseFromHardwareKey、ConfirmFromBagUpdate、ConfirmFromDrinkAura、UI_ERROR_MESSAGE 路径开发；基于用户已实测可用的 0.5.1。

## 核心限制
- 仅在玩家当前使用 Mana 且不满蓝时选择饮料，不使用食物回血。
- 允许 Tooltip 识别为 DRINK 的饮料和含回蓝作用的 BOTH 物品；排除 FOOD 和 UNKNOWN。
- 保留 /zchihe 玩家手动宏触发、DLL Status / ResolveItem / UseItem 原生接口，不走 Lua UseContainerItem。
- 使用前再次核对物品 ID、数量及锁定状态。
- 保留 75ms 最小派发间隔及每预测 Tick 最多 2 次的硬上限；**等待确认记录仍存在时不允许再次调用**，因此短窗口内实际可能仅派发 1 次。这是为了避免客户端未确认造成重复消耗。
- 派发成功仅代表 DLL 已调用。使用后在独立的 900ms 窗口内通过物品总数减少或饮用 Aura 出现进行初步确认。
- 监听 UI_ERROR_MESSAGE，记录 400ms 内对应的游戏端拒绝提示，避免误当成 Tick 失准。
- 使用结果确认时钟从 HUD OnUpdate 移到 Core 的独立事件帧：隐藏 HUD 也能完成超时判定。

## 操作
游戏宏：
```
/zchihe
```
设置界面：`/tsdw`。

## 尚未实现
- 原版完整 Mana 鲁棒分类、VERIFY/MAD、跨区相位恢复、自适应 Lead。
- 原版双状态条、Spark、5SR、回蓝渐隐提示。
- 服务器端确认 API 不存在。背包消耗或 Aura 仍只是客户端观察证据。
- 暂保留开发诊断子命令，后续集中进 /tsdw 设置窗口。

## 验收
1. 旧 0.5.1 先留作回退备份。
2. 缺蓝 + 有饮料：/tsdw 扫描能显示 DRINK，/zchihe 在 Tick 窗口内使用 DLL。
3. 满蓝、无 Mana 资源、仅配置食物：均应拒绝使用。
4. 确认状态应能区分 BAG、AURA 和 GAME_REJECTED；隐藏 HUD 时超时仍能更新。
5. 不把 CI 打包通过当作乌龟服游戏内验收。
