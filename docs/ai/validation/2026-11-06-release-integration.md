# 2026-11-06 Orca 文档归档与 Linux 2.3.2 集成

## 当前执行状态

- 范围：整理特征分析工作区的文档、参数审计工具与 Thingi10K 下载工具；修正 MeshLib README；审核并合入最新 `linux/release2_3_2`，提交推送并触发 GitHub Linux 打包。
- 基准：重建分支 `codex/meshlib-mac-adaptive-sampling-20261104` / `d949c25a3b`，特征分支 `codex/model-feature-analysis-20261018` / `f7df7e11c8`，发布工作区 `orca-merge-linux232-repair-20261103` / `ed88928e89`。实际合并前再次核对远端。
- 不改范围：独立 `orca-slicer` 克隆、工作区 `result.json`、其他服务的未提交修改、服务器和打印机。
- 当前：README 与报告依赖说明审核完成，35 模块检查通过；特征审计与下载工具 88 项离线单测通过。准备分别提交文档，再合入发布分支；尚未触发本轮打包。
- 下一步：完成模块文档检查和选择性提交，合并后验证源码身份、发布脚本与报告约定，再推送标签。

## Slicer 发布依赖

静态核对 `Print3DSliceService` 的 `feature/f_模型修复独立接入_20261029` 工作区，HEAD `ea34e78`：`ModelRepairWorkflow.readReport` 的本地补丁允许仅重建阶段接受 `binary_stl_count_and_size`，仍检查产物路径、SHA256 与 inspection。对应 `binaryStlIntegrityIsAcceptedOnlyForReconstruction` 测试覆盖检查、高级修复和重建三阶段。

该兼容补丁尚未提交；已提交的旧代码仍要求 `readback_verified=true`，不能直接接收本轮重建报告。用户明确要求本轮只处理 Orca，Slicer 由用户另行提交。因此本轮不提交、推送或运行后端服务，也不把静态核对写成服务部署验收。部署新引擎前需配套发布兼容服务。

## 验证边界

本轮未修改 C++ 引擎。重建分支已有当前源码的 Mac 共享核心 70 项／1538 断言、检查 CLI 122 项、阶段报告 CLI 65 项验证记录，见[准入交付](2026-11-06-reconstruction-admission-handoff.md)。合并后必须核对源码身份才能复用，不以来源分支通过替代不同代码的验证。

GUI 当前内存提示和最新准入相关交接仍按上述记录执行；本轮不新增 GUI 验收结论。Linux 包由 GitHub 构建并执行工作流验收，触发任务不等于打包完成或服务器通过。

## 步骤记录

- 初始审核：发现 MeshLib README 仍描述运行内存监测及未实现资源准入；同步为启动前估算、运行中协作超时／取消，并区分手动成本提示与自动准入阈值。
- 特征工具：88 项离线测试通过；下载测试强制拦截未模拟的网络请求，不下载模型、不修改业务数据。
