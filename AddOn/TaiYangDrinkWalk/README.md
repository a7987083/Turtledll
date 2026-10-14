# TaiYangDrinkWalk 0.5.1 — /zchihe 宏入口与 DLL 原生执行

WoW 1.12.1 / Lua 5.0。单入口：游戏宏内容为 `/zchihe`。不注册游戏快捷键，不提供绑定或解绑操作，已删除 WATCH/SPAM 模式切换和执行限制。

## 执行链

1. 玩家主动连续点击动作条宏 `/zchihe`；插件不会自行定时使用物品。
2. 必须加载支持 `DrinkWalk.Status`、`DrinkWalk.ResolveItem`、`DrinkWalk.UseItem` 的 TaiYangShenDian DLL。
3. Mana/Energy Tick 状态必须为 LOCKED，宏点击发生于预测 Tick 前 320–170ms。
4. 从配置的物品 ID/名称查找食物或饮料，通过 Tooltip 分类；重新核对物品 ID、数量和锁定状态。
5. DLL 先 ResolveItem，再 UseItem；原生派发成功**不是服务器确认成功**。
6. 75ms 最小 DLL 尝试间隔；每个预计 Tick 最多两次；派发后先等待最多 90ms 的初步确认，期间不再次派发。
7. 依据 BAG_UPDATE 所见物品总数减少、PLAYER_AURAS_CHANGED 所见新食物/饮料 Aura 两种证据分别判断。本阶段任何一种证据出现即可标记确认，另一种不强制要求出现。
8. 未在初步窗口看到证据时才可能第二次尝试；最多两次不保证一定喝成功。

## 操作

`/tsdw` 设置、`/tsdw set 8079, 魔法晶水` 配置物品、`/tsdw scan` 扫描、`/tsdw tick` 观测、`/tsdw window` 时间窗口、`/tsdw native` DLL 状态、`/tsdw action` 查看派发与确认状态。

创建游戏宏：

```
/zchihe
```

## 已知风险与验证范围

- 90ms 是客户端观察超时，不代表服务器处理完毕。极端网络延迟可能使第二次派发发生在第一次的结果返回前；本版采用每 Tick 两次上限降低风险，而非消除此风险。
- Aura 依赖原版的 GameTooltip:SetPlayerBuff 启发式文本识别；特殊食物/饮料或语言包可能导致 Aura 无法识别。
- 仅比较本次派发前后的物品总量和新 Aura；其他来源的物品变化或相似增益仍可能误判。BAG/Aura 具体证据会在诊断中记录。
- 当前不实现自适应 Lead / Blade Rush 的完整参考模型。测试前请确认 Tick 锁定、物品配置和 DLL 状态。
- 本项目 GitHub Actions 只负责打包，不等同于 Lua 5.0 游戏内运行验收。
