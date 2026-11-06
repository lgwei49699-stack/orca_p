# OBJ 三部件树状支撑切片失败排查

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前状态与下一步

- 最新（2026-10-15 22:05）：用户授权线上只读查询，已取得17/58/对照模板机器工艺及历史记录；禁区192和限高206的4个任务全部按线上参数复现。17为左上角X0～5/Y90～100禁区；58实际限高178mm，模型超0.016/1.034mm。X+2mm、缩放1.72并开支撑分别通过本地切片；详情和构建身份见文末“线上查询确认”。未修改线上数据。
- 2026-10-15 用户最新27条失败已进一步下载原模型、计算位姿并做候选配置单因素试切。新增结果：7条关联模型在系数2→0.5后恢复；2条文字模型还需降低最小墙线宽或进一步降低短墙系数；3条路径冲突在树支撑工艺中复现，首组仅关闭支撑可消除；3条EP7的156关联模型底部路径缺失、开启支撑可生成GCode。以上使用保存/通用工艺，不能冒充历史最终模板快照。禁区2条仍缺模板17机器JSON；206两条仍缺模板58有效机器体积/盘信息。1.76倍EP7模型加垫层后输出最高打印Z=180.37mm，超过通用180mm限高，不能把exit=0当作可安全打印。桌面TXT/JSON已更新，另有8模板只读导出SQL；完整新证据见文末。
- 2026-10-15 新日志任务 `1560369123123318784` 已取得原 STL、工艺 JSON 和 3MF：经典墙、检查薄壁关闭。原配置在本地复现完全相同的打印 Z=7.07～7.87 mm 空层/156；仅开启 `detect_thin_wall=1` 恢复三层路径并生成 GCode，仅将 `min_length_factor=2` 改为 `0.5` 仍失败。本例是薄壁路径缺失，不能沿用旧水壶的短墙过滤结论。成功结果仍含悬空区域告警，待 GUI 预览/实物验证；未修改线上模板。当前仓库 `master`、HEAD `c893a4e3ef75acd3c6fdab1b9eb75764bc0e92ed`，工作区其他未提交修改保留，线上构建提交仍未知。
- 用户任务：分析线上任务 `1559221689451700224` 的退出码 156；只做诊断，未授权修改线上配置或执行打印。
- 基准：本仓库 `master`，HEAD `a08573a374b3ada2d24122d3c1e2000ce6678e23`；线上日志自报 Linux Orca 2.3.2.0-Release，具体提交未知。
- 当前源码 `CLI_SLICING_ERROR=-100`，进程退出码低 8 位为 156；该码是通用切片失败，不能单凭它判断 OBJ 格式或树状支撑错误。
- 后续用户提供 2026-10-13 09:51 的完整线上日志（新任务 `1559503884967653376`），明确记录第 3 个 OBJ 在 7.27～8.67 mm 空层，随后 `no_check=0` 返回 -100，与本地复现相同。
- 用户指出 GUI 使用 2 可以导出，又亲自验证拆分后也出现问题。已读取 GUI 生成的 GCode/工程快照，并用同一本地 CLI 对照拆分前后工程：1 对象成功、3 对象失败156；拆分前导出物中盖子已缺少连续六层路径，壶身同层仍有挤出，导致按对象的空层检查未检出。
- 诊断阶段完成；未修改业务代码、线上模板或用户原始 3MF。建议原方案将短墙过滤系数恢复 `0.5` 后重新切片并检查盖子预览；不能用合并对象或绕过检查掩盖缺失路径。完整证据见本文后续小节。

## 输入与隔离目录

证据目录 `/tmp/orca-obj-1559221689451700224`，模型按请求顺序为 `part1.obj`（把手）、`part2.obj`（水壶）、`part3.obj`（盖子）。

| 文件 | SHA256 |
| --- | --- |
| part1.obj | 71dc0ad429d344e6a88db556a2babbb845c39878a99b80a0d37789f55b12b3e6 |
| part2.obj | 92c7a06c5d431092ce03308eb557de278f8bfce4f6d2888b07830d6c610b338c |
| part3.obj | df05f75be253e2d361f05cab024d85b1cb3aa12e72a4480941e19ab13d821bcd |
| config.3mf | ee5afda2b6bf7061d254fd6f640ecba79dea8039e1f380af4dd959d766831f0c |

3MF 来自用户提供的 `ee53dcb2-4e97-49b4-b519-d156371ba8d5.3mf`，已提取 Metadata 中机器、工艺、耗材配置。机器为 `EP3 Plus 0.4 nozzle`，工艺为 `灰度测试-树状支撑`，启用 `tree(auto)` 支撑和 `cura_v1` 垫层。三个模型均绕 X 轴旋转 90 度，以请求位置、缩放 1、ensure-on-bed、禁止自动分盘/重排执行。

## 服务代码核对

`Print3DSliceService` 当前 `feature/f_裙边垫层` 工作区干净。多模型位置命令读取全局配置，模型没有 `sliceConfId` 时使用全局工艺。此次请求未提供 `globalAdhesionEnable`，新增全局关闭底座逻辑不触发；旧 `adhesionEnable=true` 不应被解释成新开关。

## 构建、图谱与工作区边界

