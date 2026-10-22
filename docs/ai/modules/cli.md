# 应用入口与 CLI

<!-- module-review
{
  "id": "cli",
  "source_fingerprint": "sha256:f3d8e8d1d01ae431a16de28a0886ccb5b2b3d3652b585326e64a52954499dfe9",
  "reason": "2026-10-22新增独立CLI opt-in，默认关闭，不由报告启用。逐盘传递Print运行时模式，开启时拒绝无模式身份的切片缓存导入/导出；未改修复、分盘、普通切片参数和默认输出。17场景脚本与真实模型开关对照见实施记录，GUI按用户要求不接入。"
}
-->

## 职责与边界

本模块负责应用入口与 CLI。归属以[模块清单](../modules.json)的 `cli` 为准。

## 入口与调用

- [OrcaSlicer.cpp](../../../src/OrcaSlicer.cpp)：main → CLI::run；动作、模型加载与导出路由。
- [OrcaSlicer.hpp](../../../src/OrcaSlicer.hpp)：CLI 动作与配置成员的接口边界。

## 核心流程

`--optimize-model-detail-paths`是独立、默认false的CLI运行时开关，显式无值或`1`开启、`0`关闭。开启时才使用薄墙总退化恢复及可变线宽分组/体积修正；未传时保持修正前切片路径。需要一个`--slice`动作，开启时拒绝`--load-slicedata`/`--export-slicedata`，避免缓存缺乏算法模式身份；不修改普通缓存调用。每盘设置Print运行时状态并在开启时记录日志，不保存到参数/预设/工程。GUI按用户要求不接入，仍为默认关闭；分析报告开/关与该选择完全独立。平台要使用候选修正必须显式透传此CLI参数，不能仅部署二进制就宣称启用。

`--slice-analysis-report` 是默认关闭的只读导出选项，仅配合一个 `--slice` 动作及明确 `--outputdir` 使用。完成 G-code 后按盘输出 `plate_N.analysis.json`，不新增 `result.json` 字段、不改变打印参数/客户端通知。现有开关同时传到导出器和GCodeProcessor：未启用时不收集新增运动/流量/层用料统计，也不进行报告几何遍历或JSON导出；原预览、耗材和预计时间解析保留。预检拒绝已有同名报告；报告先写临时文件再无覆盖发布，报告失败返回 `CLI_RESULT_WRITE_ERROR`，保留已生成的 G-code。只有本次进程成功且对应盘报告存在才表示所请求报告完成，不能复用旧报告。输出目录应每任务独立；GUI 没有新控件，不调用报告核心。指标语义、限制和验收见[实施记录](../validation/2026-10-18-slice-analysis.md)。

报告沿用 schema 1，保留逐层计划足迹、底面占用、成本及材料流量/冷却观察，不新增动作或自动选参。本轮新增只读 `gcode.quality_paths`，复用既有moves遍历汇总质量相关角色的正挤出路径、指令速度/风扇和名义宽高；不是恢复已删除的角色运动估计器、加速度或名义流量统计。材料峰值及超配置cap距离使用同一已定型运动块，保留未知和不适用，并对合法float端点舍入仅归一化报告副本。这些不是实机测量，计划足迹也不等于最终打印覆盖；参数不合法或证据缺失不输出质量通过。`test_cli_slice_features.py` 验证同几何两种墙生成器以及稀疏内部/顶面的正常反例；启用/关闭报告仍核对最终指令与有效配置一致，普通调用不生成报告。

报告现在只保留能支持调参的二维覆盖/孔缝/窄宽、床面面积与成本/流量/冷却及质量角色执行证据；停止三维名义连通、源面/体积采样、连续深度、跨层投影及区域/接缝坐标明细，不保留旧模式或新版本开关。最终 JSON 保持紧凑序列化，字段数值不因缩进变化而改变。消费者与范围详见[G-code 报告模块](gcode-preview.md)。本轮未修改CLI动作源码；不改切片、分盘、修复命令或输出失败语义。

