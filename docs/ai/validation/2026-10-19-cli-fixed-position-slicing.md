# CLI 固定位置切片回归修复

## Linux旧补丁归并（2026-10-19，完成）

用户要求将Linux工作目录仍有用的未提交改动归入master，并与本次修复合为一个提交；清理已被替代的旧补丁，后续由用户从master同步Linux分支。核对基准为master `23a8f07844`，Linux工作目录 `orca-slicer` 的 `linux/release2_3_2` / `4b9eb2cc50`。两目录暂存区均为空，master工作区干净。

- 逐项核对Linux目录的7个已跟踪修改和2个新增文件：CLI新增27行均已在master保留；版本和模块登记完整；旧脚本全部既有run调用及参数均保留，master增加了后续边界检查。没有发现需要额外搬运的业务实现或测试。
- 旧AppImage包内切片检查、无显示环境、身份记录及失败证据上传均已包含在master；master进一步增加分盘JSON/实际3MF回归。Linux分支此前已提交的缓存、手动构建、并发及打包配置属于分支基线，保持原样，不用master整文件覆盖。
- Linux早期19项切片回归属于当时源码的历史证据，不替代master的58项最终验证；旧记录及全部9个文件、二进制补丁和SHA256清单已备份至 `planning/cli-fixed-position-20261019/linux-patch-consolidation-200954/`，逐文件核对一致。备份仅留本地，不入库。
- 已恢复上述7个跟踪文件并移除对应2个新增文件；清理后Linux工作区干净、HEAD仍为 `4b9eb2cc50`，逐文件确认与该分支HEAD完全一致，清理证据见备份目录的 `cleanup.json`。master仅补齐本次归并记录及当前/历史版本说明，按用户授权合入原修复提交，保留单笔提交。
- 本轮业务源码、测试脚本、工作流和版本均未修改，不重跑既有应用回归；GUI/Linux实机验收仍待用户打包执行。用户后续合入master时保留Linux分支已有打包配置，并确认包含两套包内回归步骤；本轮未合并Linux分支、推送或触发构建。

## 提交交接（2026-10-19）

用户已授权在master提交本次完整修复、回归脚本、Linux发布检查及配套文档，由用户随后打包测试。版本保留 `2.3.2.3-Release`，不推送、不部署；下文“未提交”等描述属于各历史轮次。

- 提交前再次核对业务源码和测试脚本身份，与最终验证记录一致；沿用已完成的58项切片、80次分盘/变换CLI运行和16个格式测试共590条断言，不重复构建或计数。
- Linux/Windows原生验证、GUI手测及服务器真实业务测试仍待执行，具体操作见下文交接表。提交通过不替代上线验收。

## 提交前复审（2026-10-19，完成，可提交）

用户确认实际业务分别使用per-model或全局旋转/缩放，不混用这两种入口。本轮按此范围完成源码复审并补充独立入口回归；复杂混用不作为本次业务验收条件，也未继续扩大实现。基准仍为master `ba979559b0`及现有未提交补丁。

- 逐文件核对业务源码、版本、格式测试和应用身份，均与上一轮最终验证一致；应用SHA256仍为 `6ba1f8aedc2efbe60ef2861b675b97f73224bfe52ef0277f5c15cb3bbd86a564`。本轮只补Python回归和文档，无需重建应用。
- 新增独立per-model旋转、缩放、旋转+统一缩放，分别覆盖单盘和多盘，共12次开关JSON对照；逐点核对JSON还原与真实3MF，包含切片产物。扩展后的分盘/变换回归 **80/80次通过**，证据 `planning/cli-fixed-position-20261019/commit-review-arrangement/`，脚本及程序身份见其 `identity.json`。上一轮58项切片和16测试590条格式断言证据继续有效，不记为本轮重复运行。
- 重新审核盘成员刷新与选盘、原工程/新增输入边界、禁用对象、全局与per-model独立入口、复制ID传递、3MF引用拒绝和错误返回；未确认新的提交阻断项。复核Linux工作流在发布前运行真实包内CLI，测试失败不进入后续常规上传/Release；包内路径与AppImage构建模板一致，该工作流尚未在Linux实际运行。
- 最终Python语法、Git空白与35模块文档/指纹检查通过。建议将本次完整修复、测试、模块登记及文档一起提交，不遗漏两个未跟踪新文件。保留既定版本号 `2.3.2.3-Release`。本轮未暂存、提交或推送。
- **代码可以提交并进入部署测试；上线验收仍待Linux真实任务及GUI手测。** 单盘不套用多盘虚拟盘居中规则；原业务单独per-model及单独全局变换在已测场景正常。GUI/服务器具体步骤见下文交接表，不能用提交结论代替实机验收。

## 当前修复与复审（2026-10-19，已完成）

本轮按用户授权在 `orca-slicer-master` 的 `master` 工作目录完成修改、构建、回归与差异复审；HEAD仍为 `ba979559b06c8da78ffbbb76597c68a9c51d28cc`，下面修复均为未提交修改，未推送或部署。后文“尚未修复”“进行中”属于对应历史轮次，以本节最终结论为准。

### 最终改动及审核结论

