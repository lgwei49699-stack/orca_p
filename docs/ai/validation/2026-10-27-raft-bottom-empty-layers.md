# Cura V1 垫层与模型首层空层误报修复

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：普通支撑插入垫层与模型之间时，仍按实际垫层接触面和已解析气隙校验模型首层；保留真实空层保护，不改模板、层高或支撑路径。
- 工作目录：`orca-slicer-master`，分支 `master`，基线 `625b37a2669cf5d21a8df3779d68297a7843a6da`。
- 原有 OBJ 解析代码、测试及 formats/support/test-tools/旧验证文档改动均保留，不纳入本次实现范围。
- 已核对：master 已有偏心 ZigZag 垫层修复；勺子原配置 CLI 退出 0。海绵宝宝仍报 1.33～1.98 mm 空层（退出 156）。证据目录 `/Users/liwei/Downloads/垫层底部空层修复_master_20261027`。
- 当前步骤：共享核心修复、应用/测试最小构建、自动化、真实模型 CLI 和本机 GUI 验证已完成。GUI 由 AI 按用户要求实际操作本 master 新进程，覆盖两个代表模型、垫层开关/保存重开和真实缺层负例；未逐条重跑汇总中的全部 10 条任务。本次提交范围仅为空层误报修复、对应测试和文档；未推送，其他平台构建与实机打印待验证。
- CodeGraph：MCP 未暴露，CLI 已安装，本工作目录无索引，按模块文档和源码定位，不初始化。

## 关键决定

1. 不搬入分析分支的通用 FillRectilinear 试验修改。当前 master 的 SupportCommon 已修复勺子同类偏心垫层，已有测试与原模型均可通过。
2. 海绵宝宝实际垫层顶面为 1.08 mm，气隙 0.5 mm、模型首层厚 0.4 mm，模型首层 Z=1.98 mm 正确。普通支撑在 Z=1.33 mm 有路径，使旧检查改用普通支撑间隙 0.24 mm，错误上限为 1.97 mm。
3. 新规则仅用于 Cura V1、已确认接触层有垫层路径（或合并垫层覆盖）、模型真实首层且位于计划高度的情形。缺少接触层、首层路径或中部路径仍保留原检查与空层告警。
4. GUI 与 CLI 共用 `Print::process` / `Print::export_gcode` 内的 `GCode::collect_layers_to_print`，不新增入口或参数。

## 验证记录

| 阶段 | 结果 |
| --- | --- |
| 修改前 CLI | 勺子退出 0、有 G-code；海绵宝宝退出 156，提示 1.33～1.98 mm 空层 |
| 修改前回归 | 正常/共享接触层在逐层、逐对象共 4 组均误报；首层缺失正确抛 SlicingErrors（初版夹具误按单个 SlicingError 捕获，已修正） |
| 本次实现 | 仅 GCode.cpp 增加真实垫层接触锚点与计划首层判断；test_support_material.cpp 增加 18 组正反例，覆盖处理和导出阶段 |
| 构建 | arm64 / Release，fff_print_tests 与 OrcaSlicer 最小构建退出 0；build-after.log |
| 聚焦测试 | `[RaftGap]` 18 组参数、194 断言通过；test-after.log |
| 完整回归 | `[SupportMaterial][Raft],[LayerCount]` 91 用例、52,214 断言通过；test-suite.log（含聚焦用例，不叠加统计） |
| 修改后 CLI | 6 组均符合预期：海绵宝宝/勺子原参数与各自无垫层对照退出 0；向日葵 0.4/0.32 mm 首层仍退出 156；cli-results.json |
| 文档/差异审核 | 35 模块零问题，git diff --check 通过，最后复核源码与已测程序 SHA256 一致 |
| GUI | 本 master 应用构建确认退出 0；海绵宝宝/勺子切片和导出通过，海绵宝宝无垫层与恢复后保存重开通过；向日葵 0.32/0.4 mm 首层仍显示真实空层警告，详见下文 |


## 修复边界与审核