现有报告另含每组单个峰值层的 `solid_fill_peak` 来源摘要，区别非桥接顶底、内部实体、桥接和跨类重叠；仅解释已有总未覆盖最大值，不重复计算所有层分类。验证器兼容无摘要旧报告；有摘要时核对真实最大值、层/Z、互斥类别之和和完整性。无新CLI开关或原生GUI入口，默认不生成报告时不调用该分类。

残余质量观察另含无路径岛峰值与最终分区轮廓的交/差、孔交叠峰值的互斥路径来源，以及G-code同段线宽/层高观察。验证器兼容旧报告缺项；新摘要核对真实峰值/分类和，同段宽高核对范围/比例/数量与长度，桥接不套用普通挤出判断。只读报告不自行修改参数或重新切片，普通动作、输出保护和退出语义不变。






业务阶段交付仅显式 `--model-stage-report` 生效，允许在check-model及独立重建入口输出固定 `model-stage.json`；`--model-reference` 指定统一原始STL基准，不能单独使用。其他动作/无动作GUI拒绝这两个显式选项，不默默改变普通切片。旧result.json与reconstruction.json语义不变，默认不增加比较成本；新报告必须与exit0、实际产物hash共同验收。后端选择与失败兜底不放进Orca路由，见[修复模块](model-repair.md)。

阶段报告的原始STL基准／导出回读精确共享重合顶点，避免把每个三角面的三个角点重复计入截面顶点预算；不移动坐标、不删面、不改文件哈希，不改变现有参数、资源上限或退出语义。该共享选项只由阶段读取开启，普通独立重建、GUI与切片导入保持原路径。当前整合已包含 master 的固定位置修复；合并后的阶段、报告开关及固定位置组合回归结果见[实施记录](../validation/2026-10-18-slice-analysis.md)，不以此前单项通过替代整合验收。

独立 `--reconstruct-model` 的可选本机评估后端使用 `--reconstruction-engine meshlib --reconstruction-voxel-mm <毫米>`，仍要求风险确认和新输出目录。同一Orca进程内部调用SDK，不再接受 `--reconstruction-worker-path`，不启动MeshLib或Orca修复子进程；默认Alpha Wrap保留原worker。普通检测/高级修复/切片的业务不变，不新增自动编排或原生GUI入口。引擎/体素仅在此入口解析；未编入SDK、参数混用或失败不静默回退。启用SDK构建依赖本地动态库，缺库可在进程启动阶段失败。输出仍为 `candidate.stl`、`reconstruction.json`，报告包含同进程身份与协作预算边界；成功仅表示候选及导出验收完成。详细结果/授权/结构保留边界见[模型修复](model-repair.md)。

`main()` 进入 `CLI::run()`，解析动作、变换和配置，加载模型后按请求分盘、切片或导出；需要交互界面时转入 GUI 启动。模型导出走 `CLI::export_models()`，工程导出走 `export_project()`，不是同一种输出。无 GUI 的业务依然复用 Model、Print 和文件格式层。

独立 `--reconstruct-model` 与内部 `--reconstruction-worker` 在 `CLI::run` 的普通解析、预设和 GUI 启动之前分流到 ModelReconstruction。前者要求一个无色 STL、尚不存在的 `--outputdir` 和显式 `--accept-reconstruction-risk`；后者只用于受控任务目录中的 worker 协议。GUI 与公共 CLI 共享候选核心，不通过旧 `--repair-model cgal` 或 `--check-model` 间接执行重建。

`--repair-evidence` 是非动作选项（默认关闭，显式 `0` 关闭），不新增修复命令、菜单或编排。默认 `--check-model` v4 仍为六字段；显式开启才附加自身 `schema_version=2` 的 `boundary`，包含 `proper_crossings` 摘要与 `reconstruction.policy`。普通 `--repair-model cgal` 和独立 `--reconstruct-model` 也只在此选项下投影已有报告，保持各自原有投影，不升级为模型检测证据协议；开启选项却不包含上述检测／修复入口时受控拒绝，不能借此启动 GUI 或改变普通导出。原检测七项计数、修复参数和旧入口退出码不变；后端自行调度，程序不因证据字段执行重建。