1. 混合工程与新增STL/OBJ按实例来源区分：工程原有无归属备用模型沿用盘选择语义，新增启用网格不能继承豁免。单/多盘及显式盘号下，完全越界或部分越界均在生成G-code前拒绝，盘内三个对象均产生挤出路径。
2. 全局旋转/缩放JSON按对象的实际volume差值与instance/orient合成；拆色和repetitions复制后保留变换记录。比较开关JSON时的物理3MF完全一致，再以JSON重建源顶点核对实际几何，避免只返回看似正确的数值。不同对象的原始volume姿态可以不同；单个多部件对象若无法用一个整体变换表示，或合成含剪切/奇异/非有限值，明确返回-2而不输出误导JSON。此时关闭JSON仍可保存实际3MF；未新增核心旋转/缩放算法。
3. GUI/CLI共用3MF导出器拒绝负数/越界对象或实例、缺失盘数据和缺失导出映射，实际写出再次核对。失败保留已有目标、清理临时文件；普通未切片工程及没有可编辑模型的纯G-code盘仍支持。
4. 复审本轮完整业务差异及失败路径，检查盘成员刷新、真实床形、禁用/跳过对象、指定盘、多盘、合并和保存重开；当前覆盖范围内未再确认未修的阻断问题。未修改重建、网格修复或切层/填充算法，不能将本轮结论泛化为所有复杂模型、GUI交互或服务器运行库均已验证。

### 最终验证身份与结果

- macOS arm64 Release，MeshLib OFF；应用 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，版本 `2.3.2.3-Release`。
- 应用SHA256：`6ba1f8aedc2efbe60ef2861b675b97f73224bfe52ef0277f5c15cb3bbd86a564`。最小构建完成，最后日志 `planning/cli-fixed-position-20261019/repair-exit-build.log`；共享格式测试目标也已构建，具体源码、测试和程序身份见 `repair-verified-identity.json`。
- 固定位置/切片脚本：**58/58通过**，`repair-verified-slicing/validation.json`。核对退出码、真实挤出路径、所有预期对象及3MF包内容。
- 分盘/变换脚本：**68次CLI运行全部通过**，`repair-verified-arrangement/{results,validation,identity}.json`。包括33组开关JSON对照及2次不可表示变换正反验证；覆盖原有单/多盘规则、三角形床、37个越界返回盘和新增组合变换。
- `[3mf][bbs]`：**16个Catch测试、590条断言通过**，`repair-format-all.log`；其中 `[PlateReferences],[SlicedExport]` 7测试319断言已包含，不重复累加。最后改动仅涉及CLI变换元数据和PartPlate返回复制ID，格式实现及测试身份未改变。
- Python AST、Git空白检查及35个模块文档/指纹检查通过。CodeGraph MCP未暴露，本目录无索引，已按模块文档和源码核对；没有初始化索引或将历史图谱当作当前事实。
- Linux/Windows原生构建、真实业务模型、GUI交互和实际打印尚未执行。Linux发布工作流已接入同一批脚本，不能把本机通过当作远端发布通过。

### 本轮GUI与服务器交接（待执行）

应用：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/build/arm64/src/Release/OrcaSlicer.app`。夹具目录：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/planning/cli-fixed-position-20261019/repair-verified-slicing/`。

| 场景 | 操作与预期 | 状态 |
| --- | --- | --- |
| 普通工程保存重开 | 打开夹具目录中 `mixed-project-seed/output/model.3mf`，加载工程参数（100×100×100 mm、0.4 mm喷嘴、PLA、0.2 mm层高、无支撑/筏层/擦料塔）。两个8×6×2 mm模型底面中心为(20,30,0)、(70,60,0)。不摆盘，另存新工程、关闭并重开；两个对象位置与打印开关保持，切片预览均有路径。 | CLI及格式层已验证；GUI待手测 |
| 禁用模型部分越界 | 打开 `disabled-partly-outside.3mf`，保留工程参数直接切第一盘；只有正常box.stl打印。启用second.stl打印后应拒绝越界，关闭后恢复；另存并重开仍保持开关和位置。 | CLI已通过；GUI待手测 |
| 指定盘导出 | 打开 `first-disabled.3mf`，只切第二盘并导出切片包；包中应有第二盘有效G-code，不因第一盘禁用而失败。 | CLI/共享格式已通过；GUI待手测 |
| 服务器真实任务 | 同步本轮代码构建Linux AppImage，确认包内两套回归通过；在测试容器分别跑后端实际的STL/OBJ固定位置、多盘分盘、缩略图任务，核对return_code、每个预期对象和真实G-code。保留命令、日志、结果及新包SHA256。 | 待Linux及业务测试 |

错误引用/缺导出映射使用格式单测构造，已验证拒绝和原文件保护；不要求在GUI故意破坏用户工程。CLI独有的JSON协议无需GUI操作。回传GUI另存工程/切片包、操作时间段及应用日志，或服务器任务的命令与产物即可继续核查。

### 本轮过程记录（保留中间失败与复测）

### 修复进展

已补按实例来源识别工程输入（合并模型时传递ID），新增STL/OBJ不再继承工程盘外备用对象豁免；全局变换JSON通过实际volume变换差值与实例/orient合成，不改变物理几何；共享3MF导出器拒绝无效对象/实例及缺失导出映射，并保留原文件与清理临时包。已按改动行格式化。

最小构建完成：Mac arm64 Release，`OrcaSlicer`与`libslic3r_tests`，MeshLib关闭。应用SHA256 `d49360bca3982b25c7f0254fbc5b4e337552254c459ab1d2d16ad80379d34a0d`。首轮58项切片、56次分盘/JSON开关对照全通过，`[PlateReferences],[SlicedExport]`为7个Catch测试、319条断言全通过。证据在`planning/cli-fixed-position-20261019/repair-*`，构建/测试均未触碰用户资料。正在复审组合参数、完整格式回归和文档；暂不作为最终交付结论。

### 组合参数复审进展

