# TaiYangShenDian AddOns

每个 WoW 1.12.1 插件使用独立目录：

```text
AddOn/
  PluginName/
    PluginName.toc
    PluginName.lua
    README.md
```

- 将真实插件文件放入各自目录，避免把所有插件混合在一起。
- 每个插件在自己的 README.md 写明插件版本、要求的 TaiYangShenDian DLL API 版本（当前基线 API33）、功能及测试记录。
- 插件打包时会保留插件目录层级，便于放入 WoW 的 Interface/AddOns。
- 插件代码与 DLL 源码分别在 addon-dev 与 dll-dev 开发；仅经验证的结果才合并到 main。

本目录没有预置示例插件，以免把未测试的占位插件误认为正式版本。