仅组合 `--check-model --repair-model cgal --repair-evidence` 采用流程完成语义：正常无进展且最终证据完整有效返回0、空error_string，保留unresolved/needs_repair及缺陷。共享工作流仍执行显式格式转换和最终复检。最终健康可跳过预检；残留缺陷须通过完整性及eligible准入检查，已确认真实穿透或允许封口策略下的残留开放边界可直接形成建议并跳过预检，否则最多执行一次完整有界预检。无确定异常的完整观察可以完成，但任意undetermined/缺证据不能直接算完成。预检异常、超限、未知或未完成仍非零（沿用既有码），部分成果照样保留；不把所有-25改0。原生GUI不走该CLI工作流，行为不变；本地维护台按真实命令及返回值显示“已完成，模型未解决”，不改旧231历史。

只有该显式组合复用当前保留网格的同一轮自交搜索，以精确几何判定区分已证明的表面穿透与共面重叠、点/边接触；不重新进行全量搜索。`evidence_present / proper_crossing_after_repair` 需要完整检查及非零长度交线上的跨面证明：保留原双三角片严格跨面，新增有界单侧共面双邻面补证；禁止两侧都重组邻面、共享多面边直接升级。具体证明与4096候选/300万面预算见共享模块；摘要自身schema2记录支持面和分类完整性，超限不能当阴性或建议。允许封口策略下的残留开边对应 `open_boundaries_under_closed_solid_policy`。这两类明确依据使 `sections.status=not_run`、`reason=reconstruction_evidence_already_present`，不是漏执行。只有未取得明确依据的残留缺陷才追加一次 Z 观察，复用普通区域构造得到有效 `ExPolygons`，不生成墙、填充、支撑或 G-code。健康网格直接复用无交对摘要并跳过截面；只检测和默认旧调用不启用该分类或截面，未知／失败结果不继续升级。

证据流程未完成的既有退出码与 `failure_kind=repair_evidence_incomplete` 不变；`boundary.reconstruction` 新增已知子阶段的 `failure_stage/failure_cause` 并正确派生 `failure_category`。如 `sections/plane_limit` 为 resource，取消为 canceled，未知为 execution；不从英文报错推断，不用后续证据覆盖旧IO失败，不放宽blocked或流程完成语义。无新CLI开关或GUI入口。

原有 `evidence_present / no_effective_regions` 保留为另一条充分依据：完整参考 Z 计划全部无有效区域、存在非退化闭圈、无主动删区参数干扰且准入为eligible；不再把它当作所有模型都必须满足的前置条件。开链、单空层、面积变化、自交数量或非流形数量不能单独触发。CLI 未加载工艺，参考层位不等于实际打印层；建议重建不证明原模型一定切片失败，也不证明重建候选可打印。GUI 仍走既有手动修复与候选确认，未新增此评估的展示或自动执行入口。详见[协议](../../../doc/model-check-workflow.md)与[共享模型修复](model-repair.md)。

分离截面间距继续作为同一报告逐层字段输出，无新 CLI 参数。它是保留参考截面不同岛的最近边界距离，包括孔内岛；不足两岛为不适用，失败未知，零不代表粘连。象脚层的参考截面不等于补偿后的实际路径，不把它写成参数效果判断。原 34 场景配置保留，另加已知 1 mm 间距的孔内岛双墙夹具；实际通过/失败和程序身份见实施记录。


## 配置与约束

平行边宽度及材料峰值/超cap距离/冷却仍在同一显式报告；最终G-code空移长度、接缝计数、角色名义流量统计已停止。无新动作、参数、客户端通知或自动选参。40场景真实切片及报告开关保护检查见实施记录；普通GUI/CLI没有报告几何计算。处理器取消非挤出段额外G1距离计算，不改生成指令、估时或GUI挤出距离。