全局差值已改为逐对象保存，避免不同原始volume姿态被错误要求使用同一批次矩阵；PartPlate复制返回原/新对象ID，CLI传递全局变换及orient记录，拆色也传递全局记录，不改变实际复制或摆放。第二轮构建通过，58项切片继续通过。扩展分盘脚本在非均匀缩放+旋转（剪切）拒绝用例抓住本次补丁的退出范围错误：`flush_and_exit`宏实际是return，放进lambda只返回lambda。已移到CLI::run调用处；不放宽该失败断言，源码再次重建后复测。前一轮分盘运行因此不能算全部通过。另两次早期手工剪切探测因夹具XML命名空间错误返回-6，仅保留调试证据，不计有效用例。

## 再次审查：导入组合与导出边界（2026-10-19，完成，新增两项未修问题）

用户要求继续检查其他问题。本轮基准仍为 master `ba979559b0` 与现有未提交补丁；先核对源码/二进制身份，检查混合原始模型与工程、多实例和变换后的归属、分盘 JSON 与真实产物、3MF 导出失败行为。已有 3MF 异常引用保护缺口单列为已知未修，不重复计为新发现。仅对新增疑点进行隔离验证，保留原回归证据；不部署、不提交。

### 新确认的问题

1. **混合工程与新增网格仍可能漏切并返回成功。** 输入已有两个正常对象的3MF，再输入一个本次新增的STL或OBJ，后者中心固定到(200,200)，使用100×100床、`--arrange 0 --slice 0`。进程及报告均返回0，但G-code只含工程原有两个对象。新增对象在导出的3MF中仍存在且printable=1，没有盘归属。单盘和多盘均复现；多盘JSON按规则为它返回虚拟盘，这不能代替实际切片。根因是 `OrcaSlicer.cpp` 切片预检按全局 `!plate_data_src.empty()` 放过无归属实例，没有区分工程原有备用对象和本次新增输入。它属于当前防漏切修复尚未覆盖的混合输入边界；本轮未通过历史二分认定首次引入提交。对照：同样的裸STL独立输入被正确拒绝(-50)，混合输入位于盘内时三个对象都产生G-code，部分越界被拒绝(-52)，开启摆盘后正常生成三个对象。
2. **分盘变换JSON遗漏全局缩放/旋转。** 对8×6×2mm模型使用 `--scale 2`，实际3MF为16×12×4mm，但返回scale仍为[1,1,1]；使用 `--rotate 90`，实际3MF为6×8×2mm，返回rotation仍为[0,0,0]。仅导出和同时切片均复现。全局操作修改ModelVolume，而JSON只组合实例与orient变换，没有纳入全局体积变换；后端若按原始模型及JSON重建位置/尺寸，将与实际切片几何不一致。该字段计算是既有定制JSON逻辑，非当前盘归属补丁新增。用户先前使用的 `--model-scale 2` / `--model-rotate 0,0,90` 对照正常：JSON分别为[2,2,2]/[0,0,90]，实际3MF匹配。本轮未扩大为所有复杂多部件工程均能用单组TRS表达的结论。

### 本轮证据与限制

- 确认源码、两个原回归脚本及构建身份与 `review-final-identity.json` 完全一致，复用同一Mac arm64 Release、MeshLib OFF程序，SHA256 `3c2db788536353c6789613a6055ef8069ea71d498f5550ba7ef5973f958a5c5e`；没有修改实现，无需重新构建。
- 共43次CLI调用，排除4次未暴露的 `--copy`/`--split` 参数探测和1次没有切片动作的repetitions参数拒绝，留下38次业务调查/对照。不能将本次计数记成“38项全部通过”：其中明确复现上述缺陷。
- 原始证据在 `planning/cli-fixed-position-20261019/review-extra{,-followup,-geometry}/`，包括逐次命令、日志、result.json、G-code、3MF及解析后的实际几何。`review-extra/findings.json`汇总计数与身份；本轮对关键失败及正常对照另做断言核对。
- 正常混合工程导出、工程位置调整/跨盘移动、切片时repetitions、以中心为原点或带XY偏移的矩形床，在此次案例中未复现新错误；未将不可调用的源码分支视为可触发缺陷。装配运行仅作为行为观察，没有用它证明任意输入的几何等价。
- 已知的共享3MF无效引用保护缺口仍未修。当前未提交补丁还不能视为所有同类入口都已修齐。本轮只修改审查记录与ignored隔离探测脚本，没有修改业务实现、提交或部署；Linux、Windows和GUI手测未执行。

## 补充复核：3MF 导出器保护缺口（2026-10-19，尚未修复）

用户提供另一份审查，要求核对剩余缺口并解释异形打印床问题。当前仍为 master `ba979559b0` 加下文未提交补丁；本次只核对源码和已有证据，不修改实现、不重新构建或运行回归。

确认 `bbs_3mf.cpp` 的模型配置导出在对象或实例索引失效时只记录警告，随后仍执行 `objects_data.find(obj)->second.object_id`。对象不存在或未进入导出映射时，解引用无效查询结果属于未定义行为，可能损坏输出或崩溃；实例索引无效时也可能继续写入错误引用。下文成员刷新已消除已复现的合并模型触发条件，但不能替代共享导出器对异常引用的拒绝检查。该防护仍未补上，44+44 项通过不能证明此异常入口安全。后续修正应覆盖 GUI/CLI 共用导出器、无效对象/实例/映射及正常导出，并核对失败清理行为。

异形床指配置中的实际可打印区域为圆形或其他非矩形轮廓。之前多盘 JSON 使用外接矩形判断，可能把实际床外对象报成盘内；下文补丁已复用真实轮廓判断，仅调整多盘返回归属，单盘仍保留实际坐标。普通矩形床的固定位置空盘误判属于另一个已修复的成员登记问题。

### 异形床问题的来源追溯

