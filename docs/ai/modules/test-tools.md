# 测试基础设施与维护工具

<!-- module-review
{
  "id": "test-tools",
  "source_fingerprint": "sha256:4df29cebc967148cc4aef84913195bd2e1569270858e50a62a27511583b6ad01",
  "reason": "新增可选MeshLib CLI小夹具回归：只调用已构建应用和独立worker，保存身份/逐例报告，检查独立七项完整零缺陷、参数和SDK身份、非法选项、原件与已有输出不变。原Alpha Wrap/检测/高级修复脚本保持原实现并重跑；测试超时不冒充监督成功。保留工作区其他用户工具和文档，提交选择性隔离。"
}
-->

## 职责与边界

本模块负责测试基础设施与维护工具。归属以[模块清单](../modules.json)的 `test-tools` 为准。

## 入口与调用

- [CMakeLists.txt](../../../tests/CMakeLists.txt)：注册测试子目录和目标；SLA 目录存在不等于在此启用。`SLIC3R_GUI_TESTS` 默认关闭，避免普通无桌面测试默认启动 GUI。
- [test_gfd_cloud_config_dialog.cpp](../../../tests/gui/test_gfd_cloud_config_dialog.cpp)：保存/上传共用表单生命周期的独立wx测试，覆盖进度、异步完成、重复提交、忙时取消/关闭、启动/异常/服务端失败、取消结果及迟到回调。结果处理前要求原表单隐藏、模态退栈、父窗口可用；Cocoa原生模态不假定wx禁用标记变化。提交前增加嵌套登录返回检查，Mac arm64共43检查通过，不代表真实云写入或Windows视觉通过。
- [test_gfd_modal_dialog.cpp](../../../tests/gui/test_gfd_modal_dialog.cpp)：独立 wx GUI 回归程序，不链接切片／云端业务，也不下发打印。正常结束、内层提示先结束再结束外层、提前隐藏、嵌套期间结束、关闭取消、进入循环时结束、连续重开，以及 Windows 的 InitDialog 结束路径，均检查窗口状态与退出结果；5 秒看门狗使循环残留明确失败。
- [check_ai_module_docs.py](../../../scripts/check_ai_module_docs.py)：模块清单与 Git 工作区/暂存/提交快照检查。
- [capture_windows_gui_state.ps1](../../../scripts/capture_windows_gui_state.ps1)：Windows 下独立采集 OrcaSlicer 顶层窗口、线程输入与程序身份，辅助区分禁用窗口、隐藏弹窗、输入捕获和消息处理异常。
- [capture_windows_gui_state.cmd](../../../scripts/capture_windows_gui_state.cmd)：双击入口，定位同目录 PS1，启用交互选择进程及采集后打开结果文件夹；保留退出码与错误文字，不自动提权或永久更改执行策略。
- [copy_windows_active_log.cmd](../../../scripts/copy_windows_active_log.cmd) / [copy_windows_active_log.ps1](../../../scripts/copy_windows_active_log.ps1)：双击选择正在使用的日志，以兼容读写共享的只读句柄复制到桌面，保留源程序及原文件。
- [test_check_ai_module_docs.py](../../../scripts/tests/test_check_ai_module_docs.py)：临时 Git 仓库的文档工具回归。
- [test_cli_model_reconstruction.py](../../../scripts/test_cli_model_reconstruction.py)：针对已有应用创建隔离 STL 夹具、逐例目录和独立重建协议审计；保存二进制／输入 hash、stdout/stderr、报告及汇总，不构建、不切片、不打印。
- [test_cli_model_check.py](../../../scripts/test_cli_model_check.py)：沿用既有检测／显式组合高级修复回归，核对默认 v4 精确六字段及显式 `--repair-evidence` 的可选 `boundary`；分别验证开关、非法组合、原字段／退出码和产物一致性，不新增自动修复命令或实际打印任务。
- [test_gmp_platform_selection.py](../../../scripts/tests/test_gmp_platform_selection.py)：在临时 CMake 脚本中执行 GMP 配方、捕获 ExternalProject/复制参数，验证目标架构对应的版本、校验值及旧补丁/预编译路径；不修改真实构建目录，不等于目标系统编译运行。