- 现有二进制：`build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，文件时间 2026-10-10 16:46，arm64 Release，自报 `2.3.2.2-Release`；SHA256 `6ca9a79341c4c9a357184ced662b32f97f21f39b161ef704e30067e66fde8672`。线上自报 `2.3.2.0-Release`，两者不能视为同一构建。本次没有源代码修改，不以旧二进制证明新代码。
- CodeGraph MCP 未暴露；CLI 在 `/Users/liwei/.local/bin/codegraph`；索引存在但提示待同步变更。本次日志与精确代码路径使用源码核对，不宣称图谱完整或最新。
- 开始时已有用户改动：docs/ai/modules.json、cloud/gui-parameters/gui-shell/localization/project-state/scene-editing/test-tools 模块文档、两份既有 validation 文档、中文翻译、result.json、MainFrame.cpp、Plater.cpp、GFDCloudConfig.hpp、test_gfd_cloud_config.cpp；另有 study-textbook 文档及 Thingi10k 下载脚本/测试未跟踪。全部保留。
- 本次 CLI 从临时证据目录运行，输出也隔离，不覆盖仓库根目录 result.json。不触发远程切片、打印、数据库写入或推送。

## 复现方法与限制

- 直接把 3MF 中的 config 文件作为独立 CLI JSON 会因 `from=project` 被拒绝（251/-5），这只是缺少服务端标准化，不能当作线上 156 的原因。
- 按服务源码合并当前仓库 `conf/dev/biz.yml` 的 EP3Plus 公共配置与 3MF 配置，设置 `from=system`。本地公共兼容名称为 `MyMarlin 0.4 nozzle`，同步设置机器名供 CLI 校验，打印区域、喷嘴、层高等仍来自上传 3MF。线上公共配置/数据库最终 JSON 未取得，不能声称逐字节复现线上请求。
- 现有本地 CLI 在 `--slice 0 --arrange 0` 下把板判为空并跳过，返回 0 但没有 GCode，明确不计为切片成功。改为显式 `--slice 1`，保持三个对象的变换和摆放不变后进入实际切片，得到 156。这是本地入口差异，未在本任务顺手修改。
- 命令、配置、日志、结果及 GCode 全部保存在 `/tmp/orca-obj-1559221689451700224`。核心命令记录为 `explicit-plate-command.json`，失败证据为 `explicit-plate.log` 和 `explicit-plate/result.json`；全部对照结果为 `comparisons.json`、`single-parts.json`。

## 单因素实测结果

以下对照均使用同一本地二进制、显式第 1 盘和固定模型变换。除表中参数外保留同一份配置。

| 场景 | 结果 |
| --- | --- |
| 原工艺，三个模型，`min_length_factor=2` | 156；part3.obj（盖子）7.27～8.67 mm 区间空层 |
| 仅改 `min_length_factor=0.5` | 0；生成 plate_1.gcode，结果无空层告警，278242 三角面、5.36 g、1560 s（切片估计） |
| 仅改 `min_length_factor=0` | 0；生成 GCode（只作诊断对照，不建议普遍关闭过滤） |
| 仅关支撑 `enable_support=0` | 156；同一盖子空层 |
| 仅关垫层 `raft_mode=legacy, raft_layers=0` | 156；盖子空层变为 5.8～7.2 mm，少了垫层高度偏移 |
| 仅改 `wall_generator=classic` | 0；生成 GCode（会改变墙生成行为，不作为最小修改方案） |
| 仅改 `layer_height=0.1` | 156；盖子仍空层 |
| 原工艺，只切把手 | 0；生成 GCode |
| 原工艺，只切壶身 | 0；生成 GCode |
| 原工艺，只切盖子 | 156；同一空层告警 |

## 源码解释

- `src/libslic3r/Arachne/WallToolPaths.cpp::removeSmallLines`：非闭合中心短墙使用路径最小实际线宽乘以 `min_length_factor` 作为过滤门槛，顶/底层另有 0.5 保护。值越大，过滤越强；它不是固定毫米长度，也不存在对所有模型通用的失败缩放比例。
- `src/libslic3r/GCode.cpp::collect_layers_to_print`：中间有空的挤出层，恢复挤出时高度间隔超过允许值，产生 `SlicingEmptyGcodeLayers` 严重告警。
- `src/OrcaSlicer.cpp`：正常检查模式下拒绝该告警，记录 -100，进程表现为 156。不要通过 `--no-check` 绕过空层来伪造成功。
- 当前源码默认系数为 `0.5`。`88a8a140d4` 修正显式零值/参数初始化，`b72b8769aa` 修正文案单位；原值 `2` 不因后者改成毫米。不能据此认定线上由某次部署引入，需另核对线上构建及历史配置。

## GUI 复核建议

在用户使用的客户端载入同一 3MF 和三个 OBJ，保持原缩放/旋转/位置，把 Arachne 短墙过滤系数（旧版本可能显示最短墙长度或最小允许的墙长度）从 2 改到 0.5，重新切片，重点逐层看盖子中段连接是否连续。CLI 已生成 GCode；GUI 和实物打印尚未验证。当前仅提供诊断建议，没有改写原 3MF 或执行上传覆盖。

## 2026-10-13 完整线上日志及 GUI 拆分对照

用户补充附件：`/Users/liwei/.codex/attachments/22d65e61-d557-4559-b422-71cdea946c10/已粘贴的文本.txt`。该日志是一整行，已在证据目录整理为 `online-1559503884967653376.log`；297/327 行为实际空层告警，328 行返回 -100。机型/工艺兼容结果为 `compatible 1`，三模型三角面分别 22044、224912、31286，与下载输入一致，位姿也与前次复现吻合。新日志不是原任务 1559221689451700224 的尾部，记录为后续同问题复现。

GUI 日志为 `planning/debug/orca-user-gui-run-20261013-092744-567057.log`：

- 09:36:43 载入 `/Users/liwei/Downloads/03b2104a-1300-45a3-b3d8-29f2de28734f.obj`，日志 `before load_model_objects, count 1`。原始 OBJ 有盖子/把手/壶身三个 `o` 分组，但 GUI 加载成一个模型对象，合计278242面。
- 10:06:05 导出 GCode。读取并保存为证据目录 `gui-latest.gcode`，其 SHA256 为 `954443e620fa245397543c79e04cd028c7c2aded56cbfdd38b3f565e3dca4658`。实际配置确认 `min_length_factor=2`、`wall_generator=arachne`、`raft_mode=cura_v1`、`enable_support=0`；用户说的2确实生效，不能错误解释成未应用参数。
- 10:09:45.163828（日志6424行）记录 `Split to Objects`；10:09:56 再切片并完成本地GCode导出。用户确认拆分后出现问题。该 GUI stdout 为 info 级，未打印完整空层告警正文，不把无该字符串当作无告警。
- 最新 GUI 使用 `无支撑 [standard]`；云端失败方案是 `树状支撑`，还有少量支撑设置不同。日志中的云端配置响应可以直接提取方案1546931192517427200，其 `defaultProcessConf` 确认 `min_length_factor=2`。之前关闭支撑仍失败的单因素对照说明该支撑开关差异不足以解释成功/失败。

从 GUI 恢复目录只读复制拆分前后 `.3mf`。其 `3D/Objects/*.model` 是各自的 ZIP，不是原始XML；解开内层 ZIP 后在临时目录补齐自包含工程，不改变几何/配置，也不写回用户恢复目录。第一次误将内层ZIP当XML打包得到250/-6，纠正打包后才得到以下有效对照：

| 输入 | 同一本地 CLI、显式第1盘结果 |
| --- | --- |
| `gui-selfcontained.3mf`，拆分前1对象、系数2 | 0，生成GCode |
| `gui-split-selfcontained.3mf`，用户拆分后3对象、系数2 | 156，盖子7.27～8.67 mm空层 |

命令和日志分别为 `gui-cli-command.json`/`gui-cli.log`、`gui-split-cli-command.json`/`gui-split-cli.log`。此对照排除了“只因本地/线上版本不同才产生差异”的解释；线上构建身份仍未完全获取，不能声称所有路径逐字节相同。

### 拆分前已经存在缺失路径

根据原 OBJ 盖子分组、GUI 工程变换矩阵计算盖子的世界 XY 包围盒（与其余部件分离），统计拆分前 GCode 中端点均在此包围盒内、E正增量且XY移动的挤出段。原始统计为 `gui-lid-layer-counts.json`，坐标依据为 `gui-component-bounds.json`：

| Z/mm | 全对象XY挤出段数 | 盖子区域XY挤出段数 |
| --- | --- | --- |
| 7.27 | 458 | 9 |
| 7.47 | 448 | 0 |
| 7.67 | 428 | 0 |
| 7.87 | 428 | 0 |
| 8.07 | 432 | 0 |
| 8.27 | 437 | 0 |
| 8.47 | 456 | 0 |
| 8.67 | 469 | 8 |

因此拆分前“导出成功”不代表盖子路径连续；缺口真实存在，拆分使其可被现有检查发现。

### 两层原因

1. 路径生成：Arachne系数2过滤掉盖子的短中心墙；调为0.5的单因素测试恢复连续路径、通过切片。
2. 检查粒度：`GCode::collect_layers_to_print(const PrintObject&)`（GCode.cpp约1203行）按对象遍历，`Layer::has_extrusions()`（Layer.hpp约204行）任一region有挤出即返回true。同一对象中壶身的连续路径掩盖盖子的局部断层；拆成独立PrintObject后盖子单独被检查。

此外，GUI `Plater.cpp::on_slicing_update` 将 `SlicingEmptyGcodeLayers` 显示为严重警告，可继续生成预览/导出；CLI默认将该告警作为 -100 失败。应分别说明“生成GCode”和“通过可打印性检查”，不能用GUI可导出来判定无缺陷。

后续若用户要求改代码，检查增强方向应为识别同一对象内独立部件的路径断层，并统一相关失败语义；需评估真实桥接/支撑/装配件，不能简单把所有OBJ强拆或让CLI跳过空层检查。本轮没有实施该增强。


## 2026-10-13 批量失败任务导出复核

用户补充 `/Users/liwei/Desktop/slice_job.json`，仅包含2026-10-01至2026-10-13的270条失败任务（Orca264，Cura6），没有成功任务分母，不能据此计算整体失败率。分类明细保存在 `/Users/liwei/Desktop/slice_job_failure_analysis_20261013.json`，包含源文件哈希、逐任务证据等级、关联水壶任务及PNG验证结果；未修改业务源码或数据库。

| exit_code | 数量 | 归类及证据边界 |
| --- | ---: | --- |
| 156 | 204 | 切片计算失败/严重告警：13条无可打印层、6条首层为空（其中2条已启用支撑）、185条仅剩截断启动日志。不能统一归因min_length_factor。 |
| 134 | 21 | 全部正文为libpng IDAT incorrect data check；本次读取21个对应thumbnailImage，全部IDAT zlib解压同样报incorrect data check，PNG chunk CRC均通过。图片数据有问题，尚未定位产生错误图片的上游实现。 |
| 155 | 17 | 当前源码CLI_GCODE_PATH_CONFLICTS=-101，进程状态155；全部为多模型且useModelPosition=true。日志截断，不能确定具体冲突对象/支撑/高度。 |
| -1 | 12 | MTL预处理失败。3条传mtlUrl但未传mtlName，触发当前stageObjMaterial参数校验；其余9条仅有通用提示，不能确定下载/复制/重写/解析哪步失败。 |
| 124 | 7 | 进程超时：Orca2条明确180秒，Cura5条。 |
| 192 | 4 | 正文明确模型太接近exclusion area；当前引擎-64。 |
| 137 | 2 | 常见SIGKILL退出状态，需运行环境证据；不能仅凭此认定OOM。 |
| 139 | 2 | 常见SIGSEGV退出状态，Orca/Cura各1；具体崩溃点未知。 |
| 206 | 1 | 当前源码CLI_NO_SUITABLE_OBJECTS=-50，无满足打印区域要求的对象；启动日志截断，无法定位具体摆放/尺寸原因。 |

206条error_msg恰为2000字符，均从Initializing StaticPrintConfigs开始、以省略号结束。切片服务当前OrcaSliceServiceImpl.buildExecErrorMessage优先取stderr，使用StringUtils.abbreviate(...,2000)保留头部，与导出数据一致。应保存引擎结构化错误、关键尾部和完整日志位置，不能把缺失尾部误认为导出文件丢失。

同一组三个水壶OBJ URL共出现29条失败：模板1546928000419676160下23条，1546931192517427200下6条，均156。此前已确认的两个案例在其中；其余仅是相同模型关联任务，不能当成已复现的29个相同根因。模板1546931192517427200共33条失败（156为32，155为1），模板1546928000419676160共89条失败（156为86，MTL为3），也不能按模板一概归因。

PNG独立校验原始结果另存 `/tmp/orca-obj-1559221689451700224/slice-job-png-validation.json`。本次仅校验压缩数据，未修改图片或发布重切任务。

## 2026-10-15 单 STL 中间空层日志复核

- 输入：用户附件 `fead8b51-ebc0-48d6-a006-1a74ca8f2a90/已粘贴的文本.txt`，线上任务 `1560369123123318784`，引擎自报 2.3.2.0-Release。
- 机型 EP3Plus，机器预设 MyMarlin 0.4 nozzle，工艺名称“无支撑”，兼容判断 compatible=1；比例 1/1/1，旋转 0/0/0，已执行落平台。10532 三角面、open_edges=0、修复状态 not_needed；这些不能证明局部厚度和内部网格完全正常。
- 直接失败：`Object can't be printed for empty layer between 7.07 and 7.87`，对象 `d5aee1ade9.stl`。`has floating regions` 被 CLI 明确标为 NON_CRITICAL，空层被标为严重告警，no_check=0 后 return -100，服务观察 exitCode=156。
- 当前 `GCode.cpp::collect_layers_to_print` 按对象的模型/支撑挤出层判断高度间隔，`src/OrcaSlicer.cpp` 对 SlicingEmptyGcodeLayers 拒绝输出。日志与此出口相符，但没有对当前工作区二进制做复现，也不能用本地新改动推断线上部署身份。
- 日志没有模型 CDN URL、模板 ID、wall_generator、min_length_factor、min_feature_size 或完整有效工艺。只能确认中间空层现象，待区分原始悬空/断开结构、薄特征路径过滤和网格问题。
- `Make overhang printable` 和 angle 日志在区域开关判断之前打印，不能凭这两行认定启用了“悬垂可打印化”。`calc_exclude_triangles` 的 error 后流程继续，亦不是此次最终退出原因。
- 当时待复核：取得原模型及实际工艺，检查预览 Z=7.07～7.87 mm 的截面和路径，并核实墙生成器后选择参数对照；不能单凭短墙系数为 2 推断根因。后续已完成下述复现。未修改业务源码、模板或数据库。

### 2026-10-15 原 STL 与经典墙单因素复现

- 用户原 STL：`https://cdn-h.gongfudou.com/print3d/prod/permanently/2025-8-8/61171015-fa94-4d3f-82c6-baea1571ccc1.stl`。下载为 `/tmp/orca-stl-1560369123123318784/input.stl`，SHA256 `0b8a0cef586b06f4fab48d81a07b69d68b445c6ee56ad9f23185f5e7bcbf1e11`，10532 面，与日志相符。尺寸约 8.224×13.608×6.390 mm；网格闭合且朝向一致，6 个拓扑连通分量，但不能将拓扑分离一概认定为物理悬空，部分分量可能接触/相交。
- 用户实际工艺附件 `131ae449-91a7-4208-98bb-247a43c90547/已粘贴的文本.txt`，保存为 `provided-process.json`。3MF 来自 `https://cdn-h.gongfudou.com/print3d/prod/permanently/2026-10-13/8e81a2d1-7fce-4af8-a355-672c5170a2fe.3mf`，含7个其他示例网格；本次只提取机器/耗材，不将其中示例模型当失败输入。
- 工艺关键值：`wall_generator=classic`、`detect_thin_wall=0`、`min_length_factor=2`、喷嘴0.4、外墙0.42、内墙0.45、层高0.2、支撑关闭、`raft_mode=cura_v1`。独立 CLI 机器/耗材从 `from=project` 标准化为 `system`，补齐 type/instantiation；机器名用线上日志和工艺兼容列表中的 `MyMarlin 0.4 nozzle`。未取得线上最终机器/耗材完整JSON，不能声称所有有效参数逐字节相同。
- 当前 Release 链接产物 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA256 `0e6c16a7237a9cc0e524f4752c41d008f2c67b17588c2b2faac834350c207627`，自报2.3.2.2-Release。源码/对象文件/二进制哈希记录在 `build-identity.json`；本构建目录为对应仓库 arm64 Ninja Multi-Config，`ninja -C build/arm64 -f build-Release.ninja -n OrcaSlicer` 只剩两个编码检查utility，没有待编译/链接动作，资源指向当前仓库。旧打包路径哈希不同，未用其证明当前源码。本轮未编译或改业务源码。
- 所有对照使用同一二进制、STL、机器/耗材、缩放1、旋转0、落平台、位置50/50/0、不重排和显式第1盘。命令在 `*-command.json`，日志在 `*.log`，结果汇总 `comparisons.json`；运行目录隔离，不覆盖仓库 result.json。三组工艺差异已核对为表中单一字段。

| 配置对照 | 结果 |
| --- | --- |
| 原配置：经典墙，检查薄壁关闭，短墙系数2 | 156；完全复现7.07～7.87 mm空层，没有GCode |
| 仅 `detect_thin_wall: 0 → 1` | 0；生成 `thinwall/plate_1.gcode`，空层严重告警消失，悬空区域非致命告警仍在 |
| 仅 `min_length_factor: 2 → 0.5` | 156；相同7.07～7.87 mm空层，没有GCode |

`thinwall-layer-counts.json` 根据有效挤出移动统计：打印Z=7.27/7.47/7.67 mm分别恢复113/73/32段外墙挤出，实际注释线宽范围约0.239～0.347、0.233～0.269、0.334 mm。不能只凭exit=0认定实机可靠；剩余悬空区域提示保留。

独立几何截面测量见 `mesh-cross-section-measurements.json`：原模型Z=5.9 mm只剩两个薄环，截面最大内切圆直径约0.191/0.228 mm（对近似均匀环壁估算厚度，不是全模型最小壁厚）。`thin-section.png` 展示模型与截面。模型坐标Z和含垫层的打印Z不同，不能直接把日志7.07～7.87当STL本地高度。

源码对应：`LayerRegion.cpp::make_perimeters` 选经典墙或Arachne；`PerimeterGenerator.cpp::process_classic` 在 `detect_thin_wall` 开启时为窄区生成中轴单线，关闭时走闭环偏移路径。本例薄壁在关闭时缺失，开启后恢复；不能把“所有小于外墙线宽的结构必定删除”当通用阈值。Arachne短墙系数不应从旧案例直接套用，本次单独降低它已实测无效。

GUI 交接（未执行）：加载该原STL，保持实际尺寸、原机器/工艺与位置，墙生成器维持经典；在“强度 → 墙 → 检查薄壁”开启后重切。逐层查看打印Z=7.07～7.87 mm是否连续，再检查剩余悬空区域提示对应位置。若结构本来需要承托，再按用途选择朝向、支撑或加厚模型，不以关闭严重告警作为解决方案。未上传覆盖、修改数据库或执行打印。


## 2026-10-15 最新27条失败数据总结

来源 `/Users/liwei/Desktop/无标题.json`，SHA256 `538048c4a5efa184e3a47c8978d0dee055334d15cbc79af2f58910d9ae37a7b9`；27条均为Orca，EP3Plus 20条、EP7 7条，8个主模板、10组模型URL集合。完整逐任务分析在桌面 `切片失败分析_27条_20261015.txt` / 同名JSON；本节不重复27条SN。

- `156` 18条：3条明确No layers、1条此前完整日志确定中间空层、其余14条只有截断启动日志。14条中2条与已定位薄壁模型/模板相同，2条同一AI文字STL，6条同一0.9倍STL，3条同一EP7模型（两模板），1条5个OBJ部件；不将这些都归因短墙过滤。
- `155` 3条：当前代码 `CLI_GCODE_PATH_CONFLICTS=-101`；同一组2个OBJ，摆放位置略有变化。具体冲突路径/支撑/高度未保留。
- `192` 2条：明确too close to exclusion area；同一模型约2.58/2.64倍，需核机器禁入区与变换后几何。
- `206` 2条：当前代码 `CLI_NO_SUITABLE_OBJECTS=-50`，同一EP7模型1.75/1.76倍；缺尾部，不能指定XY、Z还是盘归属。
- `-1` 2条：MTL预处理。实际均有mtlUrl和mtlName=杯子握把.mtl，不是漏传材质名。

### 缩放实测

临时目录 `/tmp/slice-failures-27-20261015`，读取原模型数值坐标，未重切这三个历史任务。两条STL任务使用3792面模型，原尺寸22.800×54.998×7.5 mm，modelScale=0.01后仅0.228×0.550×0.075 mm；另一个OBJ原尺寸58.784×61.8862×14.3565 mm，0.01后仅0.588×0.619×0.144 mm。两者旋转均为0，日志均明确无层。尺寸证据与缩放过小相符，未用历史最终工艺做全链路复现。

### 材质编码独立复现

原OBJ URL尾部 `c0c94f5b-edb1-4f0f-9f60-ee8d8e2fcf6f.obj`、MTL尾部 `6d8499c2-08d0-441f-b0ba-304620df9c34.mtl` 均当前可下载。OBJ严格UTF8在偏移73失败，GB18030可解出 `mtllib 杯子握把.mtl` 和 `usemtl set1`；MTL有效并定义set1。当前切片服务 `develop/7984d46851a373161b2ec947da1d08e2033cf4b2` 的 `rewriteObjMaterialReference` 使用 `Files.newBufferedReader(..., UTF_8)`。

使用Java17同种读取API：原文件抛 `java.nio.charset.MalformedInputException: Input length = 1`（退出1）；仅将临时副本转UTF8后读取4455行成功（退出0）。证据 `material-encoding-comparison.json`、`material-java-repro.log`。这是原文件编码与当前服务读取的不兼容证据，线上历史异常堆栈未在数据库中保存，未宣称已运行整个Spring服务。后续修复需覆盖引用改写和后续材质统计的读取，不能仅改变本地MTL文件名。

### 数据边界与后续

20条error_msg长度精确为2000且尾部省略号，与服务 `buildExecErrorMessage` 保留stderr/stdout头部的 `StringUtils.abbreviate(..., 2000)` 一致。问题是持久化截断，不是用户导出少字段。27条params里的orcaSliceParam均标记type=filament，是耗材覆盖，不能当完整工艺快照判断墙生成器。

优先：核对比例/百分比换算及最终尺寸，修复OBJ编码兼容，保存结构化error_string及完整日志/尾部，再对路径冲突与机器区域分类处理。此次未改业务代码、线上配置/模型或发起打印，没有新的GUI/实机验收结论。

## 2026-10-15 其他失败原因细分与候选工艺对照

用户要求继续分析MTL、禁区、无打印对象及其他原因。本轮只做本地诊断，不修改业务源码、线上模板、数据库或原模型。桌面 `切片失败分析_27条_20261015.txt/json` 已按27个SN更新，`切片失败关联模板_只读导出_20261015.sql` 为8个模板的两条独立SELECT（主表/档位表），不做字符串关联以避免此前collation冲突。

### 输入和验证范围

- 所有模型按原任务URL下载；映射/字节数在 `/tmp/slice-failures-27-20261015/more-model-downloads.json`。位姿使用后端实际输出的旋转、缩放、位置和落平台语义；`flipFlag` 对应后端命令分支已注释，不额外擅自镜像。
- 几何结果 `geometry-cases.json`，旋转顺序Rz*Ry*Rx。模型XY凸包重叠不直接视为实体或路径碰撞；额外做了同高度截面抽样 `collision-sections.json`。
- 实验全部使用同一arm64 Release二进制，SHA256仍为 `0e6c16a7237a9cc0e524f4752c41d008f2c67b17588c2b2faac834350c207627`，自报2.3.2.2；线上2.3.2.0与部署提交未知。分析期间其他工作将仓库HEAD从c893a4e推进到d751db42，本轮未提交/修改业务代码，也不将该二进制说成覆盖了并行会话的后续源码。
- 10月13日保存的模板1546931192517427200工艺用于该模板关联模型；其他模板尚未取得数据库JSON，使用 `conf/prod/biz.yml` 通用EP7/EP3Plus参数作候选。按当前服务源码合并请求otherParams、层高、填充和raft参数，包含请求保存的耗材覆盖。不得将这些实验说成历史全部有效参数完全一致。
- 每组命令、输入JSON、完整日志、result.json及analysis.json均在临时目录 `cli/<case>/`。成功对照均检查实际生成非空GCode，不只看退出码0；无跳过严重告警选项。

### MTL两条：编码不兼容

仍以此前Java对照为证：原OBJ包含GB18030可解出的 `mtllib 杯子握把.mtl`，严格UTF8读取失败；MTL有效且定义usemtl所用的set1。当前 `stageObjMaterial → rewriteObjMaterialReference` 明确按UTF8读；后续 `MtlParser` 默认读取也是UTF8。修复应统一或兼容编码，并覆盖引用改写和材质统计；仅重试下载、改MTL文件名不足。历史数据库只保留通用提示，未取得当时异常堆栈。

### 禁区两条：模板17的具体禁区缺失

- SN1559983264815599616：变换后X4.253～96.207、Y2.274～96.830、Z0～6.776mm。
- SN1559983218524999680：X3.269～97.201、Y1.248～97.837、Z0～6.922mm。
- 均在普通101×101床面内；原始日志明确模型too close to exclusion area。当前 `Print.cpp::layered_print_cleareance_valid` 检查 `bed_exclude_area` 与对象二维凸包相交。该报错不是由生成后的brim/raft边缘来判定；凸包还会覆盖模型凹处/孔洞。
- 缺模板17及standard档 `default_machine_conf`，因此无法指出具体哪块禁区，也无法确定是合理禁区被触碰还是机型模板配置错。下一步核对bed_exclude_area、printable_area后再决定移动/缩小或纠正模板。

### 无打印对象两条：不能直接归为超高

EP7同一520860面STL在1.75/1.76倍、旋转(-48,0,45)后高178.016/179.034mm，XY在通用180×180床面内。模板58的历史206日志没有尾部。

- 通用EP7高度180、原位姿能进入切片，得到156底部空层，而非206。
- 仅将候选机器高度180改为170就得到206；这是机制对照，170不是已知模板58原值。
- 同一1.76倍模型用另一模板1527683229960347648历史返回156，也提示应核对有效机器/工艺差异，不能说文件没有对象。
- **高度附加问题**：开启支撑后生成的GCode中，1.75倍最高打印层Z=179.17，1.76倍Z=180.37；后者超过通用180mm限高。源模型179.034mm能放进床面不代表加垫层后的最终路径仍可打印。该问题不能直接当成历史206的已证实触发点；实际模板58的机器范围和盘归属仍待导出。测量在 `ep7-generated-height.json`。

### 路径冲突三条：支撑相关

原“支架+支架旋钮”三组摆放，分别用候选树支撑工艺均返回155，日志明确case-12-2.obj与case-12-1.obj冲突。首组在同一工艺下只关enable_support后返回0；只去掉raft而保留树支撑仍155。因此候选实验定位为支撑参与的路径冲突，不能归为材质/空层，也不能仅凭二维凸包交集说实体互相穿插。同高度0.1～52.1mm、每1mm截面抽样未发现实体面积相交，但抽样不是全体三维碰撞证明。

缺模板1507433285833048064原工艺和完整历史冲突层，不确定当时具体是支撑与实体还是支撑与支撑。处理方向为按实际支撑路径留间距、旋转/重新排布或分盘；关闭支撑只是定位实验，不能作为必须支撑模型的通用修复。

### 其余156的具体细分

| 关联任务数/样本 | 候选基线 | 对照结果 | 含义 |
| --- | --- | --- | --- |
| 6条同一0.9倍STL | Arachne、短墙系数2，打印Z65.07～65.67空层 | 仅系数改0.5，生成GCode | 高处短墙过滤；不是整体尺寸太小 |
| 1条5个OBJ | Arachne、系数2，第4部件polySurface23在Z4.07～4.67空层 | 仅系数改0.5，生成GCode | 同类短墙过滤；导入时缺MTL提示不是最终原因 |
| 2条AI文字STL | 系数2在70.67～71.47空层 | 系数0.5仍失败；在0.5基础上最小墙线宽85%→50%通过，或者仅系数0.5→0通过；最小特征25%→0无效 | 更细短的顶部连接，0.5并非万能阈值；0/50%只是定位对照，需预览及可打印性验证 |
| 3条EP7的156 | 请求强制enable_support=0；两种朝向均在1.32～1.97空层 | 去raft直接报首层为空，首层有1个slice但perimeter/fill均0；只改airgap0.35→0.25仍失败；开启树支撑后生成GCode | 底部薄小截面无路径、缺承托；先处理朝向/底面/支撑，1.76倍另须处理总高度 |
| 2条已知经典墙模型的历史重试 | 模型/模板/位姿与已完整复现案例一致 | 沿用此前“开启detect_thin_wall恢复、降低短墙系数无效”的模型证据 | 尚无这两条历史工艺快照，保留待核标识 |

此轮没有GUI/实机验收，没有把候选实验直接批量应用到模板。后续关键是模板17、58的完整机器参数，以及1507433285833048064、1546931192517427200等档位工艺；若模板后来修改过，当前值仍不能替代任务执行时快照。日志持久化仍应保留关键尾部和结构化失败，不只保存前2000字符启动信息。

### 2026-10-15 用户追问192/206：再核机制与历史GCode

用户确认只追问第2项禁区与第3项无打印对象。源码仍以PDS develop/7984d468及本地Orca master/d751db42为准；工作区其他改动未动，试切二进制哈希仍为0e6c16a7…。

- 读取用户较早导出的 `/Users/liwei/Desktop/slice_job.txt`（111000条，数据截至2026-07-04），其中模板17成功3229条、失败202条，未找到模板58。仅为寻找历史机器参数，不混入此次27条统计。取末尾两个成功SN1522965125208276992、1522965470084423680下载其GCode，两者都记录 `bed_exclude_area=0x0`、床面101×101、限高105、机型EP3Plus、机器预设MyMarlin0.4nozzle。元数据/哈希见 `template17-historical-configs.json`。7月无禁区不等于10月未修改。
- 两条192原模型/位姿使用通用EP3Plus机器/工艺均实际生成GCode，证据 `cli/exclusion-first-common/`、`cli/exclusion-second-common/`。未使用no-check。结合7月机器快照，下一步优先核实际加载的禁区、档位JSON、3MF覆盖和模板变更；不能认定原模型必然摆错。
- `PrintConfig.cpp`默认禁区仅点(0,0)，不是一个非零面积区域。`Print.cpp`对应192文案的分支是机器禁区多边形与模型二维凸包相交。床面内也可能碰禁区，且凹形/孔洞会被凸包填满；实际禁区坐标未提供时不能猜具体相交边。
- `ModelObject::update_instances_print_volume_state`只计完全Inside对象；单模型部分越界时count=0，`OrcaSlicer.cpp`先退出206，因此用户提示“无打印对象”不能解释为空STL，也不要求整个对象都在床外。另一个206出口是Print::apply后为空。
- 体积筛选发生在生成模型/支撑/垫层路径前。因此1.76倍+垫层最高打印Z180.37是独立总高度问题，不能倒推为该两条历史206的已确认触发。通用180mm机器下原几何可进入切片，真实模板58限高/区域/盘归属尚未知。
- 当前上传逻辑 `buildOrcaSliceConfig` 以3MF机器JSON覆盖公共JSON，`applyProfile/copyProfileToConfig` 对非场景模板使用档位JSON，`resolveSliceConf` 指定ID未匹配时可能回退场景/系统默认；任务表saveSliceJob写入请求sliceConfId而不是最终配置快照。只看device_type或主表尺寸不能断言有效机器范围。没有证据声称本例一定发生回退。
- 新只读SQL `/Users/liwei/Desktop/禁区与打印范围排查_17_58.sql` 查询17、58及对照1527683229960347648的主模板、档位、操作日志。日志 `config_content` 保存配置对象，可能恢复更新前后机器参数；记录失败或直改DB可能无快照，不保证完整历史。三个结果集无JOIN/无字符列比较，兼容MySQL5.7。

桌面27条分析TXT/JSON已追加以上证据。下一步取10月13日19:53～19:55（206）与10月14日17:36（192）实际配置或最近操作快照；未修改线上配置或业务源码。

## 2026-10-15 线上查询确认禁区192与限高206

用户明确授权使用线上数据库只读查询。项目内部地址直连超时，改用已登录 Navicat `3d_online / print3d_slice`，只执行 SELECT。查询主表、standard 档与操作日志共12条快照，保存到桌面 `禁区与无打印对象_线上核查_20261015.json`；原导出 SHA256 为 `57534c61606447377f76c28a42b75570227919e2f6471a0b2fa283738ce41656`。

- 模板17：`通用配置-AI建模`，device_type=EP3Plus，机器printer_model=EP3Pro；bed_exclude_area=[0x90,5x90,5x100,0x100]，床面101×101，高105。日志385（2026-05-31）禁区为[0x0]、机器EP3Plus；日志543（2026-09-14 19:13:23）出现当前禁区和EP3Pro，且min_length_factor从0.5变2。主表和standard档均与9月日志一致。这是配置更新证据，不能据此给某次代码部署定责。
- 两条192的模型二维凸包与该禁区相交3.936/11.537mm²。线上机器/工艺+原请求位姿均复现192；仅本地清空禁区均exit0；保留禁区仅X向右2mm均exit0并有非空GCode。需先核实实机禁区，不能直接删除限制。
- 模板58：`通用配置-模型导入`，EP7，printable_height=178，床面180×180，禁区X168～180/Y0～32；主表/standard相同，updated_at=2026-07-16 09:29:50。按config_id=58未查到操作日志，不能声称拥有失败瞬间快照。
- 原模型高178.016/179.034mm，均超178；XY在床面内且不碰该禁区。线上机器/工艺复现两条206。只本地改限高180后均变156（底部1.32～1.97mm空层）；对照模板1527683229960347648真实限高180，也复现原来的156。这确认206的直接触发是裸模型超高，发生在垫层生成前。
- 保持模板58限高178，缩放1.72仍156；再enable_support=1后exit0，GCode max_z_height=176.57mm。请求otherParams.enable_support=0会覆盖模板原enable_support=1，说明高度问题解决后还有底部支撑问题。仅本地验证，未打印/未改线上。
- 本轮13个原参数/单因素/修复候选对照使用同一现有二进制，SHA256=`92f63fbe19ad0323e4b71fb46203d2d051d816cea6a33ea81fb0c233fd08762c`，不同于上轮。并行工作已推进源码HEAD，结束时master/b2d938c8c6c7a5e264239c765117b59c08f56b09；本任务没有构建、修改业务源码或覆盖其他改动。线上2.3.2.0具体提交仍未知。测试读取线上完整machine/process，按源码叠加原请求；耗材以公共值合并请求保存的filament，不冒充完整历史最终执行快照。
- 证据：`/tmp/slice-failures-27-20261015/live-db-reproduction.json`、`live-db-remedy-tests.json`、`live-db-geometry.json`及cli/live*目录。桌面27条TXT/JSON的4条分类已从待核更新为线上参数复现。其余任务结论不扩大。
