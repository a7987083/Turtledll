# Turtledll

原版 TaiYangShenDian DLL 与 WoW 1.12.1 AddOn 开发仓库。

## 分支约定

- `main`：稳定基线，不直接开发。
- `dll-dev`：C/C++ DLL 功能开发。
- `addon-dev`：统一维护所有 Lua AddOn，每个插件使用 `AddOn/<插件名>/` 独立目录。
- `编译dll`：已验证的 x86 DLL 编译工作流所在分支。

## 目录

- `src/`：DLL 源码。
- `third_party/`：MinHook 等编译依赖。
- `build/`：DLL 构建脚本。
- `tests/`：回归测试。
- `AddOn/`：Lua 插件目录，参见 `AddOn/README.md`。
- `releases/`：版本说明及兼容性清单。
- `.github/workflows/`：自动构建及插件打包。

**兼容性约定：** 插件必须标注所需 DLL Lua API 版本（例如 API33）。编译通过不等于游戏内兼容性已验证。
