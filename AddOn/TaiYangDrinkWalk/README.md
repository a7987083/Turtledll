# TaiYangDrinkWalk 0.1.0-framework

首阶段：WoW 1.12.1 / Lua 5.0 插件骨架，**不含走喝动作**。

已实现：核心加载顺序、/tsdw 与 /tsdwdiag、DLL Core.Version 检测、设置 UI、WATCH/SPAM 偏好保存、动作绑定安全占位。

未实现：食物/饮料识别、背包扫描、Mana/Energy Tick、时间窗口、原生 UseItem、成功确认、资源 HUD、FuBar 集成。WATCH/SPAM 只是占位偏好，不触发任何物品使用。

安装：将 TaiYangDrinkWalk 文件夹放到 Interface/AddOns/，禁用原 TaiYangDrinkWalk 和 TaiYangDrinkWalkDiag。输入 /tsdw 打开，/tsdw status 或 /tsdwdiag 查看状态。

DLL 未加载时仍可打开设置；快捷键动作只显示未接入提醒。**未完成 WoW 游戏内实机测试。**