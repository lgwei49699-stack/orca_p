# App 模型检查与高级修复合入 Linux 发布分支

## 当前执行状态

- 用户授权：将已提交的 Orca App 修复改动合入 `linux/release2_3_2` 并从该分支打包；本轮不推送其他服务。
- 目标目录：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer`，合并前 HEAD `52a60762e`，已 fetch 确认与 `github/linux/release2_3_2` 一致。
- 来源：`orca-slicer-soft` 的提交 `325135315abc5d7a091af70de8ef5df492696112`。共同基准 `aa39c82eb`，待合入仅此一笔提交；来源工作区的未提交改动不进入此次合并。
- 目标原有未跟踪文件 `2026-10-27-tree-support-fill-isolation.md` 保留，哈希写入本地证据。目标未提供 CodeGraph MCP，CLI 已安装但此工作目录无索引，本轮按模块文档与源码核查，不初始化索引。
- 当前步骤：业务源码已自动合并，11份模块文档的审核头及测试说明冲突已逐项整合；发布分支已有盘外姿态、局部计划进度和精简导出兼容处理保留，工作流/版本/打包脚本未改。目标目录已启动 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2`，日志 `build.log`，构建退出0，合并后的回归全部通过，详见下文。来源分支的验证见 [App 修复记录](2026-10-30-app-basic-repair.md)，不能代替合并后的执行结果。
- 打包入口：`.github/workflows/build-linux.yml` 的 `manual-build-linux-*` 标签；Ubuntu 24.04 x86_64、MeshLib ON，保留当前版本 `2.3.2.3-Release`。本地回归配置 macOS arm64 Release、MeshLib OFF，不代表 Linux 包运行验证。
- 本地证据：`build/diagnostics/app-repair-release-20261030/`。下一步合并后核对模块正文和指纹，测试通过后推送发布分支及新标签。

## GUI 与服务验证边界

App 的 basic 模式属于显式 CLI 请求，GUI 保持 full 默认，未增加界面选项。GUI 交互和 App 联调尚未在本轮执行：新包可用后，分别验证健康模型直接通过、开放网格进入一次高级修复、复杂模型失败保留原件，以及 GUI 普通导入/切片。对应业务检查与来源分支的自动化证据见上述 App 修复记录。打包不自动部署业务服务或下发打印。

## 合并后的验证与结果

已按目标快照复核 35 模块、翻译及空白差异，均通过；只有 `OrcaSlicer.cpp` 因发布分支原有盘外姿态处理与来源提交不同，其余本次源文件/测试与已审核提交一致。合并源码身份记录在 `merged-source-identity.json`，不跨目录复用旧二进制。

构建完成后顺序运行原生修复专项（ModelInspection/ModelRepair/AppBasic/ModelStageReport/MeshReconstruction/ModelReconstruction/MeshBoolean）、阶段 CLI（明确 MeshLib unavailable）、普通固定位置切片、越界分盘与局部层高 CLI。每项命令、退出码、日志和新程序 SHA256 保存至本轮证据目录。先前验证器在合并冲突仍标记未解决时拒绝运行，解决并暂存文档冲突后重跑通过；这不是源码构建失败。

### 本地已通过

- 目标目录 macOS arm64 Release 构建 `OrcaSlicer` 和 `libslic3r_tests` 成功；MeshLib OFF。
- 原生专项：215 用例、8971 断言通过。
- 阶段 CLI：57/57，包括 basic 检查/高级/混合部件保留、默认 full 与 SDK 不可用拒绝。
- 普通固定位置切片：64 场景通过；越界分盘：184 次 CLI 运行通过，核对 JSON 投影及 3MF 未改变；局部层高/真实FIFO进度/精简导出：40 场景通过。
- 应用 SHA256 `19927e042759170ad136d10ff548fefe73288f4db4e73fbea724ef54c664e670`，测试程序 SHA256 `17f9a19c371247e7fd34017a17e5bcd70c0afbdea2274a5894a02fddf40d9c04`，命令和退出码见本地 `verification.json`。
- 未改 Linux 工作流、版本和打包选项；原有未跟踪文档哈希未变。未执行本轮 GUI/App 真机及目标 Linux 包验证。

### 发布安排

本地审核和回归通过后提交中文合并提交，推送 `github/linux/release2_3_2`，在该提交创建并推送 `manual-build-linux-20261030-01`。当前远端未存在此标签；推送前再检查工作流无进行中/排队任务，以免既有并发策略取消其他打包。该标签触发新的 Linux/MeshLib ON 包内回归及 Release；这里不提前声称远端构建成功。实际运行链接/状态另保存本地 `github-run.json`。
