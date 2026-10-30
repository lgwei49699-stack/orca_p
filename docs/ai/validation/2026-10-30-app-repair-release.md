# App 模型检查与高级修复合入 Linux 发布分支

## 当前执行状态：补合 Cura V1 首层空层误报修复

- 用户已授权推送全部已审核提交，并将遗漏的 Cura V1 修复合入发布分支后重新打包。Orca `master/c0ccf31b22` 已推送 Codeup，Slicer `0f8191b`、主服务 `9edc40acc` 已推送各自测试集成分支；本轮未合并服务 develop/master，也未部署。
- 发布目录仍为 `orca-slicer`，基准 `linux/release2_3_2 / 2b7c2b197`；由最新 `origin/master / c0ccf31b22` 合入一笔已审核提交。原有未跟踪文档保留；master 工作区的 OBJ 解析修改不纳入。
- 业务范围仅补入 `GCode::collect_layers_to_print` 的 Cura V1 实际垫层接触锚点与计划首层判断，以及对应测试。共享 GUI/CLI 规则一致；真实缺路径和后续空层保护保留，不修改支撑、模型路径或打印参数。
- 当前步骤：业务源码自动合并；6处冲突均为模块审核头，已保留发布说明并按目标指纹更新。OrcaSlicer/fff_print_tests 的当前目录 arm64 Release 构建退出0，35模块全量检查和空白差异检查通过，已完成垫层/层数专项、64场景普通切片、57阶段CLI和海绵宝宝/勺子/向日葵6组正反例；先前 master 的历史通过不代替本次执行。
- 新证据目录 `build/diagnostics/cura-raft-release-20261030/`。验证通过后推送发布分支，以唯一新标签 `manual-build-linux-20261030-02` 触发构建；旧标签不移动。现有工作流会自动取消仍在运行的上一轮 Linux 打包，以新提交重建。
- GUI 手工验收本轮待执行：新版 GUI 对 Cura V1 垫层开启/关闭分别切片，检查正常间隙不误报、真实缺层仍提示、保存重开不改变设置；来源 master 的具体模型与步骤见 [Cura V1 修复记录](2026-10-27-raft-bottom-empty-layers.md)。目标 Linux 包及 App 真机验收仍待构建后执行。

### 补合后的实际验证结果

- arm64 Release / MeshLib OFF，当前目录 OrcaSlicer 与 fff_print_tests 构建成功。`[SupportMaterial][Raft],[LayerCount]` 共91用例、52,204断言通过；64普通切片、57阶段CLI全部通过。
- 海绵宝宝与勺子各自原配置/无垫层对照共4组退出0、有G-code且无空层告警。向日葵0.32/0.4 mm首层仍分别报1.57～1.99 / 1.57～2.07 mm真实缺层，退出156、无G-code。
- 真实负例首次重放失败是历史 `/tmp` OBJ/配置已丢失（253），未到切片。随后使用已保存GUI 3MF；该自定义工程直接CLI运行因预设兼容元数据失败（239），将嵌入参数直接作为外部预设又因 `from=project` 被CLI拒绝（251）。最终按CLI既有格式显式加载同一机器/工艺/耗材，补齐type/from/system与同名兼容元数据，只分别设置首层厚度0.32/0.4；原3MF几何/变换不改、未使用force-machine/no-check，负例恢复预期。失败日志和最终运行均保留，未修改业务代码绕过检查。
- 应用SHA256 `cafda9be2e6ce8faa7c8df9f003569e3760a95f8d3d99f007645934527e8ed18`；测试程序SHA256 `d11a11605986fadb1a1df14b590150588bb348f78c8501832f824583133d993d`。命令、日志及源码身份在本轮本地证据目录。
- 相对合并前仅GCode.cpp的误报判断与对应测试新增；App基础修复、特征分析、局部层高兼容、Linux工作流/版本/依赖保持发布分支内容。6处文档审核头冲突已解决，默认GUI/CLI共用同一判断。本轮未重新执行GUI交互或实印，不宣称目标Linux已验收。

## 上一批 App 合并记录

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
