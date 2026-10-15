# 模型检测、修复与保色

<!-- module-review
{
  "id": "model-repair",
  "source_fingerprint": "sha256:606a36db3da3d3d911d6488c993197d70ffe9abd52118b19304d1c1c2cfc5119",
  "reason": "共享检测工作流对新证据组合区分正常无进展与执行异常；复用严格boundary验收，最终导出网格只预检一次，未完成不得报0。旧调用保持原行为，不清零缺陷，不改变高级/重建算法与自动导入开关；同步核心/CLI及维护台边界验证。本次复用同轮自交对作精确真实穿透存在性分类，并在显式允许封口时接受残留开边依据；schema2严格检查摘要/见证/准入，直接依据命中跳过Z观察，全空区域仍为后备依据。保留原计数、原件和普通修复入口，不实施自动重建。"
}
-->

## 职责与边界

本模块负责模型检测、修复与保色。归属以[模块清单](../modules.json)的 `model-repair` 为准。

## 入口与调用

- [ModelRepair.cpp](../../../src/libslic3r/ModelRepair.cpp)：repair_model/repair_staged_model；私有候选与事务提交。
- [MeshBoolean.cpp](../../../src/libslic3r/MeshBoolean.cpp)：网格布尔、分割与 CGAL／Mcut 适配；高级修复从 ModelRepair 调用相关核心接口。
- [FixModelByCgal.cpp](../../../src/slic3r/Utils/FixModelByCgal.cpp)：GUI 修复与工作线程/主模型之间的桥接。
- [ModelCheckWorkflow.cpp](../../../src/libslic3r/ModelCheckWorkflow.cpp)：run_model_check_workflow 与业务结果投影。
- [ModelInspection.cpp](../../../src/libslic3r/ModelInspection.cpp)：inspect_model_mesh 及受限基础清理计划。
- [ModelRepairEvidence.cpp](../../../src/libslic3r/ModelRepairEvidence.cpp)／[ModelRepairEvidence.hpp](../../../src/libslic3r/ModelRepairEvidence.hpp)：只读投影检测、修复与重建结果，并按需提供一次 Z 截面/有效区域观察；不调度修复、不生成或采用候选。
- [ModelReconstruction.cpp](../../../src/libslic3r/ModelReconstruction.cpp)：独立重建的适用性、源身份、稀疏候选副本、物理毫米变换、worker 协议及单文件 CLI；GUI／CLI 共用 `reconstruct_model_candidate`，此接口不采用候选。
- [MeshReconstruction.cpp](../../../src/libslic3r/MeshReconstruction.cpp)：三角形汤基础清理、最多三档 CGAL Alpha Wrap、最终浮点网格完整七项检测与双向采样距离告警；不接入旧保形修复验收。
- [ReconstructionProcess.cpp](../../../src/libslic3r/ReconstructionProcess.cpp)：直接启动当前应用的专用 worker、取消／超时／内存监测及退出回收，不复用 GUI 的 detached 启动路径。

## 核心流程

GUI 修复入口经 FixModelByCgal 调用模型修复核心。repair_model 创建隔离的 staging 模型，选择 MODEL_PART 体积；修复前检查、网格修复、属性校验成功后才提交。repair_staged_model 供已隔离的调用者使用，失败结果不可直接写回原模型。

### 检测／修复结果的只读证据

`ModelRepairEvidence` 不构建自动流水线。默认 `--check-model` v4 原六字段与退出码不变，仅显式 `--repair-evidence` 时附加 `boundary`（当前 `schema_version=2`），按 `inspection`、`repair`、`proper_crossings`、`sections`、`reconstruction` 表达观察与能力边界。GUI 没有新菜单或自动判断，普通导入、原高级修复、手动重建及模型特征检测不因此增加预检；后续是否调用其他修复由后端或用户决定。

`ModelCheckWorkflow` 仅对显式“检测＋高级修复＋证据”验收整个流程是否完成：正常高级无进展保留unresolved，完成最终产物检测/有效预检后正常返回，不再仅因无修改而抛旧异常。显式转换不能被提前返回跳过；预检对最终保留网格只运行一次。最终健康可免预检，残缺结果须严格通过boundary的完整性/准入判断；partial或unresolved遇预检异常、超限或缺证据都保留产物并报执行未完成。旧调用无进展仍抛原异常、无新增扫描。该规则不要求几何阻断的check_complete强制为true，不清零缺陷、不修改算法、不调度重建。