## 核心流程

`scripts/test_cli_meshlib_reconstruction.py` 针对已有应用及独立sidecar做小夹具回归，不构建/下载SDK、不打印。覆盖候选独立检测与哈希、参数互斥、程序缺失/协议失败、原件/旧输出保护，以及默认Alpha Wrap对照；实际资源失败原因单独记录，不把提前内存拒绝当作超时验证。sidecar本身另有 `tools/meshlib-worker/test_worker.py` 验证参数/有界输入和协议；累计RSS、嵌套取消及父亡回收由现有Process测试覆盖。CLI测试通过不能替代结构保留或实机打印验收。

`gfd_modal_dialog_tests` 补充 MainFrame→子面板→栈上原生／自管模态的关闭回归：模态期间退出被否决，嵌套同样保护，非关联窗口不阻塞；结束模态并退出作用域后允许 owner 正常销毁。本轮 Mac arm64 87 项检查通过；15:29 用户日志进一步验证设备框下两次退出拦截、取消恢复和应用正常析构。嵌套打印设置和 Windows 仍按 [Advancer3 续修交接](../validation/2026-10-09-adv3-custom-feasibility.md) 验收。

Catch2 公共配置连接 libslic3r_tests、fff_print_tests、slic3rutils 等目标；Python／CLI 工具组织隔离输入、输出与结果审计。模块文档检查独立读取 Git 快照，不调用切片、网络或 CodeGraph。

现有 C++ 测试源统一归本模块维护基础设施；各业务模块在清单 tests 中依赖相关用例，所以修改用例也会触发对应业务文档复核。scripts 平铺目录逐文件登记，新脚本不会自动归类。

Windows GUI 采集脚本使用 PowerShell 5.1 与内嵌 C# P/Invoke，默认延迟 8 秒后采集 3 次，方便切回故障窗口；先读窗口及线程输入状态，再逐窗口发送限时 250ms 的 WM_NULL。自动匹配唯一 OrcaSlicer 进程，多进程可显式 `-OrcaProcessId`，双击入口以 `-Interactive` 提供进程 Id 输入；桌面 JSON 记录可执行文件路径/版本/SHA256、顶层窗口标题/位置/owner/启用/可见状态、焦点/捕获句柄及探测结果。脚本不改变 Orca 启用/显示状态、不注入点击、不读取配置、不触发切片或打印；标题可能包含工程名。`-OpenOutputFolder` 仅在全部采样落盘后打开资源管理器，打开失败不丢弃已有报告。中文提示的 PS1 保持 UTF-8 BOM 以兼容 Windows PowerShell 5.1，CMD 使用 CRLF、带引号的绝对脚本路径并禁用 delayed expansion，避免空格或感叹号目录损坏路径。快照不是原子采集，句柄可能在读取中失效；探测返回失败可能是超时、权限或窗口已销毁，不能统一判断为死锁。仅采集拥有顶层窗口的线程输入信息，不包含调用栈或 wx 模态循环状态。运行步骤及判断边界见[下发卡住调查](../validation/2026-10-08-print-ui-freeze-log-analysis.md#第四轮重启恢复与现场采集)。

模型修复 `[ModelRepair][Diagnostics]` 使用作用域日志接收器，恢复原日志级别，不改生产接口或注入测试策略。验证阶段耗时为非负、同一次调用 ID 一致及唯一汇总，并覆盖成功/拒绝/异常和资源超限原因；超限样例还必须证明后续分支继续且原网格未被失败候选修改。运行记录与 GUI 手测见[必要优化记录](../validation/2026-10-04-smart-print-engine.md)。

