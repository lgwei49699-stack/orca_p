# 构建、打包与平台集成

<!-- module-review
{
  "id": "build",
  "source_fingerprint": "sha256:19ef73a5601a19330befb04aee3dfb3f4fb2b4b1a782d3fd9561bdc22605ae2e",
  "reason": "复核现有重建目标集成，并将GMP配方纳入watch；仅Darwin arm64目标固定升级6.3.0避开保留寄存器x18，其他目标保留6.2.1补丁或MSVC预编译库；补真实CMake条件矩阵，不以配置测试代替跨平台运行。"
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

独立重建的 ModelReconstruction、MeshReconstruction、ReconstructionProcess 注册到 `src/libslic3r/CMakeLists.txt` 的现有库目标，候选参数／对比窗口注册到 `src/slic3r/CMakeLists.txt`。worker 直接启动当前应用并走内部命令分流，没有另一个常驻服务或新插件，也不复用 `open`／detached GUI 启动。Alpha Wrap 使用现有 CGAL 头文件依赖；CGAL 版本未变，GMP 平台定向修正如下。

10-06 缺陷优先重建压力测试复现 GMP 6.2.1/macOS arm64 汇编 x18 兼容异常。获用户授权后，[GMP 配方](../../../deps/GMP/GMP.cmake)仅针对 macOS arm64 目标固定使用官方 GMP 6.3.0 源码，SHA-256 `a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898`。按 `CMAKE_OSX_ARCHITECTURES` 选择，未指定时才取 `CMAKE_SYSTEM_PROCESSOR`，因此 Intel→arm64 也升级、arm64→Intel 不升级。Linux 和 macOS x86_64 仍为原 6.2.1 URL/hash/GCC15 补丁，Windows MSVC 仍复制原 `.lib/.dll`；不升级系统 Homebrew、不复制其库到项目，也不改变几何策略。6.3.0 在当前 Apple Clang/static 配置下使用未打补丁的上游源码；未泛化到其他编译器或 universal 构建。复现、正式构建和回归状态见[主记录](../validation/2026-10-04-smart-print-engine.md)最新节。

## 配置与约束

架构、生成器、Release 配置和依赖前缀必须匹配。应用版本与资源包版本独立。Windows 动态库、macOS rpath/zstd 和 Linux 分发标识是不同打包责任，不能只改一个文件名推定完成。

旧 arm64 依赖缓存不会因 `build_release_macos.sh -s`（仅应用）自动升级，须先在对应依赖构建目录执行 `cmake --build deps/build/arm64 --target dep_GMP`，再重新链接应用/测试，或走包含依赖的完整构建。现有 universal 脚本按 arm64/x86_64 分开构建后合并；本次只验证 arm64 产物，没有验证 universal 打包。

## 失败与取消

编译成功、链接成功、打包成功和目标系统可运行分开记录；警告不能冒充构建失败，旧二进制也不能证明新源码通过。用户已有新构建时复用；缺少当前验证所需产物时由 AI 补齐必要目标，详见[验证规范](../validation.md)。

## 修改与验证

修改 CMake／打包脚本先静态检查，再按当前分工构建受影响目标；打包改动需要验证实际打包流程，普通业务改动不默认执行全平台打包。交给用户 GUI 验证的应用路径与资源必须匹配本次构建；跨平台 Python 脚本测试不等于 Windows 安装包验收。

现有测试/工具范围：`scripts/tests/test_bundle_macos_zstd.py`、`scripts/tests/test_gmp_platform_selection.py`。GMP 测试调用真实配方的条件分支，只替换构建注册函数，覆盖 native/cross Apple、Linux 和 MSVC，不下载或编译依赖；运行时稳定性另由项目库自检和重新链接后的几何/修复回归证明。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

重建当前使用本仓库 `build/arm64` 的 Release 配置；交接以 `build/arm64/src/Release/OrcaSlicer.app` 的实际新链接产物和匹配资源为准，不能启动尚未更新的 `build/arm64/OrcaSlicer/OrcaSlicer.app` 打包副本。编译／测试状态和 hash 在[主记录](../validation/2026-10-04-smart-print-engine.md)，GUI 启动命令在[交接清单](../validation/2026-10-05-reconstruction-gui.md)。本轮不因为增加 worker 就宣称 Windows/Linux 打包、进程限制、中文路径或商业发行已经验收。

## 分支现状

本分支保留 OrcaSlicer 品牌和现有打包规则，Windows 动态库/CGAL 处理与用户版并不全同；本次不改依赖链接选项。

## 关联模块

[公共运行时与平台辅助](platform.md)、[测试基础设施与维护工具](test-tools.md)、[中英文与翻译资源](localization.md)、[打印机、耗材与工艺资源](resource-profiles.md)。
