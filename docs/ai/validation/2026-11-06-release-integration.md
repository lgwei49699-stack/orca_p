# 2026-11-06 Orca 文档归档与 Linux 2.3.2 集成

## 当前执行状态

- 范围：整理特征分析工作区的文档、参数审计工具与 Thingi10K 下载工具；修正 MeshLib README；审核并合入最新 `linux/release2_3_2`，提交推送并触发 GitHub Linux 打包。
- 基准：重建分支 `codex/meshlib-mac-adaptive-sampling-20261104` / `d949c25a3b`，特征分支 `codex/model-feature-analysis-20261018` / `f7df7e11c8`，发布工作区 `orca-merge-linux232-repair-20261103` / `ed88928e89`。实际合并前再次核对远端。
- 不改范围：独立 `orca-slicer` 克隆、工作区 `result.json`、其他服务的未提交修改、服务器和打印机。
- 当前：README 提交 `62575f2aeb`、特征归档提交 `a664abba39` 均已推送各自 GitHub 分支。发布分支以 `6669a695e6` 合入重建分支，本提交再整合特征归档；7 份模块文档冲突已按发布源码处理，未产生 C++ 冲突或追加算法修改。
- 下一步：最终暂存检查通过后推送 `linux/release2_3_2`，使用 `manual-build-linux-20261106-01` 标签触发 GitHub Linux 打包。标签及任务启动回执保存到本工作区 `build/diagnostics/release-integration-20261106/delivery.json`；不等待完整打包，不把任务启动写成包验收通过。

## Slicer 发布依赖

静态核对 `Print3DSliceService` 的 `feature/f_模型修复独立接入_20261029` 工作区，HEAD `ea34e78`：`ModelRepairWorkflow.readReport` 的本地补丁允许仅重建阶段接受 `binary_stl_count_and_size`，仍检查产物路径、SHA256 与 inspection。对应 `binaryStlIntegrityIsAcceptedOnlyForReconstruction` 测试覆盖检查、高级修复和重建三阶段。

该兼容补丁尚未提交；已提交的旧代码仍要求 `readback_verified=true`，不能直接接收本轮重建报告。用户明确要求本轮只处理 Orca，Slicer 由用户另行提交。因此本轮不提交、推送或运行后端服务，也不把静态核对写成服务部署验收。部署新引擎前需配套发布兼容服务。

## 验证边界

本轮未修改 C++ 引擎。重建分支已有当前源码的 Mac 共享核心 70 项／1538 断言、检查 CLI 122 项、阶段报告 CLI 65 项验证记录，见[准入交付](2026-11-06-reconstruction-admission-handoff.md)。合并后必须核对源码身份才能复用，不以来源分支通过替代不同代码的验证。

GUI 当前内存提示和最新准入相关交接仍按上述记录执行；本轮不新增 GUI 验收结论。Linux 包由 GitHub 构建并执行工作流验收，触发任务不等于打包完成或服务器通过。

## 步骤记录

- 初始审核：发现 MeshLib README 仍描述运行内存监测及未实现资源准入；同步为启动前估算、运行中协作超时／取消，并区分手动成本提示与自动准入阈值。
- 特征工具：88 项离线测试通过；下载测试强制拦截未模拟的网络请求，不下载模型、不修改业务数据。
- 发布工作区第一轮合并：`src/tests/deps/cmake/resources/localization/.github/scripts` 与 `62575f2aeb` 完全一致；已有验收清单的 18 份源码／测试／资源／二进制 SHA256 全部匹配。因此复用已完成的 Mac 引擎测试，不重复编译相同源码。
- `test_package_meshlib_linux`、`test_meshlib_platform_config`、`test_check_ai_module_docs` 共 78 项：75 通过、3 项按平台条件跳过；这是本地工具／配置夹具验证，不是 Linux 真包验收。两份 Linux Shell 脚本语法检查和 35 模块文档检查通过。
- 第二轮合并仅增加历史文档及四份 Python 工具／测试文件。引擎、原生测试、依赖、资源和 GitHub 工作流仍与 `62575f2aeb` 相同；两个新增／扩展工具在组合工作区再次执行 88 项，全部通过。文档冲突保留当前重建及报告合同，旧内存监测／旧 App 检查语义明确标为历史，工具模块指纹按实际组合内容更新。
- GitHub 查询确认此前三次 Linux 标签构建已结束且成功，没有运行中任务会被本次标签触发的同组并发取消；仍以创建标签前再次查询为准。Slicer 本地兼容已核对但未提交，用户另行处理。