独立重建三个 C++ 源已由 `tests/libslic3r/**` 归属本模块，并在 model-repair 的 tests 中建立业务依赖：`[MeshReconstruction]` 覆盖缺陷三角形汤、精确清理计数、大开口／多壳／自交／退化重复面／薄体／孤立三角形、有界三档尝试、偏差只告警及拒绝原件不变；最终浮点输出仍须完整七项通过，合法把手／内腔可以接受，反向独立壳与浮点塌陷必须拒绝。`[ModelReconstruction]` 覆盖对象适用性、身份、取消及失败事务；`[ReconstructionProcess]` 覆盖 argv／退出码、快速退出竞态、启动拒绝、取消、超时、资源限制与进程回收。平台专用用例与跳过项须分别报告，测试声明不等于所有平台已通过；压力测试异常不能被一次重跑通过覆盖。

`[BoundaryClosure]` 新增 3 用例/72 断言，检验缺面方块恢复实体、反向面朝向、关闭后薄壳、闭合输入/贯通孔/相交闭壳不受补面影响，以及平面/开放自交/超预算环安全回退。CLI 新增关闭选项场景，并校验默认选项通过协议生效及体积差异；总场景数量与实际运行结果以主记录为准。外部诊断切片以同一已保存 GUI 工程保留有效配置和变换，3MF 输出必须共享精确相同顶点，不能因测试夹具把各三角形顶点拆开而误判业务切片故障。

独立重建 CLI 脚本要求显式 `--binary` 和父目录已存在、自身尚不存在的 `--output-dir`；保存 `fixture-manifest.json` 与 `summary.json`，复核源文件、既有产物和二进制未改变、不产生旧 `result.json`。测试健康 ASCII／二进制／中文路径、彩色 STL 拒绝、开口／多实体、非有限坐标、截断、参数白名单、风险确认及资源预算；超限场景可能先被资源守卫拒绝，不能因此声称已观测到超时，实际超时另由进程测试覆盖。

`test_model_repair_evidence.cpp` 验证只读检测／修复／重建证据投影，区分未执行、完整检测、已知缺陷与不完整，保留已观测阳性和失败原因。高级修复正常结束、完整已知缺陷及准入通过后，真实穿透见证或显式允许封口下的残留开边可以形成建议并跳过Z观察；否则保留全部请求Z平面无有效区域、存在非退化闭圈且无主动删区配置的后备依据。摘要类型、face ID范围/次序、截断搜索、矛盾计数、坏见证及未完成操作均不得误报有效建议；开链、单空层和缺陷总数本身不触发。覆盖参考层位与调用方层位、诊断取消、资源／坐标预算、异常及原件不变；不注册自动编排或候选自动采用测试。

`test_model_inspection.cpp` 的 `[ProperCrossing]` 使用精确非共面交线、极小穿透、共面重叠、点/边接触及健康闭体对照，验证分类不改变既有检测结果。另覆盖0/1对预算及第201个接触之后的穿透：阳性可提前结束存在性分类，但原搜索不完整仍不能作完整建议；阴性必须检查全部保留对，不能把前200个展示样本当全量。分类耗时只断言非负，不以机器相关时间阈值判通过。

`test_mesh_slice_diagnostics.cpp` 的 `[MeshSliceDiagnostics]` 验证观察入口不改变普通切片输出；有效区域入口与 `slice_mesh_ex` 共用转换，切一次即得到原轮廓诊断与 `ExPolygons`。覆盖开放链丢弃、非零补隙、同坐标未焊接点、缺失水平盖，以及变换／模式／闭合半径／偏移／简化／单层／空输入和取消。参考单 Z 观察不是实际层计划或完整工艺试切片，不能宣称模型健康或打印通过。

