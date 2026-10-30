# 模型检测、修复与保色

<!-- module-review
{
  "id": "model-repair",
  "source_fingerprint": "sha256:a2eceb9fadc1425ef12eeeaf9867ebc180c5e014a184befa73d9040588285af4",
  "reason": "MeshLib从独立sidecar改为内部C++20适配库，私有网格/配置、SDK回调异常收口与协作RSS/超时/取消、后台join后再交付。保持独立完整网格/变换/STL回读验收及形状未确认语义；默认OFF、AlphaWrap和GUI路径不变，保留已提交证据规则。新增平台配置与运行库身份、移除旧worker和累计RSS，Mac实测与Windows/Linux待验分别记录。 10-17 新增单次业务交付报告：只读原始STL基准、实际回读产物/哈希、双向采样/完整共享边部件数；部分高级成果可比较，重建验收不放宽，不在Orca做阈值采用。 10-17 本轮仅调整可选macOS运行库准备目录、构建RPATH和说明；运行身份哈希仍针对最终库内容，默认AlphaWrap/GUI入口、修复业务和CLI参数不变。与已有模型阶段报告及分盘改动一起构建；真实包内MeshLib42项通过，平台配置23项通过不等于跨平台运行验收。 10-18 仅同步后端单一修复流程说明，去掉业务版本/开关，Orca共享核心及GUI/CLI行为和源码指纹未变。10-19仅修Linux ON链接参数穿过静态依赖及构建树RPATH，向打包模板导出SDK目录；不改算法、准入、报告或GUI/CLI业务入口。真实Linux容器SDK适配/迁移7项及配置/打包50项通过，完整Orca/GUI/普通切片分盘未验，MR原字节哈希规则不变。 10-19复审修正阶段报告STL按重复角点计顶点预算的问题：仅显式阶段读取精确共享顶点，面/顺序/坐标/文件哈希不变，普通重建与GUI默认路径不改；补真实百万面读取到截面预算回归。新Mac构建213原生/34阶段CLI/10报告CLI通过，默认G-code跨补丁一致；Linux静态zlib完整配置成功，最终包状态以本轮记录为准。 Linux ON增补TBB头定义局部隐藏，普通高级修复与GUI不引入新入口；配置级合成ELF通过，完整包与动态绑定结果见Linux记录。 默认时钟 lambda 使用完整 std::chrono::steady_clock 名称，修复 MSVC 对类内 Clock 别名的解析失败；补充跨平台编译约束及验证记录。时钟类型、注入接口、GUI/CLI 入口和预算语义均不变；诊断阶段最小 MSVC 对照已通过，按用户要求完整应用由用户编译。 2026-10-26合入master至Linux发布分支：两侧业务说明与发布配置均保留；源码无冲突，合并验证单独记录。 2026-10-26目标分支复核：合入master的云工艺快照优先、MSVC时钟名称与CuraV1偏心填充修复；保留已存细节opt-in和Linux专用构建。模块正文已合并对应说明，共享配置/测试引起的身份更新不代表另改本模块默认策略。运行与GUI边界见2026-10-26发布合并记录。 2026-10-28：显式 advanced 报告增加只读原面保留/边界圆盘补洞证明；共享CGAL/MeshLib算法、普通切片/分盘和GUI无改动。原生51断言/2例、34项CLI及PDS本地链路见局部修复采用记录，异常薄片仍未解决。 2026-10-28第二批：复用只读阶段比较的精确共线清理结果，以非退化三角面一致性证明基础清理；不改原修复/重建/切片算法。零面积尾巴误拒已在真实PDS链路复现并修正，60原生断言/3例、37CLI与13模型场景见同一进行中记录。 2026-10-28真实原件修正：显式阶段报告新增逐面1e-6mm坐标量化保留证明、最长边摘要，并允许彩色STL只读几何；直接重建原入口不变。PDS仅完整check/advanced允许数值裂缝计数变化，重建前一次选择有界体素，原验收仍生效。15真实原件9→11采用且完整通过、4未解决，成本/默认路径及平台边界见进行中记录。 2026-10-28第五批：仅显式完整重建且部件减少时补原始闭合实体内部面/平整接缝采样依据，PDS按外表面和证明的合并组数采用；0.25mm预算与一次触发不变。87原生断言/5例、40CLI、57服务回归与15真实原件11→12采用见局部修复记录，反例保留、普通路径和GUI不调用。 2026-10-28第六批：真实ASCII原件NaN法向造成阶段入口误拒，仅geometry_only报告读取允许NaN/Inf法向字面量；有限顶点/语法/大小限制保留，独立重建与普通切片/GUI不改。121原生断言、44CLI、真实2件修前后及80次切片对照见进行中记录；资源和未解决样本按实测记录，不提高生产预算。 2026-10-29入库复审：新增逻辑限定显式报告，修复算法与普通切片/分盘不变；复核当前源码/程序身份，两个真实模型报告开关各3轮产物完全一致，耗时/内存与平台边界见局部修复记录第九批。 2026-10-29合入Linux发布分支：保留既有发布兼容修复和本次阶段报告新增说明，源文件自动合并，模块指纹按组合源码重算。 2026-10-30合入App修复：显式basic检查与分部件高级修复，默认full及普通切片/分盘不变；保留本发布分支兼容修复。指纹按合并后的实际源码复核，验证边界见2026-10-30-app-repair-release记录。"
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

### App 基础检查模式

`--check-model --model-inspection-mode basic` 仅作用于独立检查及其高级修复组合；省略或 `full` 保持完整检查。原生导入后检查非空、有限坐标、合法索引、资源限制和五类轻量缺陷：原生未配对边、非流形边、严格零面积面、重复面及局部方向冲突。`boundary.count/method=orca_open_edges` 为原生邻接未配对边；`geometric_count` 为单面几何边界数，二者不可混称。同坐标精确归并、紧凑排序数组及过滤精确共线谓词保留，不将极窄有效面当作退化面。重复面区分同向多余面与反向重合组。

所有已测阳性均写 `needs_repair/defects_detected`，不因没有自动处理操作就声称无缺陷。开放边、重复面、退化面或局部方向冲突触发一次高级处理；只有非流形几何边而原生邻接闭合时 `next_action=none`：简单拆索引不能消除坐标重合，自动布尔合并会改变模型，因此保留残留问题，不重复运行CGAL。阶段报告透传已有 `inspection.next_action`；Slicer basic且重建关闭时依据 `none/advanced_repair` 直接分流，不再重复维护四项计数规则。非流形边如由重复面引起，仍随重复面清理处理。`nonmanifold_vertex/self_intersection` 继续 `not_requested/count=null/complete=false`，不做顶点link、自交全面搜索或部件诊断；`topology_statistics_ms` 只计新增轻量统计，非任务总时长。

App高级设置内部 `basic_repair=true`，调用 `MeshBoolean::cgal::repair_basic`：一次严格退化面/重复面清理、定向、非流形索引拆分、缝边及补洞。保持CGAL必要拓扑检查、定向前一次自交前置检查及最终浮点闭合检查；不做自布尔/实验性穿插试修、不做保形距离/中间候选验收或重建，不承诺消除复杂穿插。失败候选不替换原件。App高级恢复现有安全分组：独立部件分别修复，包围盒接触/相交/嵌套的壳保留同组；直接提取原面与朝向，避开 `ModelVolume::split` 的小部件删除和正体积归一化。成功部件保留、普通几何拒绝的部件原样保留并继续其他组；全组均无进展不提交拆分。薄片删除启发式仍关闭；只删除每个三角面均经精确共线谓词确认零面积的独立组，保留任何有面积的薄片；全对象删空仍回滚。普通 basic 检查不增加此分组。

默认 `basic_repair=false`，GUI主动修复、full、Mesh及普通分盘/切片保持既有实现。App外层 `allow_partial` 用于保留其他成功部件/volume及失败部件原件，不使用CGAL中间检查点；数据/取消/异常等仍按事务规则处理。App重建关闭不请求截面重建依据，basic阶段可记录unresolved并继续使用原件；该结果不代表打印质量保证。

basic 的 check/advanced 阶段报告不再做整模连通部件、包围盒差异、双向距离采样和补面几何证明；`change={status:not_requested, reason:app_basic}` 如实说明范围，不输出虚构零值。保留实际产物回读与已检测三角面一致、原件/参考件/产物的路径和哈希绑定、非空/有效数据及轻量统计。Slicer 仅对请求和报告均为 basic 的 check/advanced 接受此范围；full 和 reconstruction 仍完整验收。删除已无生产调用的对应三角面顶点比较快路径及其服务判断。普通阶段报告的文件失败语义不变。

共享 `repair_model` 对本次调用内没有拆分/分组、网格没有改变的原部件复用初次 `mesh_requires_repair` 结果，避免重复检测；新拆分/分组部件仍重新判断，默认 full/GUI 的修后浮点候选仍独立验收；App 成功候选按上段规则去重。GUI 同样共享此优化及原有 worker hook、绘色验证和取消/回滚语义。full CGAL 算法及 Mesh 重建验收不变，App仅采用上段简化入口，不做跨任务缓存或多轮试修。

原生 GUI 没有 App basic 请求模式，默认完整检查不变；普通切片/分盘不接受该 CLI 参数，不增加对应计算。检测内部按源顶点编号复用本次精确坐标规范化结果，保留首次出现顺序、精确坐标合并及输出身份。新版 basic 报告需配套更新引擎和 Slicer，避免旧 Slicer 将有意未请求的深检误判为缺失。主服务继续传已有 `inspectionMode=basic`；App 不加新字段或状态。性能、回归和待 GUI 手测边界见[本地验证记录](../validation/2026-10-30-app-basic-repair.md)。

2026-10-30 追加实现提速：阶段报告的精确坐标/共享边索引、比较网格准备中的精确坐标/重复面索引改用哈希表，编号仍按输入首次出现顺序分配，不迭代哈希表生成结果。精确相等（含正负零）、共线/重复清理、组件定义及采样顺序不变。产物回读先逐面精确比较；顺序不一致才使用原有排序比较，不用容差放过改坐标、丢面或改朝向。比较网格准备也由显式重建共用，需覆盖其原生及真实 MeshLib 回归；普通切片/分盘不调用这些验收辅助函数，GUI 重建仅共享同样的等价索引实现，没有新增选项或采用规则。

### 业务分阶段交付报告（CLI 显式开启）

`--model-stage-report` 只适用于 `--check-model`（可组合高级修复/证据）和独立 `--reconstruct-model`。可选 `--model-reference ORIGINAL.stl` 固定所有阶段比较基准，缺省为当前输入。新 `ModelStageReport` 复用有界无修复 STL 解析和现有采样距离，输出目录固定 `model-stage.json`；协议 `orca_model_stage_v1` 包含阶段、输入/基准/产物 SHA256、相对文件路径、回读校验、七类检查、双向采样/包围盒变化、完整连通部件数和重建建议。输入/基准只读，产物必须在输出目录内且不是输入；已有报告不覆盖。

报告只在单次流程正常完成后发布，回读物理三角面须与实际检查网格一致，并再次核对输入/基准/产物哈希。原子无覆盖发布不表示整个进程已经成功：消费者仍须同时要求退出码0、报告协议/身份匹配和实际文件哈希，失败调用不捡目录残留文件。高级阶段可保留部分修复，不套用重建完整通过规则；重建复用已完成的独立检查，不重复全量相交检查。

显式报告模式的 prepared/result 检测和截面证据直接读取实际 STL 原始浮点坐标；不使用 Model 导入居中再平移后可能发生微小舍入的视图。阶段报告专用有界读取仅合并精确重合顶点，不删除、调序或翻转三角面，不合并相邻但不同的浮点坐标；文件哈希仍对应原始字节。这样 STL 每面三个重复角点不会把截面预检的 300 万顶点预算误缩成 100 万面上限，原面数、顶点与截面工作预算不放大。修复算法仍使用原有 Model 路径，严格三角面一致性不放宽；普通未启用报告的检查、独立重建默认读取与 GUI 路径不变。非整数坐标方块/补面回归防止误拒合法导出，真实70de初次失败和修正复测见[本轮验证](../validation/2026-10-17-model-repair-staged-gui.md)。

距离比较允许两侧只读排除退化/重复面，并记录数量；原始包围盒与部件计数仍包含这些面。部件按精确坐标焊接后的共享边连通定义统计，点接触不合并；不等于设计零件或部件对应。采样不是最大误差证明。Orca 不做业务阈值选用，也不编排下一阶段；PDS 采用唯一修复流程、固定实际参数并持久兜底，不按业务版本分流，见[业务接入方案](../../../doc/model-repair-business-integration-design.md)。

显式阶段报告增加 `change.hole_fill`：仅 advanced 且最终网格完整通过时核验局部补洞。原面按物理坐标逐一精确保留；新增面须为连接原始开放边界的独立圆盘，且各自不越过该边界包围盒。原面变化、额外封闭壳、跨孔连接、异常外伸均不能获得证明；无法证明就保留原偏差采用规则，不修改修复算法或补造成功。三角面重排/翻面不影响几何保留判定，方向正确性仍由完整网格检查负责。

阶段比较发现原件含精确零面积面时，复用现有 CGAL `collinear` 清理后的比较网格，增加 `degenerate_cleanup_preserves_surface`：排序面索引核对所有非退化物理三角面精确一致，候选本身不得含退化/重复面。不采用面积容差，不把近共线面或有厚度薄实体当废面。仅完整复检通过的 check/advanced 可据此豁免退化尾巴引起的原始包围盒/连通数差异，双向距离仍受原阈值约束。MeshLib 重建和非零面积孤立面删除不适用；普通模型不新增该排序，普通无报告路径不调用。辅助索引总计每对面8字节，复用已有几何/空间索引，不另复制网格或保存区域。

2026-10-28 真实样本修正：仅完整通过的 check/advanced 且原始连通数变化时，按每个坐标四舍五入至 1e-6 mm 后逐三角面一一匹配，输出 `numerical_cleanup_preserves_surface`。允许导入焊接浮点裂缝导致的计数变化；面数、面重数和位置仍须一致，排序忽略角点顺序，方向正确性由完整检查负责。原始连通数照实报告，PDS 只豁免此计数差异，不豁免距离/包围盒、不适用于 MeshLib。增加两侧面索引共约8字节/对面，普通无报告路径不调用。

阶段报告的有界 STL 读取允许二进制颜色头/属性字节，只读取几何，与普通 STL 检查入口一致；原文件及 SHA256 不改写，不承诺修复导出保留颜色。独立 `--reconstruct-model` 仍使用原严格读取并拒绝直接彩色输入。报告新增实际候选的 `geometry.max_extent_mm`，PDS 在一次 MeshLib 调用前选择 `max(配置体素, min(偏差预算, 最长边/1024))`；默认小模型仍为0.0803mm，不提高原0.25mm验收阈值。1024仅是服务的分辨率启发式，既非内存上界也非误差保证；引擎仍按显式参数执行、保留资源限制和最终检查，不重试调参。15份真实原件结果及限制见上述进行中记录。


阶段只读几何读取也允许 ASCII 文件标准 NaN/Inf 法向字面量，因为法向不参与三角面几何，行为与二进制 STL 忽略存储法向一致。顶点坐标仍必须有限，语法/token/文件/面数上限不变；原文件字节和 SHA256 保留。独立重建的严格读取与 GUI/普通切片导入未变。真实大文件验证说明入口兼容不等于修复成功：81363可以继续检查，但重建仍超资源预算；73342检查通过，详见进行中记录。

重建采用补充证据 `change.material_union` 仅在显式 reconstruction 报告、候选七项完整通过且原始部件数减少时计算。原件有效面≤5万、壳数2–16，逐壳要求闭合、流形、朝外正体积且自身无交叉，复用 CGAL 点内外判断；只将互有内部样本的原始壳归为可合并组。原三角面7点（3顶点、3边中点、重心）逐个比较，位于另一相交原始实体内部的样本不计外表面损失。反向平整接缝允许1e-5mm数值误差，邻域全部面必须共面且方向相反；孔边/折角、独立或仅接触构件不豁免。并非以“位于候选内部”判定有效。内腔朝内壳、开放/自身相交源、超预算或无法证明时沿用原规则。

报告保留原始双向距离/bbox/计数，另给外表面采样距离、内外样本量和预期合并组数。PDS只在摘要有效、容差不大于本任务阈值且实际计数吻合时，用外表面采样距离替代forward验收；reverse、bbox、文件身份和完整检查继续保留。没有修改触发、修复算法或0.25mm默认预算。额外空间树/采样记录仅报告过程存在，最多35万采样记录约8.4MB，另有源壳/候选树开销；不能声称总开销只有索引或零性能成本。普通切片/分盘、未请求报告的重建及GUI不调用。它仍是采样验证，不是连续最大偏差或打印质量证明。真实底板误拒修正、反例及限制见[进行中记录](../validation/2026-10-28-local-repair-adoption.md)。

源面只存排序索引/位集；补面边与局部拓扑信息有10万新增面预算，在进程内释放，仅输出状态、方法和面/区域/边数量。不是服务端区域坐标或数据库新表。PDS 对有证明且完整通过的高级补洞只豁免 candidate→source 的阈值；原表面距离、包围盒、部件数量及身份约束继续生效。MeshLib 重建与普通切片/分盘不使用此豁免，不扩大自动重建条件。实际兔子缺口、误改反例、费用与剩余异常薄片见[局部修复采用记录](../validation/2026-10-28-local-repair-adoption.md)。

默认不传参数时不加载额外基准、不比较、不输出sidecar，v4六字段、普通修复及默认Alpha Wrap行为保持。原生GUI无此报告入口，不增加GUI参数或自动选用；共享修复/重建验收规则未改变。新增契约回归 `scripts/test_cli_model_stage_report.py`，包含前后端固定的距离采样与部件统计口径断言；运行结果与未验平台见[执行记录](../validation/2026-10-17-model-repair-staged-gui.md)。

`test_model_reconstruction.cpp` 的 `[ModelStageReport]` 覆盖二进制/ASCII 的文件哈希、每面坐标/角点顺序、相邻浮点数、重复/反向/退化面的保留；其中 `[slow]` 百万面夹具仅验证有界读取至单层截面预算链，对照旧未共享表示的 `input_limit`，不代表百万面完整高级修复或重建通过。新增测试当前运行状态由本轮统一构建/验证记录给出。

### MeshLib 本机评估后端（仅 CLI）

独立重建保留 `--reconstruct-model`；显式 `--reconstruction-engine meshlib --reconstruction-voxel-mm <毫米>` 选择固定 SDK 3.1.3.429。默认仍为 Alpha Wrap，原生 GUI 没有 MeshLib 选择项；普通检测/高级修复/证据流程不自动调用 SDK。体素必须有限正数且能表示为正 float，不设通用默认、不自动粗化或回退；Alpha Wrap 的 alpha/offset/预封口关闭参数不能混入 MeshLib。MeshLib 改为同进程调用，旧 `--reconstruction-worker-path` 已移除并按未知参数拒绝，不另维护修复程序。

Orca CLI → 当前进程后台线程 → 内部 C++20 静态适配库的 `rebuildMesh` → CGAL 复检/比较/回变换/凸包 → 独立目录的 STL 与报告。适配接口只传普通数组/标量/报告字符串，SDK类型不泄漏到Orca；有界内存STL维持原加载语义（每面50字节＋84字节），无中间文件或IPC。关闭加载telemetry，记录跳面/复制顶点和实际参数；加载漏面拒绝。MR网格/临时转换缓冲先离开作用域，再进行独立复检；库内部缓存可能驻留。最终STL回读逐三角形精确比较，不能以SDK成功替代网格验收。

复用重建独占槽、私有输入及选项快照。`InProcessReconstructionControl` 由调用线程每20ms采样进程RSS、截止时间与取消；SDK回调只读原子停止状态，不调用GUI/外部回调。先做有溢出/体素网格范围检查的启发式准入估算，未知RSS失败关闭；估算不是峰值上界，采样预算也不是硬内存限制。SDK取消、CGAL/凸包阶段检查共享预算，不重置时钟；不可中断阶段可能延迟取消。任何回调异常先取消并join，不detach、不在主进程使用WorkerGuard/RLIMIT/kill。同进程无法隔离SDK崩溃。旧Alpha Wrap进程监督保持；已移除不再使用的嵌套sidecar累计RSS选项。

默认时钟回调直接使用 `std::chrono::steady_clock::now()`，避免 MSVC 14.44 在构造函数默认参数的 lambda 中无法解析类内 `Clock` 别名；时钟类型、注入接口及超时规则不变。该头文件在关闭 MeshLib 时仍参与主程序构建，验证范围见 [Windows 编译修复记录](../validation/2026-10-24-msvc-reconstruction-clock.md)。

MeshLib报告保留 `model_reconstruction_v1`，增加 backend/sdk_version、输入/候选SHA256、`engine.sdk`原报告、输入缺陷、独立输出复检和分阶段耗时；`execution`记录同进程PID、非硬超时/无崩溃隔离，`admission`及`resource_limits`记录估算、预算模式和观测峰值RSS。SDK身份哈希不是原生代码沙箱。双向距离复用AlphaWrap的同一有界采样函数，只作偏差告警，不是最大误差证明。候选通过仍需确认间隙、薄壁和独立部件，`shape_confirmation=not_confirmed`、`print_ready=false`、`applied=false`。不按“只留最大部件”或样本专用阈值删除碎片，不硬编码7部件；Q对照79,400面/7部件包含额外删壳，不是无删除后端必达值。无效/取消/超时结果不导出成功STL，原件保留。

同一个Orca构建管理适配层，不单独发布它。源码不含 SDK；macOS 构建脚本为用户已配置的 SDK 制作本地 arm64 / 14.0+ 包，副本重定位和签名后才编译其身份哈希，运行期校验不弱化。外部商业发布与公证不在本轮范围。默认OFF不依赖SDK；ON配置覆盖macOS14+/arm64、Windows MSVC x64及Linux x64/arm64，后两者仍为待原生验证的实验适配，须满足静态宿主依赖及对应ABI限制。实际MRMesh/MRVoxels内容哈希校验允许路径迁移，Windows用宽字符模块定位并输出UTF-8路径；缺库仍可在系统加载阶段阻止程序启动，不保证JSON。不把配置级测试当跨平台运行通过。构建方式见[内部适配层说明](../../../src/meshlib/README.md)和[构建模块](build.md)；实际结果见[执行记录](../validation/2026-10-04-smart-print-engine.md)。

2026-10-19 Linux ON 补齐静态依赖链路参数传播、静态ZLIB选择和包内依赖加载，业务边界不变。完整包进一步审计发现TBB头定义抢占，已补仅Linux ON的局部符号隐藏；修正版重链后，隐藏SDK、原字节解包并搬迁的真实程序通过42项MeshLib／34项阶段报告／10项切片分析及AppRun重建，实际绑定不再由SDK使用主程序TBB定义，普通切片G-code与修前一致。固定Ubuntu24 x64 SDK与MR原字节身份用于测试包，必要的第三方副本可重定位。本机amd64仿真无法直接启动原AppImage外壳，原生Linux入口、目标服务器及GUI手测仍待验；不能把包内程序通过写成全平台或GUI通过。GUI没有MeshLib入口，但同一ON应用启动会加载这些动态库，边界及证据见[Linux验证与交接](../validation/2026-10-19-linux-github-packaging-review.md)。

### 既有检测与修复路径

GUI 修复入口经 FixModelByCgal 调用模型修复核心。repair_model 创建隔离的 staging 模型，选择 MODEL_PART 体积；修复前检查、网格修复、属性校验成功后才提交。repair_staged_model 供已隔离的调用者使用，失败结果不可直接写回原模型。

### 检测／修复结果的只读证据

`ModelRepairEvidence` 不构建自动流水线。默认 `--check-model` v4 原六字段与退出码不变，仅显式 `--repair-evidence` 时附加 `boundary`（当前 `schema_version=2`），按 `inspection`、`repair`、`proper_crossings`、`sections`、`reconstruction` 表达观察与能力边界。GUI 没有新菜单或自动判断，普通导入、原高级修复、手动重建及模型特征检测不因此增加预检；后续是否调用其他修复由后端或用户决定。

`ModelCheckWorkflow` 仅对显式“检测＋高级修复＋证据”验收整个流程是否完成：正常高级无进展保留unresolved，完成最终产物检测/有效预检后正常返回，不再仅因无修改而抛旧异常。显式转换不能被提前返回跳过；预检对最终保留网格只运行一次。最终健康可免预检，残缺结果须严格通过boundary的完整性/准入判断；partial或unresolved遇预检异常、超限或缺证据都保留产物并报执行未完成。旧调用无进展仍抛原异常、无新增扫描。该规则不要求几何阻断的check_complete强制为true，不清零缺陷、不修改算法、不调度重建。

`inspection` 区分未执行、通过、已知缺陷和不完整，保留已观测缺陷、计数是否完整、未完成检查与结构检查阻碍。检测截断但已有阳性证据时不丢掉已知问题，也不将未知部分当作零；完整七项与部件/壳体结构分析不能混为一项。`repair` 记录原修复报告的实际执行、变更、未解决和失败状态；引擎自身验收不冒充修复后完整检测。

检测面数/顶点预算通过 `ModelInspectionLimitError` 标识，组合工作流记录 `resource_limit`；该类型保留旧 `runtime_error` 继承与消息，不改变门槛或退出码。其他异常按明确执行阶段分类，不解析英文报错猜原因。旧高级核心已吞并为失败报告的异常仍是 `repair_failed`，不能把它进一步伪称为确定的内存不足或几何无进展。

证据未完成保留 `repair_evidence_incomplete` 包装及既有数值码；投影追加 `failure_stage/failure_cause`，从失败子阶段保留截面超限/取消/输入原因、已确认检测预算和局部分类预算，`failure_category` 不再将这些资源保护一律归为 execution。只认明确结构化原因；未知/不适用项仍按 execution，已完成观察的历史 reason 或后续摘要不覆盖早期IO等失败。截面、检测、分类不完整仍 blocked，不据资源失败推动重建。

只有显式“检测＋高级修复＋repair-evidence”且修复正常结束、当前保留网格仍有完整缺陷证据并通过既有重建准入、尚无直接几何依据，才执行 `inspect_model_section_evidence`。未改变的网格复用已有检测；有改变时检查实际导出回读网格并将该副本复用于预检。默认参考 Z 中间平面间距不超过0.2mm、最多2000面，保留30秒协作预算、300万输入面/顶点与1000万潜在三角面-层位相交保护。超限不降采样冒充完整；单次旧调用不保证硬中断。普通旧调用不做额外扫描，健康/失败/未知分支也不跑预检。

直接几何依据复用既有 CGAL 相交对，不新增全对搜索：组合流程开启 `ModelInspectionOptions::classify_proper_crossings`，以 EPECK 的非零交段及下述严格跨面/单侧共面邻接证明确认表面穿透。首个阳性可以结束存在性分类；阴性须完成声明范围内的分类，不能只看 detail_limit 的前200个。原搜索截断或摘要不一致均不能推荐；原七项计数、原 face ID 和副本身份保持。`elapsed_ms` 仅分类开销，不含原搜索。正常旧入口默认不开启，原生 GUI 高级修复和手动重建不因此加扫描。

`ModelRepairEvidencePolicy::allow_boundary_closure` 默认false；CLI只能在上述完整组合显式加 `--repair-evidence-close-boundaries 1`，表示后台确认输入应为闭合实体且允许封口，不表示当前操作会封口。维护台证据模式提供默认关闭的对应验证复选框；开放薄片不能借此自动加厚，格式/面数/对象数不能代替设计约定。历史 schema1 只读兼容，不为旧报告补造穿透分类或封口授权。

同一轮 Z 切层同时取得原始闭圈/开放链和经普通 `slice_mesh_ex` 同规则整理的 ExPolygons；不重复切层、不增加全量二维自交检查、不生成墙/填充/支撑/G-code。有效区域统计、全空区域、非退化闭圈被抵消及最多16个位置样本补充旧开链/补缝数据；绝对闭圈面积和包含洞及重叠，不能当预期实体面积或丢失比例。CLI 无工艺上下文，明确 reference_z_midplanes/printer_layer_plan=false；共享接口可接受调用方完整层位及有效参数，坐标按变换后的毫米解释，身份/完整计划由调用方负责。异常/取消/超限不改变旧返回或产物；内部 elapsed_ms 与包含网格准备的 workflow_elapsed_ms 分别记录。规模保护不是硬 RSS 限制；区域在全 Z 生成后汇总释放，不能宣称恒定内存或30秒硬截止。

`evidence_present` 统一要求高级正常结束、完整已知缺陷和重建准入 eligible，依次接受 `proper_crossing_after_repair`、明确封口策略下的 `open_boundaries_under_closed_solid_policy`、原 `no_effective_regions`。前两者命中时截面状态为正常 `not_run/reconstruction_evidence_already_present`，不重复预检；第三者仍要求完整 Z 计划全无有效区域且存在非退化闭圈、没有能主动删区的参数干扰。开链、自交总数、单个空层、非流形数量或面积跳变不独立触发。其余 undetermined，失败/取消/未知 blocked；applicability 独立准入，necessity=not_established、automatic_adoption=false，不能把质量策略建议说成原模型必定切片失败或候选必定保真。not_needed 仅表示当前完整网格检查通过。

穿透分类仅在显式证据组合启用，默认 `classify_proper_crossings=false` 不新增工作。原双三角片严格跨面快速路径优先；未命中时允许单侧共面双邻面补证：另一侧原片必须严格跨面，精确非零交段整体落在支持边，支持边全网格恰两非退化邻面，邻面共面且第三点异侧。只复用已发现的交对，不新增全对搜索；最多4096个候选、300万面的一次局部邻接收集，每边最多保存3个face ID即可拒绝多面共边。健康无交对或快速路径命中不扫描邻接；预算不足明确 incomplete。曲面/不共面邻面、双侧补证、共享多面边不证明穿透，避免将两实体边接触重组为交叉曲面。

`proper_crossings` 自身schema2保留原pairs计数，增加范围/分类完成/预算摘要和原pair的2面或3面支持见证；消费者检查kind/source、支持面互异与锚点、邻接实际扫描完成。原无版本strict摘要兼容，但不能把新版patch元数据删一部分伪装legacy。负结果仅表示声明范围未证明穿透，不是全几何或实体重叠保证；原完整搜索要求不放宽，四面双patch即使ID合法也不能形成建议。

普通高级修复和独立重建 CLI 也仅在 --repair-evidence 时投影已有结果，不增加检测/预检。重建投影保留 shape_structure_guaranteed=false、printability=not_assessed；共享候选核心/GUI 不无条件改变报告，不改变原偏差告警、风险确认、人工采用和撤销语义。

共享 CGAL 修复在 info 日志记录 `stage_finished` 与 `timing_summary`，以单次调用 `repair_id` 关联、单调时钟毫秒计时。统计仅覆盖该次 CGAL 候选生成/校验，不含外层模型检测、绘制恢复、导出或 GUI 等待；阶段包含其中的检查点工作，不能将汇总时间与阶段时间重复相加。`candidate_rejected` 不等于整个模型失败，外层仍可能采用已验证的部分成果；异常退出单独标为 `exception`。

局部拓扑准备记录 `soup_topology_result`：`face_limit`、`boundary_query_budget`、`boundary_split_limit`、`boundary_pass_limit`、`removed_face_limit`、`interior_contact_limit` 表示对应资源保护；`no_topology_change` 表示无适用修改；其余 `orientation_unproven`、`candidate_*`、`removed_face_*`、`float_output_invalid` 等说明几何证明/验收未通过。跳过本分支仍走既有后续处理，不把超限说成整个引擎不能修复；全部限制未改。

独立重建由用户明确发起：先验证对象适用性并冻结源身份，复制选中对象，按 `instance × volume` 变换到毫米空间，再交给受限子进程。worker 清除精确零面积／重复三角形，直接对缺陷三角形汤生成近似外包表面；不要求输入先成为闭合、单壳、方向一致的实体。输出必须闭合、流形、无自交、围成方向正确的正体积，并通过共享 `inspect_model_mesh` 的完整七项检测；合法把手、分离实体及正确朝向的内腔不是输出缺陷。逆变换后的模型局部网格和重新变换到物理空间的浮点网格也须通过同样复检。父进程只接收大小受限、任务身份和校验值一致的候选。原模型保留，GUI 显式确认后才用既有原子提交／Undo／备份机制采用；CLI 仅保存新产物。

开放面直接包裹可能只得到不可打印的薄壳。重建默认启用 `close_open_boundaries`：在私有副本上尝试封闭边界，必须先可转换为非自交流形、全部补面成功，再对实际 float 网格进行闭合、体积/壳体方向及完整七项检查，成功才替代包裹输入。最多 64 环、每环 512 边、合计 2048 边；准备网格上限为清理后输入面数加 2048，受同一 worker 的时间/内存预算限制，不能用较低的最终输出上限拒绝高密度输入。使用固定 CGAL 支持的 iterator 补洞接口，禁止 cubic 回退。闭合输入不补面；失败的受控几何/预算检查回到原汤，检查引擎异常则停止，不伪装成可继续的几何缺陷。

该步骤可能填上有意保留的开口，GUI 明确提示并可关闭，CLI 使用 `--reconstruction-no-close-open-boundaries` 关闭。`engine.boundary_closure` 记录边数、环数、补面数、是否采用及退出原因；原输入有开边但未采用封闭副本时，`engine.metrics.open_surface_envelope_warning=true`，提示候选可能仍是薄壳、需检查墙和实体路径。尺寸和双向距离仍与原汤比较，不用封口副本掩盖形状差异；这不是自动识别设计意图或自动可打印性验收。

候选的 3D 凸包也在 worker 内计算、复检并以有界协议返回，主线程只验收和安装，不再执行 QHull；安装后清除体积二维投影及实例轮廓缓存，避免沿用原模型的排布范围。计时／内存预算覆盖 worker 的重建、严格几何检查和凸包处理，不包括主进程的有界读写、预览截面和导出，不能称整个 UI 操作具有同一个硬截止时间。

以上受限子进程、预封口和协议传输描述仅适用于默认 Alpha Wrap。MeshLib 的生成、坐标验收和凸包在同进程后台任务中完成，其协作预算从准入估算前开始，覆盖输入检测、SDK、候选/坐标验收和凸包；初始 STL 读取/工作副本准备及最终 CLI 导出/回读/哈希不在此预算内。两端复用几何验收规则，不共用停止机制。

## 配置与约束

几何是否需要修复、是否允许拆壳／删除非三维部件、是否允许清除绘制与能否保色是独立政策。GUI／CLI 的默认策略必须分别查看调用处，不能因为共用 CGAL 就认为都允许丢弃材质。相同修复选项下应按同一规则保留几何与属性；现存入口差异需记录原因／影响，新改动不能只更新一端。

闭合候选及 polygon-soup 候选的形状验收先判断原有部件数、包围盒、面积/体积条件；已确定拒绝时不计算 Hausdorff 距离，日志 `distance_checked=false`，距离字段为未计算的 NaN，不代表零偏差。廉价条件通过时仍运行原有单向/双向距离检查，阈值、候选接受条件、保色、公开检测/修复 JSON 均不变。不增加保守模式、不改变取消架构或 20,000 面上限。

重建首版限一个普通 `MODEL_PART` 和一个实例；阻断附加体积／负体积／修改器、面级颜色与支撑／接缝／绒毛绘制、变层高、手工裙边点、SLA 点位／排液孔、切割关联及可编辑文字／浮雕／纹理。只读拒绝，不清除属性后再继续。健康模型允许用户手动尝试；单个网格内的开放边界、非流形边、自交、方向冲突、多壳／嵌套壳和贯通孔允许进入重建，不与多个 ModelVolume 混淆。孤立非退化面可生成有厚度的壳，但不保证厚度可打印；全退化、越界索引、非有限坐标仍拒绝。清理计数写入报告，仅由已保留三角形计算包围盒，不让废弃顶点影响度量。

默认细节半径 `alpha=0.5 mm`、表面偏移 `offset=0.05 mm`、偏差告警阈值 `0.25 mm`，GUI/CLI/worker/引擎共享 `RECONSTRUCTION_MAX_INPUT_FACES=3000000`，输出仍最多 100 万面；独立 STL 文件大小最多 256 MiB。没有为通过输入限制进行自动减面。三参数须有限正值，offset 不超过 alpha 的四分之一。`max_deviation_mm`／CLI 旧参数名保留，含义明确改为告警而非拒绝，报告 `deviation_policy=warning_only`。先尝试用户参数；估计／中间细分超限或候选几何不合格时，最多再尝试 alpha 与 offset 同时乘 2、4，并记录每轮结果及有效参数，不降低最终七项检查要求。

三次尝试共用同一 worker 预算。共享 `model_reconstruction_process_options` 依据未清理输入面数分档：不超过 20 万面为 60 秒/1024 MiB，20 万至 100 万面为 180 秒/2048 MiB，100 万以上为 300 秒/4096 MiB；自动内存再取物理 RAM 四分之一上限（最低 128 MiB，未知 RAM 回退 1024 MiB）。这不是可用内存探测或完成时间保证。`ModelReconstructionOptions.process` 的零值表示自动，正值保持调用方选择，包括显式 CLI 超时；通用进程 API 不接受零预算，调用前必须解析。GUI 显示生效值，父级报告 `resource_limits` 在成功或 worker 失败时均保留生效预算和面数上限。macOS 为采样 RSS watchdog，不是硬分配上限；Linux 使用 worker 内 RLIMIT_AS 加 RSS 监测，Windows 使用受内存限制并可整组结束的 Job。父进程监督之外，worker 内也监测父进程身份／存活、资源和截止时间，避免父进程退出或 GUI 事件回调停滞后无限运算；Linux 另设父进程退出信号保护。POSIX 父进程 RSS 查询失败时复查子进程退出状态，最多等待两个 20 ms 周期；连续第三次失败仍返回监测失败，不把未知内存当零，各周期仍检查取消／截止时间。平台实现存在不代表均已运行验收。

双向距离为采样估计，不能表述为最大误差保证；超过阈值只告警。缺陷优先重建不以输入代数体积／内部见证点证明结构保持，`interior_check_status=not_preserved`、`reference_interior_check_complete=false`；原有孔、空腔和薄壁可能被填充、合并或加厚，GUI 必须提示并确认。独立 `model_reconstruction_v1` 区分 `candidate_ready`、几何检测、人工确认、采用与试切片；首版 `trial_slice=not_run`、`print_ready=false`，GUI 可明确采用为待试切片候选。网格七项通过不意味着无悬空或能生成首层路径；外部诊断试切片结果单独记录，不伪装为自动流程已执行。check-model v4 原字段和退出码保留，仅显式 --repair-evidence 时附加上述 boundary；旧普通 repair JSON 和高级修复策略不变。

Alpha Wrap 的预封口、三档粗化和 worker 平台限制不适用于 MeshLib；MeshLib 不粗化/回退，使用上文同进程协作控制。面数限制及自动预算分档共用，但 MeshLib 统计整个 Orca 进程的 RSS，不是仅 SDK 增量内存。

CLI可用`--reconstruction-memory-mb`显式传128–4096的整数MiB，复用`options.process.memory_limit_mb`并保持调用者预算优先，不再次按物理RAM缩小；不传则默认行为不变。此为服务调用配置，不是新修复策略；没有容器cgroup/Job自动探测，应用外多个作业的总资源由调用方管理。GUI无新增资源输入框，两端核心处理同一预算的规则相同。

## 失败与取消

单体积失败或用户取消可导致整个事务回退；CGAL 单次内部操作不保证即时可中断。检查函数抛错不能转换成“需要修复”继续更重操作。成功写文件也不证明修复后的网格正确。

上述线程取消约束属于旧高级修复；Alpha Wrap 重建通过终止并回收专用子进程处理取消／超时。启动失败、内存预算、进程崩溃、引擎异常、报告／网格截断或校验不匹配均不得交付候选，不自动重启 worker。只有 Alpha Wrap worker 内受控的细分预算／候选不合格允许上述有界粗化尝试。MeshLib 在停止请求后等待后台线程返回，不能承诺硬截止或隔离原生崩溃。GUI 等待／审阅期间源网格、体积元数据、变换或相关配置变化后丢弃旧候选，不覆盖现场。

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
