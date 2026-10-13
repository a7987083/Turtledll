# TaiYangDrinkWalk 1.0.0

适用：WoW 1.12.1 / Lua 5.0，原始 TaiYangShenDian DLL API33（最低接口 API24，需提供下列三项）。

## 功能
- 查询 DLL `Core.Version` 与 `DrinkWalk.Status`，显示接口是否可用。
- 手动指定背包编号（0–4）、格子编号（1–36，实际以容器容量为准）。
- 手动调用 `DrinkWalk.ResolveItem` 验证格子物品。
- 手动调用 `DrinkWalk.UseItem` 派发物品使用请求。
- 背包、格子设定保存至 `TaiYangDrinkWalkDB`。

**重要限制：** 当前 DLL 只实现显式原生调用，不支持自动找水、自动走喝、自动调度或自动验证使用结果。本插件不添加后台循环、Hooks 或自动调用。原生 API 返回 `DISPATCHED_NATIVE_PRIMITIVE_UNCONFIRMED` 仅表示已派发，不代表服务器确认成功；应结合背包数量或 Drink Aura 判断。

## 安装
将完整 `TaiYangDrinkWalk` 文件夹复制至 `World of Warcraft/Interface/AddOns/`，保证 DLL 已加载且 Lua 全局 `TaiYangShenDian` 已注册。

## 命令
- `/tsdw` 或 `/drinkwalk`：显示/隐藏设置面板。
- `/tsdw status`：打印 DLL 状态。
- `/tsdw resolve`：解析当前保存的背包/格子。
- `/tsdw use`：手动使用当前保存格子的物品。
- `/zchihe`：手动调用使用接口。

**风险提醒：** `UseItem` 会尝试使用指定格子物品，测试前请确认确实是目标饮品，避免误用消耗品。

## 尚需游戏实测
检查插件可加载、UI 可显示、命令注册、无 DLL 时友好报错、有效格子解析、UseItem 的服务器实际效果、与其他旧插件共存。GitHub Actions 打包通过不等于游戏内功能验证。