三个既有 CLI 脚本验证默认为关闭、显式开启与显式 `0` 关闭的协议和产物。`test_cli_model_check.py` 默认严格断言六字段；开启后检查 `boundary.schema_version=2`、穿透摘要、策略及单Z参考观察，以及健康／只检测跳过截面、部分修复和无进展保留语义、参数／输入失败及不支持组合。新增真实高级修复后的cap穿透及删一外侧面的非流形夹具，验证直接建议跳过截面、封口策略默认/0/1对照、同模型输出字节不因策略改变；合法无开边输入不误触发，普通修复/导出/独立重建入口连显式0也拒绝该策略，非法值受控拒绝。`test_cli_repair_acceptance.py` 对照普通 CGAL 报告及导出物；`test_cli_model_reconstruction.py` 对照独立候选身份／文件及只读投影，二者保持原投影版本且不因证据选项额外执行检测。检测／断面不完整、取消或失败不能被投影成必须重建；实际运行、旧 GUI 流程、Windows/Linux 和打印结论分别记录，不由测试代码存在推定通过。

10-15组合完成语义回归：新组合正常无进展返回0但保留unresolved/缺陷，旧默认和显式off仍-25；显式STL转换必须产出并复检result.stl。用拉高Z的确定性非流形夹具触发2000截面预算，分别验证unresolved/partial的新组合非零、旧partial仍0与保留产物一致。核心测试覆盖同一路径；本地维护台Node测试按历史实际argv识别模式，并覆盖check_complete=false但已知几何阻断/有效预检完成的结果文案，不改其他任务和历史退出码。

本地维护台另以Node VM执行实际前端生命周期函数，覆盖CLI成功/失败/取消/超时/中断后的新计划和按钮恢复、首次读取历史不加载旧结果、显式历史回看、活动任务恢复及迟到响应保护；浏览器实跑证据见同一[模型修复记录](../validation/2026-10-04-smart-print-engine.md)。这是既有Git忽略的`planning/cache`维护台测试，不是原生GUI或修复核心改动，不用其通过替代跨平台验收。

重建报告主结论只展示 `assessment`，`applicability` 移到默认折叠的“重建输入范围”，明确不是重建建议。`eligible` 仅说明当前入口未发现不支持项，不承诺输入完整有效、需要重建或能修好。Node覆盖四类结论与输入范围独立、未知值不冒充支持、报告不变、实际DOM默认折叠及文本安全；旧版/缺报告继续保守显示。此项仅本地中文维护台展示，不改原生GUI/CLI业务、JSON或国际化机制，无额外模型计算。

10-15维护台双槽回归允许macOS标准GUI与一个CLI并行，全部CLI独立datadir/输出目录；构建、打包、重置/清理/恢复持全程独占锁，兼容旧服务锁。编辑版GUI命令按实际步骤归独占槽；前端依服务端execution_slot及能力位启用并行，旧服务安全保持互斥。Node覆盖双向启动、独立日志/取消、刷新恢复、未知槽拒绝及迟到响应；Python临时目录验证跨服务冲突、启动失败释放、维护操作期间锁与预览token保留、不构造SQLite及外部应用进程门禁。141项Python和38项Node通过；浏览器实跑GUI常驻下两次cube证据检测、cube/dragon普通切片通过，不代表打印质量或原生修复再验收。Windows仅模拟独占分支，未宣称Windows/Linux现场通过。

## 配置与约束

测试源码、测试程序、应用与资源配置四者身份应匹配，聚焦结果包含于完整结果时不重复计数。docs/ai/modules.json 是范围清单，新增文件必须分配模块或说明排除原因。

## 失败与取消

0=文档检查通过，1=需复核/缺失/未归属，2=工具或清单错误。脚本不生成正文、不自动记录审核、不暂存、不提交；日期和 mtime 不参与指纹；旧 blob 早于当前换行属性时，字节相同保持原身份，修改后按 Git 当前规范化规则计算。

云配置按用户要求撤回新增来源环境／账号绑定及相应策略测试，恢复旧请求完成校验；JSON协议回归保留。新增标准档名称优先级与成功导入才装饰旧名称的测试，来源按钮、保存、真实下拉选择及认证恢复仍需GUI联调，见[云配置记录](../validation/2026-10-09-cloud-config-quality-analysis.md)。

## 修改与验证

