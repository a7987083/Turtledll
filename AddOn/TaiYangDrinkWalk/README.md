# TaiYangDrinkWalk 0.5.0 — DLL 原生走喝按键执行

WoW 1.12.1 / Lua 5.0。继承 0.4.0 的物品 Tooltip 分类、Mana/Energy Tick 与 HUD。

**DLL 必须提供 API33 的 DrinkWalk.Status / DrinkWalk.ResolveItem / DrinkWalk.UseItem**。无 DLL 不会执行物品操作。不会调用 Lua UseContainerItem，也没有后台自动使用调度。

## 执行顺序
1. 玩家按游戏绑定中的 TaiYangDrinkWalk 动作按键（Bindings.xml）。
2. 仅 SPAM 模式继续；WATCH 只观察。
3. 防重入、75ms 间隔、同一预测 Tick 最多执行一次；有未确认调用时暂不再次派发。
4. DLL `DrinkWalk.Status` 检查 `available==true`。
5. Mana/Energy Tick 必须 LOCKED，且处于距下次 Tick 320–170ms 的窗口。
6. 按配置扫描物品并通过 Tooltip 判断类型；UNKNOWN 不使用。
7. 使用前再次读取背包格子物品 ID、数量和锁定状态；`DrinkWalk.ResolveItem` 后再校验一次。
8. `DrinkWalk.UseItem` 派发原生物品操作。成功返回仅代表**原生派发**，不是服务器接受。
9. BAG_UPDATE 及周期轮询检查原格子数量减少；900ms 未观察到消耗标记 UNCONFIRMED。

## 命令
- `/tsdw` 设置窗口：配置物品、切换 WATCH/SPAM、HUD 控制
- `/tsdw native` 查看 DLL 原生接口是否可用
- `/tsdw action` 查看上次执行状态、错误码和派发/确认次数
- `/tsdw tick`、`/tsdw window`、`/tsdw scan` 保留原功能

## 测试重点
- 首先 WATCH 模式下按动作键，确认不会使用物品。
- SPAM 模式检查 DLL 状态、Tick LOCKED、背包物品配置，再进入预览窗口按键。
- 成功派发要与实际扣除物品分开判断；`/tsdw action` 应显示 DISPATCHED_UNCONFIRMED、CONFIRMED 或 UNCONFIRMED。
- 没有使用物品时，确认逻辑可能因背包物品移动产生误判；本阶段只实现基本数量确认，Aura 佐证及更严格交易判定留到下一阶段。

**重要：** 这是未经过乌龟服实机验证的原生执行测试版；请先用可消耗的普通食物/饮料测试，保留原来能使用的插件 ZIP 以便回退。