普通 STL/OBJ 等没有盘元数据的输入在加载阶段逐实例加入初始盘，使用 `add_instance(..., false)`，
不移动模型；后续主动摆盘仍可重建归属。`--arrange 0 --auto-plate 0` 因而在空盘过滤前具有正确成员，
固定位置模型正常切片，越界输入仍进入切片校验。已有 3MF 的盘结构继续由导入数据建立。
在所有变换／贴床完成后，原始网格、合并模型或存在切片动作时无移动地刷新盘成员，不重排对象或物理盘；无摆盘的原始输入仍登记到初始盘，
仅导出合并工程也须刷新，避免旧对象下标写出不存在的3MF对象引用；非合并、非切片的已有工程保留原盘元数据。保证无效输入不会被当成空盘。全部盘切片对无盘元数据的 STL/OBJ 批量输入逐实例检查，启用模型全部越界或摆放溢出时，
在生成任何 G-code 前失败，不能仅切成功的子集。保存的工程保留 GUI 语义：未归属任何盘的备用模型不属于盘切片目标；
显式盘号只检查目标盘。JSON 中的虚拟盘号不参与此选择。最终盘号按摆盘后的真实盘数重验，无效盘号返回 -2。
CLI 预检查与 Print::apply 一致，关闭打印的对象／实例不参与越界拒绝、可打印实例统计和三角面数限额；
显式选择全部禁用盘仍返回 -50，明确 skip-objects 后无对象沿用 -60。界面端的实例级越界汇总也过滤打印开关，
保留禁用体积的可视状态；实际操作尚待手测，见下方记录。
每个 FFF 导出必须得到非空普通 G-code 文件；一次 `--slice` 没有任何成功导出时返回
`CLI_NO_SUITABLE_OBJECTS`（JSON -50），不再报告 All done, Success。仅导出未切片工程不受影响。
本改动补 CLI 生命周期及界面禁用实例的检查入口，未修改共享排布／切层算法；回归及部署交接见
[固定位置切片修复记录](../validation/2026-10-19-cli-fixed-position-slicing.md)。

`--export-model-transforms` 仅在已解析的多盘模式（auto_plate_value != 0，含历史省略默认）中重映射越界/无归属实例：先核对 `contain_instance` 归属，再用只读 `check_outside` 及共享 `calc_print_volume_state(BuildVolume)` 复核当前几何与真实床面轮廓，避免异形床只按外接矩形误判，区分真正盘内与仅有编辑归属；不依赖可能在 `--ensure-on-bed` 后过期的盘外状态缓存，每个越界实例按输出遍历顺序分配一个独立 `plate_index`，从实际盘数量开始递增，XY 为有效 `printable_area` 包围盒中心（盘内坐标），Z固定为0，表示独立返回盘上以底面贴床呈现；旋转和缩放保持实际输出。普通盘内实例保持原盘号与坐标规则，包括原有正/负底面Z；字段结构不变，不增加 `outside`/`position_space`。多盘不重排导出也沿用此规则，正常原始模型先登记归属而不误当盘外。显式 `--auto-plate 0` 不创建虚拟返回盘、不居中、不归零：保留实际归属，未登记实例沿历史回退到盘0，XYZ为实际包围盒底面中心；重开多盘工程但不重排时也保留实际归属和世界坐标。只读投影不修改模型或盘列表，不受真实36盘容量限制，也不改变实际排布、切片退出码、GUI或3MF。返回虚拟盘号不能当作真实切片盘号；复现与对照见[验证记录](../validation/2026-10-17-cli-oversized-arrangement.md)。

命令行加载的机器／耗材／工艺配置与输入工程的配置合成为有效配置，再应用动作及对象覆盖。输入顺序、单位变换、多盘及颜色分组都会影响输出，不能只比较命令末尾几个参数。日志、进程退出码与结果 JSON 分别核对。

`--export-model-transforms` 仅在返回 JSON 中重映射越界/无归属实例：先核对 `contain_instance` 归属，再用只读 `check_outside` 复核当前几何，区分真正盘内与仅有编辑归属；不依赖可能在 `--ensure-on-bed` 后过期的盘外状态缓存，每个越界实例按输出遍历顺序分配一个独立 `plate_index`，从实际盘数量开始递增，XY 为有效 `printable_area` 包围盒中心（盘内坐标），Z固定为0，表示独立返回盘上以底面贴床呈现；旋转和缩放保持实际输出。普通盘内实例保持原盘号与坐标规则，包括原有正/负底面Z；字段结构不变，不增加 `outside`/`position_space`。此规则同样用于显式单盘和不重排导出；只读投影不修改模型或盘列表，不受真实36盘容量限制，也不改变实际排布、切片退出码、GUI或3MF。返回虚拟盘号不能当作真实切片盘号；复现与对照见[验证记录](../validation/2026-10-17-cli-oversized-arrangement.md)。