进一步按用户问题核对 Git 历史：`PartPlate::check_outside` 的非下沉对象外接矩形检查可追溯至上游 BambuStudio 初始源码提交 `1555904beff`（2022-07-15），并非本次定制改写出的矩形判断。但本项目的 `export_model_transforms`/分盘 JSON 是定制提交 `1b3bcc2a92`（2026-01-26）新增，最初仅使用盘成员归属；`4e50224aca`（2026-10-17）新增独立居中返回规则时，又将 `check_outside` 当作充分的盘内判定，仍未核对异形床真实轮廓。因此此次 JSON 误报属于本项目定制逻辑遗漏，不能将其表述成已证实的官方完整切片流程缺陷，也不能仅凭该行 blame 说异形床误报首次出现在 10 月 17 日。此前“既有 JSON 判断缺口”指本项目已有定制实现，不是官方缺陷归属。当前未提交补丁已补真实轮廓判断；本次仅追溯源码，无新增实现或运行验证。

## 当前复审：未提交补丁及遗漏边界（完成，又修复两处）

用户要求检查当前改动本身是否有问题，以及是否还有其他同类问题。基准仍为 master `ba979559b0` 加本文未提交补丁；
已核对补丁与共享盘成员、打印开关、模型变换、选盘和G-code导出调用，并补测未覆盖的边界。
先前76次验证没有覆盖下面两个问题，不能据此宣称全部入口无回归。当前修正后重新最小构建并完成88次自动化运行。

### 确认的缺陷与修正

1. **合并模型仅导出3MF残留旧成员。** 新增原始网格登记发生于 `--assemble` 之前，合并将两个对象替换成一个，原切片专属刷新在“仅导出”时没有运行。实测进程退出0，但盘成员多出 `object_id=0 / identify_id=0`，指向不存在的对象。原始输入是本批补丁遗漏的生命周期回归；输入已有3MF再合并也复现了同类旧成员问题。修正为原始网格、合并模型和切片动作均在最终变换后无移动地刷新；非合并且非切片的已保存工程继续保留元数据。追加导出引用完整性校验、原始/工程合并导出、合并切片及重开切片。
2. **异形床的多盘JSON误报盘内。** 三角形床(0,0)-(100,0)-(0,100)，45mm方块中心(75,75)在床外，但原 `check_outside` 非下沉分支只核对床的外接矩形，JSON返回真实盘0。实际切片使用BuildVolume，会正确拒绝，造成两份结果不一致。这是既有JSON判断缺口，不是单/多盘模式限定新引入。多盘返回现已额外复用 `ModelInstance::calc_print_volume_state(BuildVolume)` 判断真实轮廓；单盘继续保留实际XYZ，未改共享排布或实际切片边界。

### 验证与范围

- `review-boundaries/probe.py`、`summary.json`：使用上一版身份一致的 `4531efe8...` 程序完成14项隔离调查。固定位置STL/OBJ不加贴床、旋转缩放、禁放区拒绝、复制摆盘、部分下沉、悬空拒绝、显式选择正常盘且其他盘越界等均核对实际结果；整盘以下的已保存备用对象维持既有工程语义，未将这些正常/预期失败情况列为缺陷。
- 红灯：`review-red-slicing/validation.json` 43项中2项失败，均为合并仅导出的无效对象引用，重开依赖项因此未执行；`review-red-arrangement.log` 在三角床盘外模型断言失败。保留原证据，不用改断言掩盖失败。
- 当前应用 **Mac arm64 Release、MeshLib OFF**，`build-review-boundaries.log` 4步退出0，既有编译警告无新增错误。程序路径仍为本目录 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，未发布版本 `2.3.2.3-Release`，最新SHA256 **`3c2db788536353c6789613a6055ef8069ea71d498f5550ba7ef5973f958a5c5e`**。
- 最终 **44/44切片/工程回归通过**（`review-final-slicing/validation.json`）；包括成功G-code、错误拒绝、实际坐标、3MF所有盘成员引用、切片包内容及合并保存重开。
- 最终 **22组成对、44次分盘导出通过**（`review-final-arrangement/validation.json`）；单/多盘、异形床、原有固定位置、超尺寸、正负Z、37个虚拟盘等均通过，每组JSON开/关的真实网格、变换、打印开关及盘成员相同。
- 工作流YAML解析、发布前步骤顺序、新增shell语法、Python AST及Git差异检查通过。Linux包脚本确认AppRun及bin/orca-slicer的包内布局；未执行Linux原生构建、服务器部署或Windows验证。
- 本轮新增修正为CLI成员生命周期及返回投影，GUI ArrangeJob/共享排布算法未改；真实轮廓判断复用GUI/切片已用的BuildVolume核心。此前GUI打印开关修复仍按下文具体清单待手测，不将CLI结果当GUI验收。
- 最终程序、两份脚本和源码身份保存在 `review-final-identity.json`；模块正文/指纹同步后全量35模块、0问题。所有证据仍位于 `planning/cli-fixed-position-20261019/`，不提交产物，不推送部署。

上述回归覆盖的业务场景已通过；后续源码复核另确认共享 3MF 导出器仍缺少异常引用保护，见文首补充记录。不能将这些通过结果扩大为整个 Orca 所有功能或平台已全面验证。

## 当前续接：单盘／多盘 JSON 规则（2026-10-19，已修复并完成本地验证）

用户明确：摆不下的模型独立返回盘、XY 居中、底面 Z=0，只适用于多盘分盘；单盘摆盘没有此要求。
本轮开始时 master `ba979559b0` 加未提交修复仍沿用 `4e50224aca` 的无条件投影，确实扩大到了单盘。
本轮限定修正 JSON 模式判断：多盘保留原需求；显式 `--auto-plate 0` 保留实际归属（无归属沿历史回退盘0）和实际底面中心，
不新增返回盘、不居中、不归零。真实模型、摆盘算法和 3MF 不因开关 JSON 导出而改变。
CLI 入口为 CLI::run 的 export_transforms；GUI ArrangeJob 不消费此返回协议，共享 ModelArrange/PartPlate 仍保持原行为。
之前的空盘／漏切修复继续保留，不能把虚拟返回盘当作真实切片盘。

