# TaiYangDrinkWalk 0.4.0 — Tick HUD 与走喝窗口预览

针对 WoW 1.12.1 / Lua 5.0。保留已验证的基础框架、物品配置/Tooltip 分类及 0.3.0 的 Mana/Energy Tick 观测。

**本阶段只显示，不使用物品。** `Action.lua` 仍为安全占位；WATCH 和 SPAM 都不执行 `DrinkWalk.UseItem`。

## 新增
- 独立可拖动 HUD；位置及显隐保存到 `TaiYangDrinkWalkDB.hud`。
- Mana / Energy 按玩家当前资源自动选择进度条；Rage 显示未同步提示，不虚构怒气恢复 Tick。
- Tick 锁定状态、预计距下一次 Tick 的秒数，以及红色 Tick 零点线。
- 根据已有 Tick 预测的演示窗口：剩余 320–170 毫秒标记为 YES，其他时间为 NO。只在状态为 LOCKED 时显示 YES。
- 设置窗口「显示/隐藏 HUD」「窗口详情」按钮。
- `/tsdw hud` 开关 HUD；`/tsdw window` 打印窗口详情；`/tsdw tick` 保持既有采样诊断。
- 无有效 Tick 锚点时显示 UNSYNCED，不伪造时间窗口。

## 界面和操作
`/tsdw` 打开设置界面；`/tsdw hud` 显隐进度条；`/tsdw window` 查询当前预览窗口；`/tsdw scan` 保留只读物品识别。

## 限制
- HUD 的 80ms 近似刷新只用来显示；不是精确的服务端 Tick 定时。
- 320ms/170ms 是初始参考数值，**不是**已实测的最佳使用窗口。
- Mana/Energy 仍使用当前 0.3.0 的简化观测模型，尚未移植原版全部异常分类与 Blade Rush 调整。
- 不包含自动物品使用、成功确认、自适应 Lead、实际按键触发。
- GitHub Actions 打包成功不能代表乌龟服游戏内运行成功。