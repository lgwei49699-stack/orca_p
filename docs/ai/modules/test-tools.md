# 测试基础设施与维护工具

<!-- module-review
{
  "id": "test-tools",
  "source_fingerprint": "sha256:8cfb4ecf9767192d832939cd48b6cadb51635c51a639a560495cf7cb359db1a7",
  "reason": "新增三项边界预闭合核心测试及CLI默认/关闭对照，覆盖薄壳复现、闭合/贯通孔不变和预算/无效几何回退；保留GMP平台选择与旧修复回归，外部同配置试切片不冒充自动可打印性验收。"
}
-->

## 职责与边界

本模块负责测试基础设施与维护工具。归属以[模块清单](../modules.json)的 `test-tools` 为准。

## 入口与调用

- [CMakeLists.txt](../../../tests/CMakeLists.txt)：注册 Catch2 测试子目录和目标；SLA 目录存在不等于在此启用。
- [check_ai_module_docs.py](../../../scripts/check_ai_module_docs.py)：模块清单与 Git 工作区/暂存/提交快照检查。
- [test_check_ai_module_docs.py](../../../scripts/tests/test_check_ai_module_docs.py)：临时 Git 仓库的文档工具回归。
- [test_cli_model_reconstruction.py](../../../scripts/test_cli_model_reconstruction.py)：针对已有应用创建隔离 STL 夹具、逐例目录和独立重建协议审计；保存二进制／输入 hash、stdout/stderr、报告及汇总，不构建、不切片、不打印。
- [test_gmp_platform_selection.py](../../../scripts/tests/test_gmp_platform_selection.py)：在临时 CMake 脚本中执行 GMP 配方、捕获 ExternalProject/复制参数，验证目标架构对应的版本、校验值及旧补丁/预编译路径；不修改真实构建目录，不等于目标系统编译运行。

## 核心流程

Catch2 公共配置连接 libslic3r_tests、fff_print_tests、slic3rutils 等目标；Python／CLI 工具组织隔离输入、输出与结果审计。模块文档检查独立读取 Git 快照，不调用切片、网络或 CodeGraph。

现有 C++ 测试源统一归本模块维护基础设施；各业务模块在清单 tests 中依赖相关用例，所以修改用例也会触发对应业务文档复核。scripts 平铺目录逐文件登记，新脚本不会自动归类。

模型修复 `[ModelRepair][Diagnostics]` 使用作用域日志接收器，恢复原日志级别，不改生产接口或注入测试策略。验证阶段耗时为非负、同一次调用 ID 一致及唯一汇总，并覆盖成功/拒绝/异常和资源超限原因；超限样例还必须证明后续分支继续且原网格未被失败候选修改。运行记录与 GUI 手测见[必要优化记录](../validation/2026-10-04-smart-print-engine.md)。

独立重建三个 C++ 源已由 `tests/libslic3r/**` 归属本模块，并在 model-repair 的 tests 中建立业务依赖：`[MeshReconstruction]` 覆盖缺陷三角形汤、精确清理计数、大开口／多壳／自交／退化重复面／薄体／孤立三角形、有界三档尝试、偏差只告警及拒绝原件不变；最终浮点输出仍须完整七项通过，合法把手／内腔可以接受，反向独立壳与浮点塌陷必须拒绝。`[ModelReconstruction]` 覆盖对象适用性、身份、取消及失败事务；`[ReconstructionProcess]` 覆盖 argv／退出码、快速退出竞态、启动拒绝、取消、超时、资源限制与进程回收。平台专用用例与跳过项须分别报告，测试声明不等于所有平台已通过；压力测试异常不能被一次重跑通过覆盖。

`[BoundaryClosure]` 新增 3 用例/72 断言，检验缺面方块恢复实体、反向面朝向、关闭后薄壳、闭合输入/贯通孔/相交闭壳不受补面影响，以及平面/开放自交/超预算环安全回退。CLI 新增关闭选项场景，并校验默认选项通过协议生效及体积差异；总场景数量与实际运行结果以主记录为准。外部诊断切片以同一已保存 GUI 工程保留有效配置和变换，3MF 输出必须共享精确相同顶点，不能因测试夹具把各三角形顶点拆开而误判业务切片故障。

CLI 脚本要求显式 `--binary` 和父目录已存在、自身尚不存在的 `--output-dir`；保存 `fixture-manifest.json` 与 `summary.json`，复核源文件、既有产物和二进制未改变、不产生旧 `result.json`。测试健康 ASCII／二进制／中文路径、彩色 STL 拒绝、开口／多实体、非有限坐标、截断、参数白名单、风险确认及资源预算；超限场景可能先被资源守卫拒绝，不能因此声称已观测到超时，实际超时另由进程测试覆盖。

## 配置与约束

测试源码、测试程序、应用与资源配置四者身份应匹配，聚焦结果包含于完整结果时不重复计数。docs/ai/modules.json 是范围清单，新增文件必须分配模块或说明排除原因。

## 失败与取消

0=文档检查通过，1=需复核/缺失/未归属，2=工具或清单错误。脚本不生成正文、不自动记录审核、不暂存、不提交；日期和 mtime 不参与指纹；旧 blob 早于当前换行属性时，字节相同保持原身份，修改后按 Git 当前规范化规则计算。

## 修改与验证

修改文档工具时，在临时 Git 仓库测试部分暂存、分支切换、文件增删改名、坏映射、共享参数依赖及快照缓存隔离。路径匹配优化必须与原通配规则等价；不能借缓存跳过内容哈希。AI 负责测试目标的必要构建和自动化／CLI 测试；业务测试执行前按 [验证规范](../validation.md) 核对或补齐当前构建，不能运行旧程序声称新代码通过。

现有测试/工具范围：`scripts/tests/test_check_ai_module_docs.py`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

重建自动化和 CLI 结果统一回填[主记录](../validation/2026-10-04-smart-print-engine.md)，交互对比、采用／撤销／保存重开见[GUI 交接](../validation/2026-10-05-reconstruction-gui.md)。首版明确没有诊断试切片；报告 `trial_slice=not_run` 及 `print_ready=false` 是契约的一部分，不能将这些测试通过写成完整商用或打印质量验收。`[ReconstructionWorkerGuard]` 通过独立 `reconstruction_worker_test_helper` 检验正常销毁、错误父身份、worker 自身超时/RSS 与父退出；不在测试主进程触发退出或收紧内存限制。显式 `[ReconstructionIntegration]` 需 `ORCA_RECONSTRUCTION_BINARY` 指向同源码应用，验证非均匀缩放/镜像后的候选、局部/物理复检和原件不变。

## 关联模块

[构建、打包与平台集成](build.md)、[应用入口与 CLI](cli.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