另一工作目录 `orca-slicer-soft` 当前为 `codex/model-feature-analysis-20261018` / `40d8be288d`，其审查只验证未修复版本；
本目录尚未提交的修复不会自动出现于该目录。本轮不修改它或 Linux 发布分支。
已完成模式限定与成对回归，改动仍未提交、推送或部署。当前程序路径同下文 GUI 清单，版本仍为未发布的 `2.3.2.3-Release`，
最新 SHA256 为 `4531efe8ce6e2c4a507f85ef813ca1f6823022c0d98c16dd152e2e7efce5abf8`。

### 本轮证据与边界

- 旧程序红灯：`mode-boundary-red.log` 在 explicit-single 的独立返回盘断言失败；实际错误为 oversized-corner.stl 返回虚拟盘1、(50,50,0)。
- 最小重建：`build-mode-boundary.log`，Mac arm64 Release、MeshLib OFF，4步退出0；既有警告保留。
- 最终 `mode-boundary-final/validation.json`：18组成对、36次CLI全部通过。正常固定位置在单/多盘均返回盘0、(30,40,0)；固定盘外(125,40,0)在单盘保留原位置，多盘返回独立盘1、(50,50,0)。单盘摆不下的150mm模型返回实际(150,-20,0)，多盘仍按需求独立居中。每组核对开/关JSON的真实3MF网格、变换、打印开关、盘成员一致。
- 单盘正/负底面、超床/超高、容量溢出、重开多盘工程，以及多盘正常排布、省略auto-plate、矩形床、37个越界实例均覆盖。非零Z用3MF显式夹具验证；第一轮新增固定位置夹具错误地假设 `--model-position` 第三个值会保留7，源码原有逻辑固定贴床，实际3MF也是0。该测试错误保留在 `mode-boundary-green.log`，改为明确Z0的固定XY用例；没有改产品Z解析或放宽3MF实际底面断言。
- 同一最终程序 `mode-boundary-slicing/validation.json`：原40项切片回归全部通过，正常固定坐标保持且有实际G-code；非法/空输入按预期失败。
- Python AST、工作流新增步骤的 bash 语法、全量35模块文档检查（0问题）和 `git diff --check` 通过。二进制/两份脚本与验证身份匹配，源码、版本和工作流SHA256保存于 `mode-boundary-identity.json`。
- Linux工作流已补充在发布前运行36次单/多盘分盘对照，复用切片测试参数并保存证据；尚未执行Linux原生构建或发布。其他工作目录不自动获得本次未提交修改。
- 本轮仅CLI返回协议，无新增GUI入口或共享排布算法修改；此前禁用实例边界修复的GUI手测仍待按下表执行，清单中的应用路径不变，程序身份以上述最新SHA为准。Windows/Linux和真实服务器业务测试尚未完成。

证据均位于 `planning/cli-fixed-position-20261019/`。下文保留先前构建和22次旧规则回归，不以它们证明本轮模式边界。

## 追加审查：分盘与切片边界

用户要求继续检查 master 是否有同类影响分盘、切片的问题。当前基准仍为 master `ba979559b0` 加本文未提交修复，保留原工作区改动，不发布或部署。追加审查最终程序为本目录 Mac arm64 构建，SHA256 `3dbeb219ad6d78766f1a762bc0e574573cb287e35f27729942208b7ad8143408`；之前 `3447a160...` 用于本轮红灯复现。

范围：从模型导入、变换／盘归属到指定盘／全部盘切片和产物导出，重点检查原始网格、3MF、禁用／越界实例、选盘索引与返回成功的条件；并对照 GUI 共用盘和格式实现。先用当前程序建立边界用例复现，确认问题后再做最小修正，源码改变后重建再测。

CodeGraph MCP 未暴露；已安装 CLI 位于 `~/.local/bin/codegraph`，本工作目录没有 `.codegraph` 索引、没有项目 `.mcp.json`。按仓库约定使用本分支文档与源码，不初始化或借用其他工作目录索引。

已复现：混合有效／完全越界原始网格输入和单盘摆放溢出仍返回成功并漏掉模型；禁用但部分越界实例拦住正常模型，禁用实例还误计入三角面数限额；不存在的原始输入盘号误报为空盘。`master-audit-red-final/validation.json` 用上一轮已验证二进制执行 34 项，其中 9 项未符合最初断言。对照 GUI 和工程选盘后，将其中“保存工程的无归属备用模型”重新判定为原有有效行为，保留成功对照；其余 8 项对应实际缺陷，不把试验性断言失败全部算产品错误。

已修正：切片前按最终几何刷新盘成员（不改变位置、对象顺序或非切片导出）；全部盘检查无盘元数据批量输入的所有启用实例，显式选盘检查该盘成员；遗漏／越界在任何 G-code 生成前报错。保存工程的盘外无归属备用模型仍与 GUI 一致，不加入物理盘切片目标；不将 JSON 的虚拟返回盘当作物理盘。禁用对象不参与 CLI 越界判断和面数统计。最终盘号重验；新增贴床后重新归盘对照。GUI `GLVolumeCollection::check_outside_state` 的最后实例级聚合原本绕过 printable，已同步加打印开关，保留体积的显示状态。GUI Plater 校验将聚合结果写回盘就绪状态，核心 Print::apply 原本已过滤禁用对象；GUI 操作仍待手测。

