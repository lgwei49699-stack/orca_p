# 测试基础设施与维护工具

<!-- module-review
{
  "id": "test-tools",
  "source_fingerprint": "sha256:f3150c06dd8b0614e946a8210590a4540c36dad1b1c57efea5144806a03b721e",
  "reason": "按用户最新要求精简说明，断言首步标题无版本括注且删去恢复保护段落；中文三步顺序、占位符、默认包三项内容及不执行安全设置命令的检查保留。生产构建与切片源码不变。"
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

`test_macos_build_compatibility.py` 使用隔离目录、真实 clang/lipo/签名工具，替换主应用和上游库
耗时构建后执行实际构建入口；包含 `-b` 缓存一致性、资源软链接保护/其他用户可读权限、
双架构库和原依赖缺失。
告警能力用最小 C/C++ 工程配置/编译，验证不支持时不传选项、C 源不接收 C++ 特有选项。
另在配置阶段独立模拟编译器 ID、版本和能力，覆盖 8 个组合及 `VERSION_GREATER 15` 边界，
不依赖宿主版本来断言支持分支。每组用独立构建目录，检查 C/C++ 命令均存在及选项归属；
不编译模拟的选项，因此不要求旧工具链实际支持新告警，也不等于旧工具链实测。
原生脚本用例与完整应用构建分别记于 [Mac 验证记录](../validation/2026-10-08-macos-runtime.md)。

`test_bundle_macos_zstd.py` 的两种应用名 DMG 用例验证首次启动说明逐字匹配模板替换结果，
检查仅中文三步、第一步标题为“开启任何来源”（无版本括注）、系统设置确认及无恢复保护段落；在待打包应用旁放置校验工具副本，
不传 `--include-validator` 时要求镜像顶层可见内容仅为主应用、Applications 和说明。
打包子进程 PATH 中的 spctl/xattr/sudo
替身记录调用并失败，要求没有调用记录，防止把说明命令变成自动操作；不实际关闭 Gatekeeper，
不把内容及签名完整性检查当成目标系统的首次启动放行验证。

筏层 `[EndpointOverlap]` 使用 `tests/data/raft_geometry/benchy-stepped-raft.json` 捕获轮廓，
不依赖完整用户 3MF 或本机路径。先证明旧搭墙延伸会被安全裁断，再检查限制后的路径、
实际宽度/厚度/单位长度流量及恢复默认输出；CLI 另用用户同参模型验证，不以二维夹具代替
完整切片。专项结果与 GUI 待验见[筏层主记录](../../../doc/print_settings/support/raft_geometry_trial_progress.md#2026-10-08-当前调查ep3-plus-复杂轮廓断开与平滑对照)。

`[SlicedExport]` 的多盘回归使用临时 G-code 和 3MF，断言被跳过盘不残留 G-code/MD5/切片元数据/关系引用或本机绝对路径；model_settings 仅保留其编号空槽。禁用盘有/无缓存、未知状态、纯 G-code 盘、全部禁用、单盘选择和普通工程分开验证。所有成功包均经 `load_gcode_3mf_from_stream` 读回，包括首盘禁用和单独选择第二盘，避免只查 ZIP 文件名而遗漏索引错误。

当前盘发送补充用例使用已解析的明确盘号，以首/次盘、其他盘三种缓存状态、单盘/全部盘组成12个组合：单盘仅含目标 G-code，全部盘缺结果必须拒绝，成功包盘编号仍可读回。该用例验证共享导出合同，不直接执行 GUI `send_gcode` 或网络上传。

`[LayerCount]` 显式生成两个不同对象和额外副本，完整切片后清除指定层的挤出集合，保留计划层数，通过擦料塔宽度参数变化使工具选择重新计算，断言 infill 仍完成。另覆盖仅支撑尾层；逐层须合并共同 Z，逐对象须重复副本层数。双材料夹具显式设置完整材料/喷嘴数组，断言两个工具与擦料塔实际启用且换料数大于零，不以开关值代替覆盖。输出解析以实际换层标签为准，分别核对 header/footer 及开始/结束/每层的 total_layer_count；作用域恢复资源目录和删除测试自身临时文件。诊断对照脚本同时支持普通与 BBL 风格换层标签；脚本断言失败不能误记成切片业务失败。具体红/绿结果、同配置龙模型运动序列/配置哈希和 GUI 待验见[层数记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-输出层数一致性修正)。

`[PrintApplyCache]` 必须先对原模型 apply 建立缓存，再同时替换网格/体积身份与实例变换，完成切片/G-code 后重复 apply；只打开已修好工程无法覆盖首次导出问题。包含未变化的另一体积，有/无分层覆盖，新建 Print 对照边界缓存以及真实参数/位置变化继续失效。夹具不自动摆放，分层必须带 layer_height，导出枚举映射完整，作用域恢复资源目录及清理临时文件。真实龙模型生命周期探针另保存修前/后 apply 状态与运动指令对照，不以合成用例替代 GUI 验收。

导出 `[SlicedExport]` 使用临时目录和进度回调定向删除测试源 G-code，检查失败不替换旧目标、普通未切片工程不受影响。`[FloatingWarning]` 先完整切片健康方块，再注入合成截面和规范嵌套的挤出岛，只使支撑检查失效以验证真实告警；四种模式覆盖无路径/墙/GapFill/线宽相交，不开放生产私有接口。真实龙模型另用同配置工程实跑并比较运动指令，合成用例不能代替模型级证据。结果与旧摆放夹具失败分列于[本批记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-悬空微区与切片包导出修正)。

大模型专项：`test_model_reconstruction` 测共享预算分档边界、低/未知 RAM、显式覆盖和回调保留；`test_mesh_reconstruction` 测不能解除输入硬上限、预闭合不用较低输出预算。CLI 脚本在独立证据目录生成 300 万重复面 STL（约 143 MiB），验证包含上限、旧文件门槛移除、显式超时；另用稀疏文件验证超面数和超过 256 MiB 的受控拒绝。重复面仅覆盖读取/清理协议，不能代替真实 180 万面龙模型的时间、内存、实际导出复检记录。

修改文档工具时，在临时 Git 仓库测试部分暂存、分支切换、文件增删改名、坏映射、共享参数依赖及快照缓存隔离。路径匹配优化必须与原通配规则等价；不能借缓存跳过内容哈希。AI 负责测试目标的必要构建和自动化／CLI 测试；业务测试执行前按 [验证规范](../validation.md) 核对或补齐当前构建，不能运行旧程序声称新代码通过。

现有测试/工具范围：`scripts/tests/test_check_ai_module_docs.py`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

重建自动化和 CLI 结果统一回填[主记录](../validation/2026-10-04-smart-print-engine.md)，交互对比、采用／撤销／保存重开见[GUI 交接](../validation/2026-10-05-reconstruction-gui.md)。首版明确没有诊断试切片；报告 `trial_slice=not_run` 及 `print_ready=false` 是契约的一部分，不能将这些测试通过写成完整商用或打印质量验收。`[ReconstructionWorkerGuard]` 通过独立 `reconstruction_worker_test_helper` 检验正常销毁、错误父身份、worker 自身超时/RSS 与父退出；不在测试主进程触发退出或收紧内存限制。显式 `[ReconstructionIntegration]` 需 `ORCA_RECONSTRUCTION_BINARY` 指向同源码应用，验证非均匀缩放/镜像后的候选、局部/物理复检和原件不变。

## 关联模块

[构建、打包与平台集成](build.md)、[应用入口与 CLI](cli.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