- 生产改动仅 `src/libslic3r/GCode.cpp`，复用计划参数及现有路径来源标记。只增加实际 Cura V1 接触层锚点；须层号、顶面 Z 正确且有挤出体积，或已被共享垫层覆盖。判定仅应用于有路径、Z 与计划一致的模型首层。
- 原普通支撑间隙、legacy 路径、首层无挤出的异常和后续空层告警保留。不改支撑/模型路径、气隙、模板、数据库、后端服务或默认值。
- 测试覆盖正常/共享接触、接触路径缺失、只有普通支撑来源、第一垫层缺失、模型首层缺失、中部连续缺层、模型首层抬高、legacy；各自覆盖逐层/逐对象。两阶段同时检查 psSkirtBrim 与 psGCodeExport，首垫层缺失应在 process 阶段抛 SlicingErrors。
- 同一 master 上勺子修复前后 179,374 条 G0/G1/G2/G3 运动指令完全相同。`path-counts.json` 记录新 G-code 实际正挤出移动；海绵宝宝五层垫层 Z=0.32/0.62/0.92/1.00/1.08，模型首层 Z=1.98；勺子五层 Z=0.32/0.62/0.92/1.12/1.32，模型首层 Z=1.97。Z=0.28 的起始挤出线不是垫层。
- CLI 向日葵首层 0.4/0.32 的负例仍分别报 1.57～2.07 / 1.57～1.99 mm 空层，无 G-code。此修复不宣称解决实际模型首层缺路径问题。

## 构建与证据

所有日志、模型、配置与产物均在本机证据目录 `/Users/liwei/Downloads/垫层底部空层修复_master_20261027`，不入 Git。
- `identity.json`：源码、测试、应用身份；应用 SHA256 `aaf7539e8a55e62c3a9b2d0448abb5db261f4fe89151177bd7e9450ac72b18b8`。
- `inputs.json`：原始模型路径、SHA256、尺寸、缩放；每个工程一个对象、一盘，旋转 0，原位置和缩放保留。
- `run_cases.py`、每例 `command.json` / `run.log` / `result.json`：真实 CLI 复现，未使用 `--no-check`。
- 测试命令：`build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests '[SupportMaterial][Raft],[LayerCount]' --reporter compact`。
- 资源来自本 master 的 `resources`；未更改。当前应用同时包含工作区之前的 OBJ 解析修改，不能将其误记为纯 HEAD 二进制。

## GUI 实测结果（2026-10-27）