`--repair-evidence-close-boundaries 0|1` 默认false，仅允许与 `--check-model --repair-model cgal --repair-evidence` 同时启用。值1是调用方明确承诺“输入应为闭合实体、允许填补剩余开口”，仅影响重建建议，不执行封口、重建或采用；不从STL格式、面数或组件数猜测设计意图。`boundary.reconstruction.policy` 记录版本1及实际 `allow_boundary_closure`。显式传0也必须在该组合使用；普通修复/导出和独立重建早分流均拒绝这个策略参数，独立重建仍使用自己原有的风险确认及封口参数。该选项不是对旧 `--auto-repair-model 0|1` 的替代。

`--slice 0` 全部切片跳过空盘及全部实例明确关闭打印的盘，不因这些盘在 apply 后没有打印对象而中断任务。显式指定该盘仍执行原有校验；启用但越界的模型不因此跳过。`--min-save --export-3mf` 使用共享导出快照，只写选定的可打印盘的 G-code/切片信息，未选中盘仅留编号空槽兼容既有读取器；参与打印但未切片、缺失 G-code 或没有目标盘仍失败。普通带模型工程允许包含未切片盘，不套用切片包限制。

重建首版仅为模型候选入口，拒绝额外输入、普通切片配置／变换和未白名单参数；STL 数值按毫米解释，不猜单位或采用原修复的 geometry-only 导入降级。公开参数为 `--reconstruction-alpha`、`--reconstruction-offset`、`--reconstruction-max-deviation`（毫米）及 `--reconstruction-timeout`（秒）。其中 max-deviation 保留参数名但改为偏差告警阈值，不再因超过它拒绝已通过完整网格检查的候选；帮助和 `deviation_policy=warning_only` 明确这一点，与 GUI 告警文案一致。最多三档尝试共享总预算，报告包含每轮及最终 alpha/offset。默认几何／资源策略见[模型修复](model-repair.md)。由于首版尚未自动试切片，不能把“同一预设名”或 CLI 候选生成当成与 GUI 同有效配置切片对照已完成。

## 失败与取消

重建输入上限为 300 万面且 STL 文件最多 256 MiB，超过任一限制即受控拒绝；输出仍最多 100 万面。未传 `--reconstruction-timeout` 时共享核心按模型面数选择 60/180/300 秒，传入的 1–300 秒覆盖自动值。可选 `--reconstruction-memory-mb` 严格接受十进制整数128–4096，覆盖自动内存预算；未传时仍共享GUI分档及物理RAM约束，不由STL字节大小替代面数。该服务配置仅在CLI暴露，GUI没有资源手填项，但两端共享同一有效预算规则。MeshLib统计整个Orca RSS、AlphaWrap统计worker预算；不自动检测容器限制，不宣称硬隔离。`resource_limits`记录实际生效值，超时/内存不足也保留；具体分档见[模型修复](model-repair.md)。

输入解析、参数组合、IO 和切片校验失败有不同出口；分盘未排入与进程成功不能画等号。应同时核对盘归属、警告和实际产物。用户没有要求时不要启动真实设备任务，也不要把运行生成的 result.json 入库。

重建不覆盖源 STL／已有目录，不生成旧 `result.json`，仅在本次创建的输出目录保存 `candidate.stl` 与 `reconstruction.json`。拒绝／失败不给出可用候选；读取和报告中明确区分候选成功、形状未确认、`trial_slice=not_run` 和 `print_ready=false`。公共命令退出 0 只能表示候选工作流成功，不表示可打印；专用 worker 退出状态和业务拒绝报告分开解释。

