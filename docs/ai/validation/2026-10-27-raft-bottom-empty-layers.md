# Cura V1 垫层与模型首层空层误报修复

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
