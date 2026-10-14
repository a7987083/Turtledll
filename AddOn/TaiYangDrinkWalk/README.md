# TaiYangDrinkWalk v0.6.3

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

此前 OnEvent 重构阶段经用户反馈实机测试正常；后续性能与稳定性优化仍需逐阶段回归。DLL API 依赖实际客户端与 DLL 版本，CI 语法通过不等于客户端兼容性得到全面验证。

## v0.5.6：盗贼刀锋冲刺能量光标速度

以原始 v0.5.4 为基线重做（不包含 v0.5.5 的事件采样变更）。点出刀锋冲刺时，能量 HUD 采用天赋和敏捷计算的有效周期，显示数值与移动光标共用该周期；不允许历史 2 秒采样覆盖天赋周期。每帧更新周期以处理天赋/属性变化。

未在游戏内验证；请对比无天赋 2.00 秒与点天赋例如 1.57 秒时的光标完整移动时间。

## v0.6.3：维护和稳定性

- Lua 5.0 / WoW 1.12 CI 语法检查；OnEvent 事件处理拆分，背包扫描合并，槽位 ID 校验。
- HUD 不变项更新缓存，Tooltip 成功分类缓存；保留逐帧 Spark、现有 Tick 公式与手动操作。
- FuBar 右键菜单仅保留必要入口；独立插件安装目录不依赖 FuBar。
- 保留现有 SavedVariables 名称，不迁移用户配置。

请重点复测换图、死亡复活、/reload、切换能量/Mana、FuBar 图标恢复。

## v0.6.3：生命周期防护

- `/reload` 恢复的 `VERIFY` 相位只允许首次 `PLAYER_ENTERING_WORLD` 保留，其他进世界事件清除过期相位。
- 复活和进入世界时重置 Energy 采样锚点，避免复用过期采样。
- Mana/Energy Tick 数学公式、绿色窗口和 DLL API 保持不变。

注意：这些变化经过 Lua 5.0 语法和 CI 打包测试后，仍需在游戏中验证跨地图、复活与重载场景。

## v0.6.3：生命周期模型回归

- 仅保存 LOCKED/VERIFY 且有相位锚点的 `/reload` 候选；恢复时验证角色、状态、时间和锚点区间。
- 自动化事件序列模型覆盖 `/reload` 首次世界进入、重复进入、死亡复活、区域变更与资源切换。
- 模型测试是 Python 状态模型，不运行完整 WoW 客户端；游戏内行为仍需实机验证。

## v0.6.3：稳定版候选发布整理

- 发布 CI 只生成独立插件 ZIP，不再生成重复的 All-AddOns.zip。
- 发布包排除 tests/ 与 Python 编译缓存；CI 校验 ZIP 完整性与无内嵌 ZIP。
- 统一登录、界面、TOC、FuBar 与 README 版本号；优化未知物品诊断文字。
- 继续运行 Lua 5.0.3 检查、静态接口合同及生命周期模型测试。CI 通过不代表乌龟服实机验收通过。