Alpha Wrap 默认 `parameters.close_open_boundaries=true`，与 GUI 的“包裹前尝试闭合开放边界”相同；`--reconstruction-no-close-open-boundaries` 显式关闭。该参数完整传递至 worker，旧内部请求缺字段时取默认 true。封闭副本必须通过完整检查才能用于包裹；有意开口可能被填上，仍要求风险确认。报告新增 `engine.boundary_closure` 与 `engine.metrics.open_surface_envelope_warning`，分别解释预闭合结果和原汤包裹的薄壳风险；不能仅因命令退出 0 推断墙/填充可生成。不改 `--check-model` v4 或旧 repair JSON。MeshLib 不接受此 Alpha Wrap 专用参数；其超时/RSS为协作预算，覆盖准入及生成/验收而非最终文件导出，并明确报告非硬限制。


保存工程与本次新增 STL/OBJ 混合时，盘外备用对象豁免按导入实例身份判断，合并模型时传递身份；新增输入不会因批次含3MF而漏检。显式选择盘也不能略过本次新增但完全无归属的启用网格，避免只切成功子集。

全局 `--rotate/--rotate-x/--rotate-y/--scale` 原来作用于 volume；变换 JSON 需将实际 volume 的前后差值与实例及自动定向变换合成，不能只读取实例字段。按对象保存差值，拆色和repetitions复制时传递变换记录；不修改原有几何操作。仅在请求JSON且存在全局变换时计算volume差值。JSON导出对全局变换、左手镜像实例或有自动定向记录的对象使用实际矩阵分解，将反射方向保存在有符号scale中，正确组合实例镜像与orient；即使没有全局选项，镜像3MF也不能只输出旋转和正缩放。负号可等价归到X轴并配合旋转，不要求保持源文件的镜像轴标记；应以还原几何比较。非等比实例缩放与orient不一定可交换，必须按实际顺序组合后分解，不能直接将原缩放附在组合旋转之后；可表示的变换正确导出，产生剪切时沿用下述明确拒绝语义。普通无镜像、无定向、无全局操作的入口保持原行为。若多部件各自变换不等同于一个整体仿射变换，或分解的合成矩阵有剪切、奇异或非有限值，现有 rotation/scale 字段无法忠实表达，返回 -2 并说明原因；关闭JSON的3MF导出仍沿用原行为。单独per-model参数行为保持。该协议仅CLI提供，GUI不输出此JSON；共享工程导出保护见[格式模块](formats.md)。

## 修改与验证

修改动作路由优先检查 run/export_models 和结果序列化；验证无参数启动、模型导出、分盘→切片和不合法参数。先查看本分支可用的 CLI 脚本，测试写入隔离输出目录。共享功能同时核对 GUI 的调用及生效配置，验证实际产物与错误语义一致；对应 GUI 步骤按[交接规范](../gui-validation.md) 提供，不能只凭 CLI 退出 0 结束验收。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

独立重建另用 `scripts/test_cli_model_reconstruction.py --binary <当前应用可执行文件> --output-dir <全新证据目录>`，校验二进制／输入 hash、独立报告、候选 STL、输入与既有输出不变；脚本不构建、不试切片、不打印。运行身份与结果在[主记录](../validation/2026-10-04-smart-print-engine.md)，GUI 路径和具体对照在[交接清单](../validation/2026-10-05-reconstruction-gui.md)。

现有三个修复CLI脚本覆盖默认旧协议、显式on/off证据、非法组合、输入/早期参数失败，以及相同模型修复/候选身份和原件不变；不另设自动修复协议脚本。`test_cli_model_check.py` 新增schema2、真实穿透见证、封口策略开/关/默认、无开边不误触发，以及新参数在普通入口/独立重建及显式0时拒绝的契约检查；用高级修复实际保留网格验证建议，不能以修复已通过的缺顶方块冒充残留开边触发。只检测不运行分类或截面观察。脚本覆盖不等于已运行，统计与平台/GUI状态见主记录，不能据证据字段宣称已经可打印。

## 分支现状

master 包含独立 `--check-model` 工作流、受约束的高级修复与对象分盘接入；检测只允许单个未变换 STL/OBJ，不能与任意切片变换混用。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[配置定义、预设继承与资源更新](presets.md)、[自动分盘与打印板](arrangement.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
