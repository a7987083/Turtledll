# 发布记录

本目录用于保存版本说明与兼容性清单，不用于提交编译生成的 DLL 二进制。

建议每次发布记载：

- DLL 构建 Commit、x86/PE32 校验及 SHA256。
- DLL Lua API 版本和旧插件回归结果。
- 各 AddOn 的版本、所需 DLL API 版本、已验证的 WoW 客户端版本。
- 已知兼容性限制与回滚版本。

构建产物通过 GitHub Actions Artifacts 或 GitHub Releases 发布。
