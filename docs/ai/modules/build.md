# 构建、打包与平台集成

<!-- module-review
{
  "id": "build",
  "source_fingerprint": "sha256:23991a6cde4bfd2d1f045dff406803096614a601f595408c546d0099e495e290",
  "reason": "按 2026-10-04 用户约定补齐 GUI／CLI 同步检查、必要构建与测试分工、GUI 验证交接；只改维护规则，源码指纹及既有测试证据未变。"
}
-->

## 职责与边界

本模块负责构建、打包与平台集成。归属以[模块清单](../modules.json)的 `build` 为准。

## 入口与调用

- [CMakeLists.txt](../../../CMakeLists.txt)：根构建选项、平台依赖与子目标装配；由配置阶段进入。
- [CMakeLists.txt](../../../src/CMakeLists.txt)：装配应用可执行目标、链接依赖和平台资源。
- [version.inc](../../../version.inc)：定义应用名称、标识及版本；由 CMake 引入，产品版本与资源包版本分开维护。
- [build_release_macos.sh](../../../build_release_macos.sh)：macOS 的依赖、架构、构建配置和打包命令入口；按[验证规范](../validation.md)选择必要目标并确认脚本的依赖／打包范围。

## 核心流程

根 CMake 配置依赖与目标，src 子目标链接 libslic3r、GUI 和开发工具；平台脚本组织依赖、应用与打包，dev-utils/platform 提供图标、manifest、plist 和桌面集成。deps/deps_src 是供应链边界。

## 配置与约束

架构、生成器、Release 配置和依赖前缀必须匹配。应用版本与资源包版本独立。Windows 动态库、macOS rpath/zstd 和 Linux 分发标识是不同打包责任，不能只改一个文件名推定完成。

## 失败与取消

编译成功、链接成功、打包成功和目标系统可运行分开记录；警告不能冒充构建失败，旧二进制也不能证明新源码通过。用户已有新构建时复用；缺少当前验证所需产物时由 AI 补齐必要目标，详见[验证规范](../validation.md)。

## 修改与验证

修改 CMake／打包脚本先静态检查，再按当前分工构建受影响目标；打包改动需要验证实际打包流程，普通业务改动不默认执行全平台打包。交给用户 GUI 验证的应用路径与资源必须匹配本次构建；跨平台 Python 脚本测试不等于 Windows 安装包验收。

现有测试/工具范围：`scripts/tests/test_bundle_macos_zstd.py`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支保留 OrcaSlicer 品牌和现有打包规则，Windows 动态库/CGAL 处理与用户版并不全同；本次不改依赖链接选项。

## 关联模块

[公共运行时与平台辅助](platform.md)、[测试基础设施与维护工具](test-tools.md)、[中英文与翻译资源](localization.md)、[打印机、耗材与工艺资源](resource-profiles.md)。