来源复核：CLI 按 Inside 统计、仅拒绝 Partly_Outside 的分支及未过滤禁用实例的检查可追溯到 2023 年 `088f2a4acef`／`c65a5f8bf51` 等既有实现；GUI 最终实例汇总的分支来自 `1555904beff`（2022）。本轮是补查出既有边界漏洞，不将其归因于 10 月 17 日 JSON 投影改动；逐行 blame 不能代替整项行为最早引入时间的全历史二分。

### 追加验证结果

- 应用最小重建已通过（macOS arm64 Release、MeshLib OFF）；最终日志 `planning/cli-fixed-position-20261019/build-master-audit-scoped.log`，退出 0。有既有编译警告，无错误。
- 首轮扩展 40 项有 39 项通过；“显式选择全禁用盘”错误码从 -50 变成 -60，被旧断言抓住。修正为仅真正 skip-objects 后无对象使用 -60，没有放宽断言。最终源代码重新构建后，`master-audit-final/validation.json` **40/40 通过**。
- 成功用例核对真实挤出路径；固定坐标保持；禁用模型无挤出；全盘、首盘禁用、明确第二盘的切片 3MF 校验 CRC、G-code 列表及内容逐字节一致。失败用例无 G-code，不报告成功。
- 原有 JSON／3MF **22 次运行全部通过**，退出 0，证据 `master-audit-oversized.log` 及 `master-audit-oversized/`；验证 JSON 投影和未改变的实际 3MF。非切片导出路径不进入新增归盘和切片准入检查。
- Python AST、`git diff --check` 通过；全量模块文档检查 35 模块、0 问题。最终二进制／脚本与测试身份再次一致性核对，源码、版本与工作流 SHA256 保存于 `planning/cli-fixed-position-20261019/master-audit-identity.json`。
- 未触发 Linux 构建、镜像打包或部署；本次追加修复只写入用户指定的 master 工作目录，尚未同步 Linux 发布分支。Linux 工作流会执行扩展后的同一脚本，但尚未实际运行该平台。

### GUI 待验证清单

应用准确路径：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/build/arm64/src/Release/OrcaSlicer.app`，内部版本仍为未发布的 `2.3.2.3-Release`，以本节二进制 SHA256 区分前一轮构建。

夹具目录：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/planning/cli-fixed-position-20261019/master-audit-final/`。
打开 `disabled-partly-outside.3mf` 并加载工程参数：FFF、100×100×100 mm、0.4 mm 喷嘴、0.2 mm 层高、PLA、关闭支撑／筏层／裙边／擦料塔。
两个 8×6×2 mm 方块，`box.stl` 底面中心 (20,30,0)，`second.stl` 中心 (99,30,0)，后者关闭打印；不自动摆放。

| 场景 | 操作和预期 | 状态 |
| --- | --- | --- |
| 禁用对象越界 | 选择第一盘并切片；只生成 box.stl 路径，不被灰色 second.stl 越界阻止。 | 待 GUI 手测；对应 CLI 已通过 |
| 恢复打印开关 | 在对象列表启用 second.stl 的打印；保持位置重新切片，应拒绝越界。再次关闭后应恢复可切片。 | 待 GUI 手测 |
| 保存重开 | 关闭 second.stl 打印后另存工程，再重新打开并加载参数；位置／开关保持，第一盘仍可切片。 | 待 GUI 手测 |
| 指定第二盘 | 打开同目录 first-disabled.3mf，选择第二盘，切片并导出切片包；保留第二盘的有效 G-code，不因第一盘禁用而失败。 | 待 GUI 手测；对应 CLI 包内容已通过 |

回传本次应用日志的操作时间段、另存工程／切片包及实际提示即可复核；不需要连接打印机。界面端改动仅为检查入口同步，尚未以截图或 CLI 结果替代交互验收。

## 当前 master 工作目录

用户要求在原 master 修正。已在 `orca-slicer-soft` 仓库的独立工作目录 `../orca-slicer-master` 检出 `master`，基准 `ba979559b06c8da78ffbbb76597c68a9c51d28cc`，初始工作区干净；原功能分支及用户未提交修改保持。迁入本次小范围 CLI 修复、回归和发布前检查；没有搬入其他 Linux 分支修改。此处无 CodeGraph 索引，不初始化。master 已独立构建并通过下列回归；后文保留的原始 17 项验证属于 Linux 发布分支的本地 Mac 构建，不混同二者身份。

### master 首轮验证（追加审查前）

- 本目录从 master 源码独立配置并编译 macOS arm64 Release，573 个构建步骤完成，退出 0。仅读取已有第三方安装依赖，第一方库和应用均重新编译。
- 应用版本 `2.3.2.3-Release`；路径 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。
- 应用 SHA256：`3447a160697674d37acae565885bd5b881aaddce5334e52f0937b904c0fd29d2`。
- `test_cli_fixed_position_slice.py`：19/19 项通过，包含不带 JSON 开关的固定位置 STL/OBJ；证据 `planning/cli-fixed-position-20261019/master-final/validation.json`，记录的脚本身份对应当时 19 项版本，不是后续 40 项脚本。
- `test_cli_oversized_arrange.py`：22 次 CLI 运行通过，JSON 越界投影、实际 3MF 保持及多盘排布回归通过；证据目录 `planning/cli-fixed-position-20261019/master-oversized/`。
- Python AST、工作流 YAML、新增 Bash 步骤语法及发布前顺序、`git diff --check` 均通过；`check_ai_module_docs.py --all` 检查 35 个模块，0 问题。
- 本次改动仍未提交、推送、打标签或部署；Linux AppImage 发布前回归已接入工作流，尚未远程执行，Linux 环境和真实业务任务待部署测试。