`inspection` 区分未执行、通过、已知缺陷和不完整，保留已观测缺陷、计数是否完整、未完成检查与结构检查阻碍。检测截断但已有阳性证据时不丢掉已知问题，也不将未知部分当作零；完整七项与部件/壳体结构分析不能混为一项。`repair` 记录原修复报告的实际执行、变更、未解决和失败状态；引擎自身验收不冒充修复后完整检测。

检测面数/顶点预算通过 `ModelInspectionLimitError` 标识，组合工作流记录 `resource_limit`；该类型保留旧 `runtime_error` 继承与消息，不改变门槛或退出码。其他异常按明确执行阶段分类，不解析英文报错猜原因。旧高级核心已吞并为失败报告的异常仍是 `repair_failed`，不能把它进一步伪称为确定的内存不足或几何无进展。

只有显式“检测＋高级修复＋repair-evidence”且修复正常结束、当前保留网格仍有完整缺陷证据并通过既有重建准入、尚无直接几何依据，才执行 `inspect_model_section_evidence`。未改变的网格复用已有检测；有改变时检查实际导出回读网格并将该副本复用于预检。默认参考 Z 中间平面间距不超过0.2mm、最多2000面，保留30秒协作预算、300万输入面/顶点与1000万潜在三角面-层位相交保护。超限不降采样冒充完整；单次旧调用不保证硬中断。普通旧调用不做额外扫描，健康/失败/未知分支也不跑预检。

直接几何依据复用既有 CGAL 相交对，不新增全对搜索：组合流程开启 `ModelInspectionOptions::classify_proper_crossings`，以 EPECK 的双方严格跨平面与非零交线段确定真实穿透，排除共面/点/边接触。首个阳性可以结束存在性分类；阴性须查完所有保留对，不能只看 detail_limit 的前200个。原搜索截断或摘要不一致均不能推荐；原七项计数、原 face ID 和副本身份保持。`elapsed_ms` 仅分类开销，不含原搜索。正常旧入口默认不开启，原生 GUI 高级修复和手动重建不因此加扫描。

`ModelRepairEvidencePolicy::allow_boundary_closure` 默认false；CLI只能在上述完整组合显式加 `--repair-evidence-close-boundaries 1`，表示后台确认输入应为闭合实体且允许封口，不表示当前操作会封口。维护台证据模式提供默认关闭的对应验证复选框；开放薄片不能借此自动加厚，格式/面数/对象数不能代替设计约定。历史 schema1 只读兼容，不为旧报告补造穿透分类或封口授权。

同一轮 Z 切层同时取得原始闭圈/开放链和经普通 `slice_mesh_ex` 同规则整理的 ExPolygons；不重复切层、不增加全量二维自交检查、不生成墙/填充/支撑/G-code。有效区域统计、全空区域、非退化闭圈被抵消及最多16个位置样本补充旧开链/补缝数据；绝对闭圈面积和包含洞及重叠，不能当预期实体面积或丢失比例。CLI 无工艺上下文，明确 reference_z_midplanes/printer_layer_plan=false；共享接口可接受调用方完整层位及有效参数，坐标按变换后的毫米解释，身份/完整计划由调用方负责。异常/取消/超限不改变旧返回或产物；内部 elapsed_ms 与包含网格准备的 workflow_elapsed_ms 分别记录。规模保护不是硬 RSS 限制；区域在全 Z 生成后汇总释放，不能宣称恒定内存或30秒硬截止。

`evidence_present` 统一要求高级正常结束、完整已知缺陷和重建准入 eligible，依次接受 `proper_crossing_after_repair`、明确封口策略下的 `open_boundaries_under_closed_solid_policy`、原 `no_effective_regions`。前两者命中时截面状态为正常 `not_run/reconstruction_evidence_already_present`，不重复预检；第三者仍要求完整 Z 计划全无有效区域且存在非退化闭圈、没有能主动删区的参数干扰。开链、自交总数、单个空层、非流形数量或面积跳变不独立触发。其余 undetermined，失败/取消/未知 blocked；applicability 独立准入，necessity=not_established、automatic_adoption=false，不能把质量策略建议说成原模型必定切片失败或候选必定保真。not_needed 仅表示当前完整网格检查通过。

