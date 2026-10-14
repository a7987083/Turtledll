# TaiYangDrinkWalk v0.5.6

原始来源：DrinkWalk_v0.5.4_Lua50_Fixed2(1).zip。

## 安装

将本压缩包内 TaiYangDrinkWalk 目录解压到 WoW/Interface/AddOns/，进入游戏后在插件列表启用。

## 依赖

- 乌龟服 / WoW 1.12 (`Interface: 11200`)。
- 运行时需要实现 `TaiYangShenDian` Lua 全局函数的 DLL，以及 `DrinkWalk.Status`、`DrinkWalk.UseItem` API（原注释指出 API24 起支持，API33 应覆盖）。
- 可选 FuBar / !Libs；主功能不依赖它们。

## 用法

- `/tsdw` 或 `/drinkwalk`：插件命令入口。
- `/zchihe`：执行吃喝动作（需按键/宏主动触发）。
- 通过 WoW 按键设置找到“太阳神殿 DrinkWalk”绑定动作。

## 兼容性说明

为了兼容旧配置，保留 `TaiYangDrinkWalkDiagDB`、`TaiYangDrinkWalkFuDB` 保存变量名称，以及相关 Lua 全局接口。
只将安装目录、TOC 文件和主 Lua 文件规范命名为 `TaiYangDrinkWalk`，没有调整其他游戏功能。

未完成游戏实机测试；Lua 语法与 DLL API 在实际客户端中仍需验证。

## v0.5.6：盗贼刀锋冲刺能量光标速度

以原始 v0.5.4 为基线重做（不包含 v0.5.5 的事件采样变更）。点出刀锋冲刺时，能量 HUD 采用天赋和敏捷计算的有效周期，显示数值与移动光标共用该周期；不允许历史 2 秒采样覆盖天赋周期。每帧更新周期以处理天赋/属性变化。

未在游戏内验证；请对比无天赋 2.00 秒与点天赋例如 1.57 秒时的光标完整移动时间。