Windows GUI 采集在 2026-10-09 收到同事现场 JSON：64 位 Windows PowerShell 5.1 下完成三次窗口／线程输入枚举及 249 次消息探测，均返回成功。它证明该环境下采集核心路径已执行；不替代多进程交互、错误处理、双击启动器或 Orca 修复验收。应用故障结论见[现场记录](../validation/2026-10-08-print-ui-freeze-log-analysis.md)。

正在使用日志复制工具由 CMD 以 STA 启动 PowerShell 5.1，文件选择窗口禁用会提前拦截共享冲突的校验，随后由只读 FileStream 按 `ReadWrite | Delete` 共享方式实际打开；已有句柄不允许读取或文件权限不足时仍会失败，不宣称解除独占锁。源日志允许继续写入／轮换，仅复制打开句柄时已有长度，按 1 MiB 分块读取，避免追赶持续追加；中途截短报错并保留 `.partial`，成功关闭句柄后才改为原日志名并计算副本 SHA256。桌面独立目录使用新 GUID，输出文件 CreateNew，不替换已有文件；`copy-info.json` 记录源路径、长度、复制字节数、时间与异常信息。读取不是原子快照，也不刷新源程序缓冲。静态审阅和 ZIP/编码一致性检查已通过；同事已返回 PID 19568 的活动日志副本及 copy-info，578,681 字节与 SHA256 一致，共享读取核心路径在 Windows 实测成功。文件选择、取消、读取失败、截短等其他分支未全部验收。

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

`gfd_cloud_config_tests` 是云方案JSON协议的独立Catch目标，覆盖旧标准档、三档互不覆盖、坏数据拒绝和回读识别丢档；旧来源缺类型／null／空串默认标准到保存payload的连贯校验，并验证明确快速／精细类型不回退到标准。元数据回归验证只修改名称时保留最新备注、明确清空备注、ID禁止被元数据补丁修改，以及回读未保存字段时不能确认成功。新增名称回归覆盖标准档优先带后缀、回退旧名称、快速／精细隔离、避免重复后缀，以及仅成功导入标记才能装饰列表旧名称；仅上传、坏标记和空名称不装饰，已有导入标记持久化后保持。来源环境／账号绑定策略及对应测试已撤回；原请求完成检查保留。真实下拉条目身份、脏状态和认证恢复需GUI手测，不把纯函数协议回归当作GUI通过。无需账号或桌面，不能代替真实云服务验收，见[云方案记录](../validation/2026-10-09-cloud-config-quality-analysis.md)。

新增 `tests/libslic3r/test_advancer3_presets.cpp` 并注册到现有 libslic3r_tests，标签 `[Advancer3]`：验证向导元数据、Custom 独立加载、与 AD5X 有效参数等价及多品牌共存。测试只写临时目录，运行身份和 CLI 对照结果见[执行记录](../validation/2026-10-09-adv3-custom-feasibility.md)。

## 关联模块

[构建、打包与平台集成](build.md)、[应用入口与 CLI](cli.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。

GFD 模态回归使用真实 wx 窗口和事件循环，保留其他已禁用窗口；同一源码可在 Mac 运行公共循环场景，但生产设备选择仅在 Windows 接入新基类。Windows 原生 InitDialog／焦点／Alt+Tab 的验收状态单列，见[调查与修复记录](../validation/2026-10-08-print-ui-freeze-log-analysis.md)。

Advancer3 专项扩展为四喷嘴、12 工艺和六耗材的有效配置与兼容关系测试。网页测试 `node --test tests/web/guide_filaments.test.cjs` 同时载入首次向导和耗材管理脚本，验证完整身份、精确型号过滤、默认材料和桥接提交；运行结果见同一 Advancer3 记录，不代替实际 GUI。

Advancer3 下发协议专项目标 `gfd_print_options_tests` 位于 `tests/slic3rutils`，只链接 test_common 与 nlohmann_json，不发起网络或打印。覆盖实际 PMC 槽位结构、非法数据、ms／调平和旧机型载荷保持；验证身份和 GUI 交接见[Advancer3 记录](../validation/2026-10-09-adv3-custom-feasibility.md)。