普通高级修复和独立重建 CLI 也仅在 --repair-evidence 时投影已有结果，不增加检测/预检。重建投影保留 shape_structure_guaranteed=false、printability=not_assessed；共享候选核心/GUI 不无条件改变报告，不改变原偏差告警、风险确认、人工采用和撤销语义。

共享 CGAL 修复在 info 日志记录 `stage_finished` 与 `timing_summary`，以单次调用 `repair_id` 关联、单调时钟毫秒计时。统计仅覆盖该次 CGAL 候选生成/校验，不含外层模型检测、绘制恢复、导出或 GUI 等待；阶段包含其中的检查点工作，不能将汇总时间与阶段时间重复相加。`candidate_rejected` 不等于整个模型失败，外层仍可能采用已验证的部分成果；异常退出单独标为 `exception`。

局部拓扑准备记录 `soup_topology_result`：`face_limit`、`boundary_query_budget`、`boundary_split_limit`、`boundary_pass_limit`、`removed_face_limit`、`interior_contact_limit` 表示对应资源保护；`no_topology_change` 表示无适用修改；其余 `orientation_unproven`、`candidate_*`、`removed_face_*`、`float_output_invalid` 等说明几何证明/验收未通过。跳过本分支仍走既有后续处理，不把超限说成整个引擎不能修复；全部限制未改。

独立重建由用户明确发起：先验证对象适用性并冻结源身份，复制选中对象，按 `instance × volume` 变换到毫米空间，再交给受限子进程。worker 清除精确零面积／重复三角形，直接对缺陷三角形汤生成近似外包表面；不要求输入先成为闭合、单壳、方向一致的实体。输出必须闭合、流形、无自交、围成方向正确的正体积，并通过共享 `inspect_model_mesh` 的完整七项检测；合法把手、分离实体及正确朝向的内腔不是输出缺陷。逆变换后的模型局部网格和重新变换到物理空间的浮点网格也须通过同样复检。父进程只接收大小受限、任务身份和校验值一致的候选。原模型保留，GUI 显式确认后才用既有原子提交／Undo／备份机制采用；CLI 仅保存新产物。

开放面直接包裹可能只得到不可打印的薄壳。重建默认启用 `close_open_boundaries`：在私有副本上尝试封闭边界，必须先可转换为非自交流形、全部补面成功，再对实际 float 网格进行闭合、体积/壳体方向及完整七项检查，成功才替代包裹输入。最多 64 环、每环 512 边、合计 2048 边；准备网格上限为清理后输入面数加 2048，受同一 worker 的时间/内存预算限制，不能用较低的最终输出上限拒绝高密度输入。使用固定 CGAL 支持的 iterator 补洞接口，禁止 cubic 回退。闭合输入不补面；失败的受控几何/预算检查回到原汤，检查引擎异常则停止，不伪装成可继续的几何缺陷。

该步骤可能填上有意保留的开口，GUI 明确提示并可关闭，CLI 使用 `--reconstruction-no-close-open-boundaries` 关闭。`engine.boundary_closure` 记录边数、环数、补面数、是否采用及退出原因；原输入有开边但未采用封闭副本时，`engine.metrics.open_surface_envelope_warning=true`，提示候选可能仍是薄壳、需检查墙和实体路径。尺寸和双向距离仍与原汤比较，不用封口副本掩盖形状差异；这不是自动识别设计意图或自动可打印性验收。

候选的 3D 凸包也在 worker 内计算、复检并以有界协议返回，主线程只验收和安装，不再执行 QHull；安装后清除体积二维投影及实例轮廓缓存，避免沿用原模型的排布范围。计时／内存预算覆盖 worker 的重建、严格几何检查和凸包处理，不包括主进程的有界读写、预览截面和导出，不能称整个 UI 操作具有同一个硬截止时间。

## 配置与约束

几何是否需要修复、是否允许拆壳／删除非三维部件、是否允许清除绘制与能否保色是独立政策。GUI／CLI 的默认策略必须分别查看调用处，不能因为共用 CGAL 就认为都允许丢弃材质。相同修复选项下应按同一规则保留几何与属性；现存入口差异需记录原因／影响，新改动不能只更新一端。