## 目标与源码身份

- 用户反馈：升级容器 v2.0.21 后 STL/OBJ 切片返回 0 但没有 G-code，回退 v2.0.20 恢复。
- 修改仓库：`orca-slicer`，分支 `linux/release2_3_2`，起点 `4b9eb2cc504cba25e7e4365b5c902fbd1901d972`，初始工作区干净。
- 本目录没有 CodeGraph 索引；依据本分支模块文档及源码定位，没有初始化或借用其他分支索引。
- 范围：修复 CLI 不摆盘时的盘成员初始化；切片无产物必须失败；增加实际 CLI 回归和 Linux 发布前验证。部署由用户完成。

## 已确认原因

日志中 `--slice 0 --auto-plate 0 --arrange 0` 导致 `need_arrange=0`。普通 STL/OBJ 导入未建立 PartPlate 成员，新增的空盘过滤先于打印体积校验执行，将真实模型误判成空盘，最后仍返回成功。该过滤由 `b05085f7cd1a0b048fcd3a21020a3742be571c6f` 引入。

GUI 已由盘管理流程建立成员；本次修正 CLI 生命周期，不改变 GUI 排布策略。仍需保持固定坐标、禁用实例、真实空盘、多盘和显式盘号的既有语义，不能通过打开自动摆盘掩盖问题。

## master 提交追溯与发布范围复核

用户质疑“只改分盘返回 JSON 为什么影响切片”。按提交逐项核对如下：

| 提交 | 本来要解决的问题 | 本次判断 |
| --- | --- | --- |
| `4e50224aca881146c0bab13aed9a18495cb4f838`（10-17） | 越界模型在变换 JSON 中独立盘号、居中、Z=0；真实模型与盘结构不改 | `src/OrcaSlicer.cpp` 的改动均在 `if (export_transforms)` 内。本次失败命令没有 `--export-model-transforms`，不是直接触发点。 |
| `b05085f7cd1a0b048fcd3a21020a3742be571c6f`（10-08） | 大模型重建预算、替换网格缓存、悬空误报、输出层数统计、切片包多盘导出 | 共 43 文件。其中 CLI 仅新增 7 行空盘／全部禁用盘过滤；这 7 行引入固定位置原始网格被误跳过的回归。 |
| `4b9eb2cc504cba25e7e4365b5c902fbd1901d972`（10-19） | 将 master 合入 Linux 发布分支并打包 | 两父提交为上一 Linux 打包基准 `e5640e1a5` 和 master `ba979559b0`，并非只合入 JSON 补丁。两个 Linux tag 之间共 36 个新增提交、261 个文件差异。 |

`b05085f7c` 和 `4e50224ac` 都不在 `manual-build-linux-20260923-01` 中，都已进入 `manual-build-linux-20261019-01`。
这是源码 tag 身份对比；尚未从旧 Docker 镜像提取构建 commit，不能据此将旧容器绝对等同于上一 tag。

实现错误是将 GUI/已有 3MF 的“盘成员已建立”前提套到原始 STL/OBJ CLI。
`PartPlate::empty()` 只检查 `obj_to_instance_set` 是否为空，不能用它证明尚未登记的原始输入没有模型。
历史验证虽覆盖已有 3MF、多盘导出及核心层数/缓存，没有覆盖后端的原始 STL/OBJ + 固定位置 + `--slice 0` 完整路径。
因此单项 JSON 改动范围正确，不等于整个发布包只改 JSON；发布说明和回归覆盖均遗漏了这个边界。

### “空盘”“空层”“无 G-code”分别判定

用户已澄清“空层”指前述 `empty()` 判断，并非另一个 G-code 中间缺层问题。因此本次问题就是盘成员尚未登记造成误判；本轮没有实际中间缺层的独立报告。

- 本次日志 15:26:09、15:26:48 两任务都先输出 `Skip non-printing plate 1`，没有 `start Print::process` 或 `export_gcode finished`，随后 `All done, Success`。证据支持整盘未切片，不能据此声称已发生模型中间空层。
- 导出日志全文有 4 次 `Slicing volumes - removing top empty layers`，均出现在旧 `2.3.2.0-Release` 的成功任务内；这是普通调试阶段信息，不是新版本空层报错。
- 同一个 `b05085f7c` 确实修改 `GCode.cpp` 的总层数统计：历史龙模型实际输出 112 层、设备占位符统计 113，修正为按实际输出层统计。源码保留原层组，不删除模型层；历史记录有同参运动指令不变的验证。这不能代替另一模型的实际空层调查。

补充两个回归：固定位置 STL/OBJ 明确不传 JSON 导出开关，仍核对实际 G-code 位置；共 19 项，防止只在 JSON 路径开启时验证成功。

## Linux 发布分支的前序执行记录

- 已完成：原始发布分支编译及红灯复现；非移动式成员初始化、无文件／全跳过保护；首轮同源码 17 项回归通过。
- 已完成：Linux AppImage 发布前接入真实 CLI 回归及失败证据留存；公开版本调整为 `2.3.2.3-Release`。
- 已完成：`2.3.2.3-Release` 最终同源码编译；17 项新增回归、22 次既有摆盘回归、脚本和工作流静态检查通过。
- 文档：本次 cli/build/test-tools 的归属、指纹、正文和链接检查通过；全量检查剩 2 项 HEAD 已存在的过期指纹，详见下文。
- 下一步：用户将本次修改纳入 Linux 构建，包内回归通过后在测试容器执行真实业务复测。
- 尚未执行：Linux 原生构建、镜像打包、服务器部署、真实业务复测。未提交、推送、打标签或触发远程构建。

## 验证记录

### 构建与第一轮实跑

