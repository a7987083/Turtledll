# TaiYangDrinkWalk 0.2.0 — 物品管理阶段

适配：WoW 1.12.1（Lua 5.0）。保留 0.1.0 已验收框架，不调用 DrinkWalk.UseItem。

新增：背包 0–4 只读扫描；手工配置食物 food / 饮料 drink / 双恢复 both 规则（物品 ID 或完整名称）；匹配格子、数量和 ID 的聊天报告；规则存档与删除。

## 游戏命令
- `/tsdw` 打开/关闭设置窗口
- `/tsdw status` DLL 与模块状态
- `/tsdw add drink 8079` 添加饮料物品 ID 示例
- `/tsdw add food 物品完整名称` 添加食物名称规则
- `/tsdw add both 物品ID` 添加双恢复规则
- `/tsdw list` 显示规则及序号
- `/tsdw remove 1` 删除第一条规则
- `/tsdw scan` 扫描所有背包并输出匹配结果

**物品规则严格按 ID 或完整显示名称匹配**，不使用 Tooltip 猜测类型；未知物品不自动加入。扫描是只读操作；ManaTick/EnergyTick/Action/HUD/FuBar 仍为占位模块。

测试建议：先确认 /tsdw 与模式切换仍正常，添加背包内真实物品的 ID，执行 /tsdw scan；换包和减少物品后再次扫描，确认位置和数量更新。本版未经实机验证。