闭合候选及 polygon-soup 候选的形状验收先判断原有部件数、包围盒、面积/体积条件；已确定拒绝时不计算 Hausdorff 距离，日志 `distance_checked=false`，距离字段为未计算的 NaN，不代表零偏差。廉价条件通过时仍运行原有单向/双向距离检查，阈值、候选接受条件、保色、公开检测/修复 JSON 均不变。不增加保守模式、不改变取消架构或 20,000 面上限。

重建首版限一个普通 `MODEL_PART` 和一个实例；阻断附加体积／负体积／修改器、面级颜色与支撑／接缝／绒毛绘制、变层高、手工裙边点、SLA 点位／排液孔、切割关联及可编辑文字／浮雕／纹理。只读拒绝，不清除属性后再继续。健康模型允许用户手动尝试；单个网格内的开放边界、非流形边、自交、方向冲突、多壳／嵌套壳和贯通孔允许进入重建，不与多个 ModelVolume 混淆。孤立非退化面可生成有厚度的壳，但不保证厚度可打印；全退化、越界索引、非有限坐标仍拒绝。清理计数写入报告，仅由已保留三角形计算包围盒，不让废弃顶点影响度量。

默认细节半径 `alpha=0.5 mm`、表面偏移 `offset=0.05 mm`、偏差告警阈值 `0.25 mm`，GUI/CLI/worker/引擎共享 `RECONSTRUCTION_MAX_INPUT_FACES=3000000`，输出仍最多 100 万面；独立 STL 文件大小最多 256 MiB。没有为通过输入限制进行自动减面。三参数须有限正值，offset 不超过 alpha 的四分之一。`max_deviation_mm`／CLI 旧参数名保留，含义明确改为告警而非拒绝，报告 `deviation_policy=warning_only`。先尝试用户参数；估计／中间细分超限或候选几何不合格时，最多再尝试 alpha 与 offset 同时乘 2、4，并记录每轮结果及有效参数，不降低最终七项检查要求。

三次尝试共用同一 worker 预算。共享 `model_reconstruction_process_options` 依据未清理输入面数分档：不超过 20 万面为 60 秒/1024 MiB，20 万至 100 万面为 180 秒/2048 MiB，100 万以上为 300 秒/4096 MiB；自动内存再取物理 RAM 四分之一上限（最低 128 MiB，未知 RAM 回退 1024 MiB）。这不是可用内存探测或完成时间保证。`ModelReconstructionOptions.process` 的零值表示自动，正值保持调用方选择，包括显式 CLI 超时；通用进程 API 不接受零预算，调用前必须解析。GUI 显示生效值，父级报告 `resource_limits` 在成功或 worker 失败时均保留生效预算和面数上限。macOS 为采样 RSS watchdog，不是硬分配上限；Linux 使用 worker 内 RLIMIT_AS 加 RSS 监测，Windows 使用受内存限制并可整组结束的 Job。父进程监督之外，worker 内也监测父进程身份／存活、资源和截止时间，避免父进程退出或 GUI 事件回调停滞后无限运算；Linux 另设父进程退出信号保护。POSIX 父进程 RSS 查询失败时复查子进程退出状态，最多等待两个 20 ms 周期；连续第三次失败仍返回监测失败，不把未知内存当零，各周期仍检查取消／截止时间。平台实现存在不代表均已运行验收。

双向距离为采样估计，不能表述为最大误差保证；超过阈值只告警。缺陷优先重建不以输入代数体积／内部见证点证明结构保持，`interior_check_status=not_preserved`、`reference_interior_check_complete=false`；原有孔、空腔和薄壁可能被填充、合并或加厚，GUI 必须提示并确认。独立 `model_reconstruction_v1` 区分 `candidate_ready`、几何检测、人工确认、采用与试切片；首版 `trial_slice=not_run`、`print_ready=false`，GUI 可明确采用为待试切片候选。网格七项通过不意味着无悬空或能生成首层路径；外部诊断试切片结果单独记录，不伪装为自动流程已执行。check-model v4 原字段和退出码保留，仅显式 --repair-evidence 时附加上述 boundary；旧普通 repair JSON 和高级修复策略不变。