本地 macOS arm64，使用本仓库 `build/arm64`、Ninja Multi-Config、Release、MeshLib OFF。
初始旧缓存残留 Homebrew Boost 1.90 路径，首次配置失败；显式选择现有第三方依赖前缀中的 Boost 1.84 后配置成功。
仅读取 `orca-slicer-soft/deps/build/arm64/OrcaSlicer_dep/usr/local` 的已安装第三方依赖及固定 zstd 14.0 构建目录，
应用和第一方库全部从当前 Linux 发布分支源码重新编译，没有复用其他分支业务对象文件或改动该工作目录。

命令：`cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 8`。
本次应用：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。
证据根目录：`planning/cli-fixed-position-20261019/`（忽略的本地运行产物）。

原始源码实际编译后，在 `baseline-final/` 复现固定 STL、OBJ、多模型返回 0 却没有 G-code；
全部禁用盘同样返回 0 且无产物。自动摆盘对照、多盘正常切片以及首盘禁用／空盘对照通过。
最初夹具改写 XML 未保留默认命名空间导致 -6，已修正夹具并重跑，不作为引擎缺陷；
全空／显式空盘被导入器提前拒绝的 -6 是既有行为，最终测试允许该错误。

首轮修复构建结果在 `fixed/`，17 项全部通过。固定位置输出 `plate_1.gcode`，
多盘按实际盘号输出，全部禁用返回 -50，非切片工程导出仍成功。
随后追加实际 G-code 每对象路径坐标断言，已有三项固定位置产物复核通过；最终脚本将随新版本完整重跑。

### 最终验证（16:37 后）

- 完成版本更新、受影响行 clang-format 及重新构建，`build-final.log` 成功；无错误，有既有编译警告。
- 日志确认 `Current OrcaSlicer Version 2.3.2.3-Release`。
- 最终应用 SHA256：`dd197af337748578ff1734d4856f16014a0ae38956573b9ed9b10a54a82e4974`。
- `src/OrcaSlicer.cpp` SHA256：`2909871e3fa03e3410c16872b65dc37ad424fe529a426e0b2d2dee54585e5351`。
- 完整身份清单在 `planning/cli-fixed-position-20261019/final-identity.json`。
- `python3 scripts/test_cli_fixed_position_slice.py build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --outputdir planning/cli-fixed-position-20261019/final`：退出 0，17/17 通过，证据 `final/validation.json`。本轮包含实际 G-code 坐标检查。
- `python3 scripts/test_cli_oversized_arrange.py build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --machine planning/cli-fixed-position-20261019/final/machine.json --process planning/cli-fixed-position-20261019/final/process.json --filament planning/cli-fixed-position-20261019/final/filament.json --outputdir planning/cli-fixed-position-20261019/oversized`：退出 0，22 次 CLI 运行通过，覆盖原有 JSON 越界投影、实际 3MF 保持及多盘排布。
- Python AST、工作流 YAML 解析、新增 Bash 步骤 `bash -n`、发布前步骤顺序检查和 `git diff --check` 通过。
- `python3 scripts/check_ai_module_docs.py --all`：剩 `jobs`、`multimaterial` 两个过期指纹。用 checker 的只读 HEAD 快照独立复核，二者在起点 HEAD 即已过期，且目标源码身份与工作区相同；没有为消除告警修改无关模块。相同检查器限定本次 cli/build/test-tools 的复核通过，证据 `docs-check-scoped.json`。

本轮最小应用及回归在 macOS arm64 执行，不能标记为 Linux 原生验收；Linux 工作流门槛已实现并静态校验，尚未远程执行。

### 已落地的预防

- STL/OBJ 导入补齐初始盘成员，`add_instance(false)` 不重排或居中，越界输入不会被当成空盘跳过。
- 导出立即核对非空 G-code 文件；整次切片无成功导出时非零退出，JSON 保留失败原因。
- 回归同时断言实际挤出和位置，不仅检查退出码或文件名；所有案例使用新目录，旧文件不能掩盖失败。
- Linux 打包工作流验证实际 AppImage 解压产物，通过后才进入上传及 Release 步骤；失败也上传回归证据。
- 此措施防止已覆盖的同类缺陷再次随该工作流发布，不能保证所有模型／配置永不出现其他故障。

## 用户部署测试交接

1. 在 `linux/release2_3_2` 将本次修改纳入下一次 Linux 构建；等待 `Verify packaged CLI slicing` 成功。
   本地 Mac 测试不代表该 Linux 步骤已经运行。下载本次新生成的 AppImage，保留其 SHA256。
2. 在新的测试容器中替换引擎，`orca-slicer -v` 应显示 `2.3.2.3-Release`；使用新的镜像标签，保留可用旧版作回退。
3. 用 15:26 失败任务对应的 STL 和 OBJ、原有机型／工艺／耗材配置与同一命令复测，保留
   `--slice 0 --auto-plate 0 --arrange 0` 及模型位置参数，不打开自动摆盘作为绕过方案。
4. 每次使用全新任务目录，要求进程退出 0、非空 `plate_1.gcode`、后端成功上传并将任务置为成功。
   日志不应再出现真实单盘模型被 `Skip non-printing plate 1` 跳过。
5. 同时测一个越界输入应失败；原日志中全部禁用等异常场景不能再返回成功。回传引擎版本、
   容器镜像 digest、命令、任务 ID、日志和 G-code，即可与自动化证据核对。

GUI：本次修改仅在 `CLI::run` 和发布／测试脚本中，不修改 GUI 入口或共享算法；没有新增 GUI 控件或需手测的 GUI 交互。
服务器业务、Linux 运行库、真实模型／打印质量仍由部署测试验收，不能用小方块回归替代。