执行 `cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 2` 退出 0，增量构建确认目标已是当前源码，日志为 `build-gui.log`。随后通过 Computer Use 启动并操作本次构建的新 GUI 进程，核对可执行路径和 SHA256；未操作另一个工作目录或旧安装包：
`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。

### 测试工程与配置核对

最初 CLI 导出的 `gui/sponge/sponge.3mf` 和 `gui/spoon/spoon.3mf` 只有汇总配置，没有完整内嵌机器预设。GUI 加载后选用了本机同名 MyMarlin 预设，导出的平台实际为 250×250 mm。该批结果不能作为原机器配置的验证结论，保留作排查证据。

正式复测使用 `gui-verified/*-exact-input.3mf`：从原工程配置提取机器、工艺和耗材值，嵌入项目预设，并赋独立的 `RaftValidation-20261027-*` 名称、清空继承；保留工程的模型几何与变换。这里只构造本地测试文件，没有修改业务模板、资源或生产代码。来源记录在 `gui-verified/fixture-manifest.json`。

最终 GUI 导出确认平台 101×101 mm、限高 105 mm、喷嘴 0.4 mm、原层高/气隙、tree(auto)/default 和 Cura V1 1/2/2 层生效。需要区分业务任务机型 EP3Plus 与历史机器配置内部的 `printer_model=EP3Pro`，本次保留原配置内部字段。

GUI 与原 CLI 输出仍有已记录的非空层配置差异：GUI 固定使用 High Temp Plate，首层热床温度为 45℃；原 CLI 为 Cool Plate、35℃。此外 GUI 规范化单耗材冲刷数组及预设标识。这些差异不改变本次空层判定所依赖的垫层高度、气隙和模型首层高度，但不能声称两份 G-code 完全相同或已验证实机温度。3MF 往返后的路径有小量坐标/离散差异，未宣称逐指令一致；原 STL CLI 的修前/修后勺子运动指令一致结论仍成立。

### 实际操作与结果

| 场景 | 实际结果 | 证据（相对 gui-verified） |
| --- | --- | --- |
| 海绵宝宝，首层 0.4、层高 0.08、气隙 0.5 mm | GUI 切片、预览及导出成功，无底部空层警告；五层垫层都有正挤出路径，Z=1.33 有支撑，模型首层 Z=1.98 有 275 条正挤出移动 | `sponge-exact.gcode.3mf`、`sponge-exact.gcode`、`sponge-exact-sliced.png` |
| 勺子，首层 0.4、层高 0.2、气隙 0.25 mm | GUI 切片及导出成功；逐层预览确认垫层顶面 Z=1.32、模型首层 Z=1.97，分别有 31/60 条正挤出移动 | `spoon-exact.gcode`、`spoon-exact-raft-top-layer.png`、`spoon-exact-first-model-layer.png` |
| 海绵宝宝关闭垫层 | GUI 改 Legacy、层数 0 后重新切片和导出成功，模型从 Z=0.4 开始 | `sponge-exact-no-raft.gcode.3mf`、`sponge-exact-no-raft-result.png` |
| 海绵宝宝恢复、保存重开 | 恢复 Cura V1 后重新切片，另存工程、重新打开并再次切片导出；平台与工艺参数保持，模型首层回到 Z=1.98 | `sponge-exact-restored.3mf`、`sponge-exact-reopened.gcode.3mf`、`sponge-exact-reopened-result.png` |
| 向日葵首层 0.32 mm | GUI 仍提示 `Object can't be printed for empty layer between 1.57 and 1.99`，对象 `f72176a98c5f.obj` | `sunflower-032-warning.png` |
| 向日葵首层 0.4 mm | 在 GUI 中修改首层并重新切片，仍提示 1.57～2.07 mm 空层 | `sunflower-040-warning.png` |

`gui-gcode-verification.json` 保存 GUI 实际导出的底层正挤出移动统计、SHA256 和相对原 CLI 的配置差异。海绵宝宝恢复重开前后的底层路径数量一致。勺子无垫层对照本轮仅有 CLI 验证，未补 GUI；未把它算作 GUI 已通过。

### 失败语义与验证边界

真实缺层时共享核心仍发出 `SlicingEmptyGcodeLayers` / CRITICAL。GUI 实测是橙色警告并保留预览；CLI 未传 `--no-check` 时会转为 156，停止交付 G-code。这是现有入口处理差异，本次只修复误报判定，没有把真实缺层改成正常，也没有新增 GUI 强制阻断。不能用“GUI 已拦截导出”描述这次验证，负例未下发或打印。

当前结论覆盖本机 macOS arm64、上述代表模型与 91 用例回归；不代表汇总中每条任务都已 GUI 重跑，也不代表 Windows/Linux 或垫层剥离、粘附和实机打印质量已验收。


## 合并前本地诊断记录（历史归档）

以下为快进合并前同路径未跟踪文件的原始内容，保留分析和未执行事项的当时时点；当前已提交修复与验证状态以上文及集中测试合并记录为准。

# Cura V1 垫层底部空层调查

## 当前执行状态

- 范围：用户要求仔细分析 10 月 EP3Plus 的 10 条“垫层与模型底部路径衔接异常”，给出优化依据。当前仅诊断和方案验证，未修改生产代码、模板或数据库。
- 基准：`codex/model-feature-analysis-20261018`，HEAD `f9bcf0afe0abc42f327828342b168a806e5763f8`；进入时已有多个模块文档、验证文档及 result.json 的用户改动，保留。
- 本地应用：`build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA256 `b0f013aba4136052df22f5a5103d33214c43dda38c163310dc5dff66ada2f02e`，与上午海绵宝宝诊断相同。生产最终配置及程序身份未完全恢复，不能等同线上复现。
- CodeGraph MCP 未暴露；CLI 可用，索引存在。GCode.cpp、Slicing.cpp、TreeSupport.cpp、SupportCommon.cpp、RaftGeometry.cpp 的索引内容 SHA256 与当前源码相同；只在已核对范围使用。
- 已确认：10 条是海绵宝宝 4 次、蓝白可爱勺子 6 次，均模板 17；关闭垫层的历史本地对照通过。不能把海绵宝宝的气隙校验发现直接用于全部 10 条。
- 已补充核查：勺子确实缺失垫层中间／顶部 4 层路径，不能归为纯气隙误判。独立检查器通过现有 Release libslic3r 读取层状态并重放填充，同一多边形在 ZigZag 中为 0 路径，修正扫描包围盒的诊断对照能生成路径；没有 InfillFailedException。
- 证据目录：`/Users/liwei/Downloads/垫层底部空层分析_20261027`；旧海绵宝宝证据：`/Users/liwei/Downloads/海绵宝宝空层复现_20261027`。
- 当前结果：分析与独立候选验证完成。扫描范围候选恢复勺子 5 层垫层；叠加首模型层气隙候选后，两模型空层告警均消失。8 个受控路径缺失负例仍触发空层告警／首层错误。
- 下一步：若实施正式修复，按下面范围移入共享核心并做 GUI/CLI、普通/树支撑、旧垫层、其他一致填充图案、多对象共享垫层回归。尚未修改正式业务源码或实机打印。

## 调查记录

### 2026-10-27：恢复证据与源码核对

- 当前 `GCode::collect_layers_to_print` 仅在“最近有挤出的支撑层 ID + 1 等于垫层数”且当前是首模型层时，使用 `gap_raft_object`。树支撑可以在垫层顶部与模型底部之间插入额外层，因此最近的支撑层不总是最后一层垫层。
- 该检查只涉及 Z 间隔，不能证明 XY 承托完整；优化时不能把缺失垫层路径也作为正常气隙放过。
- 勺子历史基线报 Z 0.32～1.97 mm；海绵宝宝相关对照报 1.33～1.98 mm。先检查两段间隔的物理来源。

### 本轮补充对照

- 勺子两个历史请求组模型 SHA256 相同：`66575f96906e0741f52472a603becc61e2f21bd565cc789bada9492fe4eda7d2`；真实 scale 为 `1.385985025837745`。独立检查器首轮用展示精度 1.38599，最终诊断需用完整精度重跑。
- 正常 CLI 基线仍 156；仅普通支撑 `normal(auto)` 通过；禁用支撑仍 156；首层 0.24 仍 156；旋转 Z 10 度仍 156，但缺失层段变化。中间/顶部垫层各增加 1 道墙可通过，却只是补充轮廓路径，不能据此声称填充完整。
- 勺子计划垫层 Z：0.32、0.62、0.92、1.12、1.32；首模型层 Z 1.97 = 1.32 + 0.25 + 0.4。实际中间/顶部 4 层 `has_extrusions=false`，轮廓面积约 74/59 mm²，属于“有区域但无填充路径”。
- 海绵宝宝计划垫层顶 Z 1.08；普通支撑层 Z 1.33 有路径、1.58 无路径；首模型层 Z 1.98 有路径。当前校验按最近支撑 Z 1.33 + 首层 0.4 + 向层高取整的普通支撑间距 0.24 = 1.97 判断，差 0.01 mm 报空层。实际首模型层正好等于 1.08 + 气隙 0.5 + 首层 0.4 = 1.98。
- 仅用于取证的 `--no-check` 导出文件位于 `spoon-paths-diagnostic-only`；这不算正常切片通过，不得打印。正常 CLI 单因素结果位于 `spoon-controls.json`；内部层状态为 `spoon-layers.json`、`sponge-layers.json`。

### 扫描范围定位与独立候选验证（已完成本轮诊断）

- 完整 scale 已重跑，`spoon-bbox-probe-exact.json`。例如中间层 Z 0.62，原 ZigZag 扫描 X 区间约 -26.16～-10.51 mm，而旋转后的轮廓 X 区间约 -7.83～6.72 mm，二者不相交。不是轮廓太小或异常被 catch 吞掉。
- 相关代码：`FillRectilinear::extended_object_bounding_box` 只交换 X/Y 的 min/max 再乘 sqrt(2)，并不保证偏心包围盒覆盖任意旋转；`fill_surface_by_lines` 在一致图案中直接使用它。`TreeSupport::generate_toolpaths` 给该填充器的是偏心的首层/支撑区域包围盒。
- 候选 1：扫描包围盒转换到当前旋转坐标系，并合并实际扫描轮廓包围盒；保留原图案基准点、线距和方向，不通过增加外墙掩盖填充丢失。
- 候选 2：只对 Cura V1 首模型层，在真实垫层顶部已有垫层挤出路径（含共享筏层）且实际首模型 Z 与计划 Z 一致时，按 `raft_contact_top_z + gap_raft_object + first_object_layer_height` 校验。不把气隙重复叠加到最后普通支撑层上，不豁免中间空层。
- 为验证方案，在证据目录 `candidate/` 复制两个实现文件并编译独立探针；原仓库业务源码、用户应用、模板、SQL 均未修改。构建与结果另存日志；尚未视为正式修复或 GUI/线上验收。

| 独立核心验证 | 勺子 | 海绵宝宝 |
| --- | --- | --- |
| 当前实现 | 0.32～1.97 mm 空层；只有底层垫层有路径 | 1.33～1.98 mm 空层；垫层本身 5 层均有路径 |
| 只修填充扫描范围 | 5 层垫层均有路径，空层告警消失 | 仍有气隙误判 |
| 叠加首模型层计划 Z 校验 | 5 层路径保留，无空层告警 | 5 层路径保留，无空层告警 |

- 结果文件：`candidate/results.json`。程序调用共享 `Print::process`，不是关闭检查后导出；无空层告警仅证明此次核心切片检查，未执行正式 CLI 入口或完整 GUI/生产环境验收。
- 负例：对两个已处理模型分别清除垫层顶部、垫层首层、首模型层、连续中间层挤出路径。垫层首层为空时抛出切片异常，其余 6 项保留 `SlicingEmptyGcodeLayers` 告警，详见 `candidate/negative-results.json`。
- 首轮负例在不完整导出上下文中继续生成 G-code 时，6 项诊断程序崩溃，未计为通过，原结果保存为 `negative-results-first-attempt.json`。最终负例在捕获指定空层告警后立即停止，只验空层保护，不对后续导出/实印作结论。
- 独立候选 patch：`candidate/proposal.patch`。`GCode.cpp` 和 `FillRectilinear.cpp` 在正式工作区均无改动。

## 建议实施范围与验收条件

1. 在一致图案的扫描范围计算中统一坐标系，并包含实际旋转后的多边形。保留原图案相位、角度、线距和流量；避免以增加外墙替代丢失的中间填充。现有共享填充器同时服务其他一致图案，正式实现应补 ZigZag/CrossZag/LockedZag 的平移、旋转、孔洞及多岛回归，确认健康区域路径的变化范围。
2. Cura V1 首模型层按计划 Z 判断正常分离气隙，要求实际最后垫层接触层有垫层来源路径（含共享覆盖）、当前确为首模型层、实际 Z 与计划一致。原始首层／模型中间空层检查保留；普通支撑和旧垫层不改判定语义。
3. 先验证海绵宝宝与勺子两份原输入和原参数，再覆盖有无支撑、普通/树支撑、0.24/0.4 首层、0.08/0.2 常规层、0.25/0.5 气隙、多对象共享筏层。不能只看退出码，需核对每个筏层阶段实际路径、模型首层 Z、层高/挤出量及承托覆盖。
4. GUI 与 CLI 共用核心，分别验同配置结果。正式 Linux 服务镜像仍需用对应构建复测；本地历史模板重建不能替代生产最终配置。最后验证实机垫层剥离、附着与首层质量。

原汇总应细分为“Cura V1 首模型层气隙误判（4 条）”和“垫层 ZigZag 扫描范围导致中间/顶部路径缺失（6 条）”。本轮未修改原 Excel，避免把调查记录更新误报为已回填。