## 失败与取消

单体积失败或用户取消可导致整个事务回退；CGAL 单次内部操作不保证即时可中断。检查函数抛错不能转换成“需要修复”继续更重操作。成功写文件也不证明修复后的网格正确。

上述线程取消约束属于旧高级修复；独立重建通过终止并回收专用子进程处理取消／超时。启动失败、内存预算、进程崩溃、引擎异常、报告／网格截断或校验不匹配均不得交付候选，不自动重启 worker。只有 worker 内受控的细分预算／候选不合格允许上述有界粗化尝试。GUI 等待／审阅期间源网格、体积元数据、变换或相关配置变化后丢弃旧候选，不覆盖现场。

## 修改与验证

`test_model_repair_evidence.cpp` 覆盖检测完整性与缺陷投影、未执行/失败/取消的区别、残留截面证据与无法确定重建必要性、只读原件保留及预算边界。`test_mesh_slice_diagnostics.cpp` 的 `[MeshSliceDiagnostics]` 对照普通切层输出，覆盖开放链、补隙、未焊接重合点、水平缺盖、变换/模式与取消。现有 `scripts/test_cli_model_check.py` 扩展 v4 `boundary` 断言，核对旧字段/退出码、只检测不运行断面和显式组合修复的实际证据。测试范围不等于已运行；没有新增自动编排集成或自动采用测试，实际构建、CLI、GUI、跨平台与实机结果分别记录。

改核心需检验健康模型、开边、退化／重复面、空体积、材质与绘制、失败原模型不变。按验证规范选 `[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`，无匹配不能计为通过。

现有测试/工具范围：`tests/libslic3r/test_model_repair.cpp`；`tests/libslic3r/test_meshboolean.cpp`；`tests/libslic3r/test_saved_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

计时/退出原因专项标签为 `[ModelRepair][Diagnostics]`：覆盖成功、拒绝、异常、边界查询预算及面数上限，断言后续分支继续、拒绝不修改原网格；耗时仅要求非负，不使用脆弱的速度阈值。必要优化的构建、CLI 证据和 GUI 交接复用[执行记录](../validation/2026-10-04-smart-print-engine.md)的当前任务章节，后半部分已撤回方案不得作为现行行为。

重建专项为 `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard]`，对应 `test_mesh_reconstruction.cpp`、`test_model_reconstruction.cpp`、`test_reconstruction_process.cpp`；覆盖各类缺陷汤、清理计数、三档尝试、偏差只告警、最终七项完整通过、合法内腔与反向独立壳、浮点塌陷及快速进程退出。隐藏 `[ReconstructionIntegration]` 需指定当前应用的 `ORCA_RECONSTRUCTION_BINARY`，验证真实 worker 及已预热轮廓缓存的更新。CLI 用 `scripts/test_cli_model_reconstruction.py` 核对独立 schema、失败原件不变、中文路径、文件边界、实际导出网格及告警语义；自交样例通过网格检查仍不能升级成可打印结论。具体结果、压力测试异常及真实失败模型／试切片结果归同一[主记录](../validation/2026-10-04-smart-print-engine.md)，GUI 对比／采用／撤销／保存重开与待试切片边界见[重建 GUI 交接](../validation/2026-10-05-reconstruction-gui.md)。测试源码存在不表示已通过，不能以人工方块候选替代真实失败模型与打印流程验收。

`[BoundaryClosure]` 专项覆盖正/反向缺面方块、关闭后薄壳复现、闭合方块/贯通孔/相交闭壳结果不变，以及平面、自交开放面和超预算环的回退。CLI 验证同一策略传至 worker；同一 GUI 工程配置的外部诊断切片验证墙、填充及顶底面，不只检查“层非空”。

## 分支现状

本分支新增 ModelInspection/ModelCheckWorkflow：检查入口与导出产物复检共享 256 MiB 输入上限及 300 万面/顶点默认，交叉检测另有资源限制。检测不完整不能计为通过。基础清理限精确零面积/同向重复面并保留属性与事务约束；公开业务检测结果由专用投影给出，不把内部历史结构直接输出。普通 STL/OBJ 导出与此流程不同。

## 关联模块

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[应用入口与 CLI](cli.md)、[后台切片、任务和取消](jobs.md)。
