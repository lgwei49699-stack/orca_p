# 独立重建候选：GUI 验证交接

## 10-07 传统当前盘发送目标修正

本轮仅修传统 `send_gcode_legacy → send_gcode → export_3mf` 的当前盘盘号，不改重建、有效切片配置或 GFD 云打印。以下为待执行 GUI 验证；没有连接设备、上传或启动打印，自动化通过不代表设备发送已通过。

### 程序及输入

基准 master / a7750c76c7 加当前未提交修改，arm64 Release 构建成功。新直接应用 SHA-256 `9911a0a131d5f4bd2ff41933e6cab3c9d9e1ef614d61f29752d3b99d1edf063e`；常用 `build/arm64/OrcaSlicer/OrcaSlicer.app` 仍旧 `46fdfcf7...`，未替换。保存当前工作并退出旧实例后启动：

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
send_current_run=$(mktemp -d /tmp/orca-send-current-gui-XXXXXX)
set -o pipefail
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee "$send_current_run/gui.log"
```

本机可复用 `/tmp/orca-multiplate-ZrJhym/both-enabled.3mf`：两盘各一个 8×8×2 mm 方块实例，两个均可打印，首盘中心 (90,90,1)，第二盘为对应盘内位置。CLI 对照使用工程内 EP7 0.4 / QIDI PLA Rapido / Standard 0.20 mm，层高与首层0.2 mm、无支撑、无新增对象覆盖，未覆盖用户预设。**该 EP7 工程直接点打印走 GFD 分支，不能证明传统入口已通过。**

传统入口手测需在此工程副本选择用户已有的非 GFD、非 Bambu 网络的测试打印机预设及匹配耗材/工艺，保持0.4 mm喷嘴、0.2 mm层高/首层、无支撑，使用该设备有效平台和温度；不猜测真实设备温度或连接信息，不保存覆盖原预设。日志必须确认 `on_action_print_plate` 的 `is_gfd_printer=0` 且实际出现传统上传框。回传普通工程以固定完整有效配置；没有对应测试主机时此项保持待验，不用 EP7 代替。

### 操作和预期

1. 两盘均保持可打印，只选第一盘执行“切片单盘”，第二盘不切片。点击“打印”，进入传统发送框。**仅在用户允许向已有测试主机上传、且可选“仅上传”时确认；不要选择开始打印。** 没有此条件就取消，不能把打开弹框当成导出已通过。
2. 预期第一盘正常生成发送包，不因第二盘未切片弹“异常打印文件数据，请重新切片”；发送包只包含 `Metadata/plate_1.gcode`，盘号映射保持原编号。网络上传失败与切片包生成失败分开判断。
3. 重新打开未切片工程副本，只切第二盘并发送，预期仅含 `Metadata/plate_2.gcode`，不能误发第一盘。再做两盘均切片的全部盘导出对照，预期两个 G-code 均存在；未切片盘的全部盘导出仍应被拒绝或按钮不可用。取消上传不应启动打印或改变模型。
4. 回传本轮 `gui.log`、用于手测的普通 `.3mf`、实际发送缓存包（若可取得），以及异常截图。不要回传凭据。尚未执行这些操作，也未验证 Windows/Linux 或实机打印。

### 自动化与 CLI 已验

- 最终3MF测试16用例555断言通过，含新增12组合：目标盘0/1，其他盘未切片/缓存文件缺失/有效，单盘/全部盘。单盘不连带校验其他盘，全部盘仍拒绝不完整数据；也检验包内容、重开映射和临时文件清理。它验证共享导出契约，不直接执行 GUI 哨兵转换。
- 新应用实际 CLI 六场景符合预期：全部有效盘/显式单盘/另一盘禁用/第一盘禁用均导出正确成员，显式选择禁用盘返回206；成功包 CRC 正常。证据为 `/tmp/orca-multiplate-ZrJhym/send-current-*/run.log`、`result.json` 和 `export.3mf`（失败项无成功包）。复跑可使用 `python3 /tmp/orca-multiplate-ZrJhym/probe.py send-current-manual /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，标签应使用尚不存在的名字。

## 10-07 层数统计一致性（当前待验）

本轮只修“实际输出112、机器/文件尾声明113”的统计，不改温度、重建算法、路径或导出菜单。共享核心与应用 CLI 同输入前后已验证，总层数现在一致且运动指令/配置块不变；详情见[层数记录](2026-10-04-smart-print-engine.md#2026-10-07-输出层数一致性修正)。无需重复重建修复验收。

### 程序、输入与参数

master / a7750c76c7 加当前未提交改动。新程序 SHA `32e2ab2baa5595ef3a4e5c6ce44ea0e2a756c4dd1fbd379853526d219ad4f9cc`；打包路径仍旧 `7e35b86b...`。先保存当前工作并正常退出旧应用，用以下已构建直接路径，无需再编译；不要同时启动两个窗口混淆版本：

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
layer_count_run=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/layer-count-manual-XXXXXX")
set -o pipefail
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee "$layer_count_run/gui.log"
```

优先使用用户已保存的重建后普通工程及已修正的温度配置；没有保存时，可打开本机隔离候选工程 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-slice-compare-atRGYm/gui-rebuilt.3mf`（SHA `5424207daf1eda5838e15f0ff16573113bd838e8b1d8ab207253228f60db969f`）。后者为1盘/1对象/197318面，EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard @EP7、首层及层高0.2mm、支撑关闭、100%尺寸和原摆放；它包含历史温度值，不要保存覆盖当前用户预设。可以把温度改为用户刚确认的值，但不要同时改层高或摆放。界面简中即可，本轮无翻译变更。

### 操作与预期

1. 打开普通工程后，确认准备页有模型；不要把只含切片的 `dragon222.gcode.3mf` 当作可重切的模型工程。点击“切片单盘”，等待完成，然后导出到新验证目录 `dragon-layer-count.gcode.3mf`。
2. 相同模型、摆放、层高下，预览应仍为112层，最高实际输出22.4mm；包内 `Metadata/plate_1.gcode` 的文件头 `total layer number`、启动指令 `SET_PRINT_STATS_INFO TOTAL_LAYER`、文件尾 `total layers count` 均应为112。若采用了不同层高/摆放，不固定要求112，但三者必须等于实际输出层数。计划 `max_z_height` 不属于此三者。
3. 不修改内容重复导出一次；两份文件的层数和模型运动应一致。此轮不需要连接打印机、上传或实机打印，也不用重新运行重建来验证统计。

回传本次 `gui.log`、新切片包；若用当前自己的工程，请同时保存普通 `.3mf` 以核对有效配置/变换。当前这些 GUI 步骤尚未执行，历史首次导出通过不能替代本轮新程序验证。

### 对应 CLI（AI 已完成）

已完成新旧应用对照，命令与结果记录于 `planning/repair/layer-count-6ZXTup/validation-final.json`。用户无需重复；如需独立执行，使用新输出目录：

```sh
layer_count_cli=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/layer-count-cli-manual-XXXXXX")
cd "$layer_count_cli"
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 --orient 0 --arrange 0 --outputdir "$layer_count_cli" --export-3mf dragon.gcode.3mf --slice 0 --min-save /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-slice-compare-atRGYm/gui-rebuilt.3mf 2>&1 | tee "$layer_count_cli/cli.log"
```

## 历史验证（以下身份和待验状态按各轮时间解释）

## 10-07 19:11 回传：首次导出主流程通过

用户已重新打包，两条程序路径均为 `7e35b86b...`。日志 `orca-user-gui-run-20261007-191103-298082.log` 确认19:12:23采用候选、只切一次、19:12:42首次导出刷新apply=0/invalidated=0，19:12:49完成导出。桌面 `dragon222.gcode.3mf` 内有完整G-code、配置和缩略图，ZIP/MD5正常，不再是空切片包。

实际GUI备份197318面/98539顶点，精确索引转OBJ后七项独立复检完整为零；网格与前次候选完全一致，837077条运动指令及导出参数与旧GUI切片相同。112个实际输出层均有主体挤出、Z=0.2–22.4mm连续，未见中间空层，支撑仍关闭且未再出现本次微区悬空告警。现有热床60℃/软化阈值45℃告警与计划113/输出112层统计差异分别记录，不当作模型修复失败。证据与边界见[本次回传记录](2026-10-04-smart-print-engine.md#2026-10-07-1911-gui-首次导出回传)。

下节核心步骤1–2的正常流程已由用户回传覆盖；重复导出、修改层高后拒绝/重切等其余步骤尚无新回传，不能一并标为通过。无需为正常首次导出再重复测试；如需补齐本轮GUI状态保护，继续第3–4步即可。跨平台和实机打印仍未验收。

## 10-07 首次导出失效源头修正后验证（当前待验）

最新修正：模型替换并落床时，区域缓存坐标同步不完整，第一次导出刷新误使已完成切片失效。已在共享 `Print::apply` 修复并完成真实龙模型生命周期修前/后对照，G-code 运动指令/配置不变；缓存/悬空 2 用例 484 断言、格式/重建 34 用例 973 断言、3 项应用 CLI 验证通过。**19:11回传已通过首次切片/导出，其他手测项仍以文档顶部记录为准**；不是要求用户取消 Z 落床，也不绕过导出守卫。详情见[本轮证据](2026-10-04-smart-print-engine.md#2026-10-07-首次导出失效源头修正)。

### 程序与输入

基准 master / a7750c76c7 的当前工作区；新增业务修改仅 `PrintApply.cpp`，测试与模块文档已同步。直接应用已经构建，SHA-256 `7e35b86bae79e16d3ed30bd616c02671cc7a724e06f1f9e2500bfb3218f896ee`。资源仍使用本仓库，简中 MO 与上轮相同。本轮没有替换用户正运行的打包副本；`build/arm64/OrcaSlicer/OrcaSlicer.app` 仍是旧 `1f8bc14c...`。

先保存并正常退出旧应用，再启动以下直接路径；无需为了本次手测再次编译。若自行重新打包，应重新核对程序身份。

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
export_cache_run=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/export-cache-manual-XXXXXX")
set -o pipefail
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee "$export_cache_run/gui.log"
```

打开 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-slice-compare-atRGYm/native-original.3mf`，SHA-256 `49e180bc3a94a77ea6dccee3b5a4a2d249e72ad59160e64bf7ea75b29519a543`。这是原龙模型导入后的隔离工程，1 对象/1 盘、1808822 面；保持 EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard @EP7，层高与首层 0.2 mm、支撑关闭、100% 缩放及原变换。预设缺失/变化先记录，不随意替换。**不能只打开已经重建好的工程来验证本次问题，必须经过采用候选这一操作。**

### GUI 操作与预期

1. 准备页选择龙模型，右键“重建修复”；使用 0.5 mm 半径、0.05 mm 偏移、0.25 mm 告警阈值，勾选预闭合并确认形状风险，生成候选。候选仍须完整检测通过；偏差告警保留，不等于实机通过。
2. 比较窗口勾选“沿 Z 轴移动候选，使其最低点落在打印平台上”，采用候选。**不要关闭/重开工程，也不要先重复切两次**。马上切片一次，等进度结束，直接导出为新验证目录的 `dragon-first.gcode.3mf`。预期第一次即能导出，不弹“没有有效的 G-code”；包内有非空 `Metadata/plate_1.gcode` 和切片参数。本次微小无挤出区域不应产生悬空告警，真实悬空仍可能告警。
3. 不改任何内容，再导出一次 `dragon-repeat.gcode.3mf`，预期仍成功。保存普通工程 `dragon-rebuilt.3mf` 用于复核网格与有效配置；切片包省略可编辑网格是既有规则。
4. 把层高改为 0.22 mm，不切片时不应允许导出有效切片包（按钮禁用/提示重切均可）。切片完成后导出 `dragon-changed.gcode.3mf`，预期成功。证明修正没有绕过真实参数修改的失效保护。
5. 可选快速对照：新工程导入 `tests/data/model_repair_acceptance/01-missing-top.stl`，默认重建并落床、一次切片直接导出。保留 Z 不移动的对照已有核心回归，不要求为本次再跑全套历史 GUI 验收。

回传新验证目录（`gui.log`、三个切片包、普通工程）及异常截图。若第 2 步失败，先保存日志与现场普通工程再回传，不反复重切掩盖首次状态。不覆盖原模型/旧文件，不连接打印机或上传。

### CLI 对照范围

AI 已用最新应用验证同配置候选切片包、缺 G-code 拒绝、普通工程保存；同对象“原件 apply→替换网格/落床→切片/导出→刷新”的关键历史由独立核心探针及自动化回归覆盖。CLI 单次打开现成候选不能代替 GUI 的第 1–2 步。用户可选执行下节 CLI 命令，但应使用本节最新程序身份；不需要重复 CLI 才能手测 GUI。Windows/Linux 与实机仍未验收。

## 10-07 悬空微区与导出修正后验证（此前交接）

**18:18 用户回传更新：守卫通过，正常导出仍未通过。** 用户已重新打包，两条程序路径均为下述 `1f8bc14c...`；本节“旧包”描述仅是交接时状态。18:27:56 已完成切片/写 G-code，18:28:03 导出前刷新再次使切片失效，因此出现重新切片提示。缓存文件仍在，不是用户未切片。本次不应反复按步骤 2 重试，也不能判 GUI 导出已修复；下一步先定位 Print::apply 的失效分支，详见[新包回传](2026-10-04-smart-print-engine.md#2026-10-07-1818-新包导出守卫回传)。

本批基于 `master / a7750c76c7` 工作区改动，修正未实际打印微区的悬空告警和无 G-code 切片包误报导出成功，不改重建网格/支撑开关。自动化与同配置 CLI 已验证，以下 GUI 新交互未验；此前大模型 GUI 未覆盖项仍保留在后节。全量支撑另有旧摆放夹具失败，详情见[执行记录](2026-10-04-smart-print-engine.md#2026-10-07-悬空微区与切片包导出修正)。

### 新程序与输入

先保存并正常退出旧应用。**目前 `build/arm64/OrcaSlicer/OrcaSlicer.app` 仍是旧包**；无需再次编译即可使用已最小构建的以下直接路径。应用 SHA-256 `1f8bc14ce5f88ed63c9a5be435253744def9e79d0d0ccb752be700b0f790a41f`，简中 MO `a463a01041040b26fbcb2bde19d47ec19f1ad3a257aff7109027861f050576f2`；再次构建后需重新核对身份。

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
dragon_export_run=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-export-manual-XXXXXX")
set -o pipefail
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee "$dragon_export_run/gui.log"
```

输入为 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-slice-compare-atRGYm/gui-rebuilt.3mf`：从用户实际采用后的备份生成的隔离工程，197318 面；不用重新修复代替这个候选。保留工程参数：EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard @EP7、首层与层高 0.2 mm、支撑关闭、100% 尺寸及原朝向。若加载提示缺失预设或有效参数有变，先记录，不替换为其他预设后直接比较。

### 手动步骤与预期

1. 打开上述普通工程，切片。预期不再出现这次由无路径微区引起的“浮空区域”提示；逐层仍有主体路径。这不是关闭所有悬空检测，也不是实机通过。原件对照工程为同目录 `native-original.3mf`。
2. 导出到新验证目录的 `dragon-rebuilt.gcode.3mf`。预期导出成功后包内有 `Metadata/plate_1.gcode` 及工程/机型/工艺/耗材配置，可作为切片包重新加载。此种包不含可编辑三角网格是设计行为；另用“保存工程”为 `dragon-rebuilt.3mf` 才保留可编辑模型。
3. 将层高改为 0.22 mm，但不重切，尝试导出。允许按钮禁用或明确提示重新切片；不允许产生仅缩略图/参数的成功包。重新切片后导出 `dragon-changed.gcode.3mf` 应成功。不要覆盖步骤 2 的文件。
4. 若步骤 2 每次切片后仍在导出时要求重新切片，停止重复尝试，回传该普通工程与日志；导出前具体失效触发源尚未隔离，守卫不能当作这一源头已经消除。不要强制回用旧缓存。
5. 可选多盘：新增有模型但未切片的第二盘。导出有效当前盘应可行；导出全部盘不能成功忽略第二盘。切完所有盘后全盘包应含各盘 G-code；空盘无需生成 G-code。

回传验证目录、GUI 日志、普通工程、两个切片包和提示截图；不要覆盖原件/原文件，不连接打印机、不上传。无效目标保护和真实小挤出告警已有核心回归，但不将其写成上述 GUI 已通过。Windows/Linux 与实机仍单列待验。

### 可选 CLI 手动对照

GUI 退出后在同一终端执行；新目录中已有同名输出时请创建新父目录，不覆盖原证据。

```sh
mkdir "$dragon_export_run/cli"
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --debug 3 --slice 0 --orient 0 --arrange 0 --min-save \
  --outputdir "$dragon_export_run/cli" --export-3mf dragon.gcode.3mf \
  /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/dragon-slice-compare-atRGYm/gui-rebuilt.3mf \
  2>&1 | tee "$dragon_export_run/cli.log"
unzip -l "$dragon_export_run/cli/dragon.gcode.3mf"
```

预期退出 0、有 `Metadata/plate_1.gcode`，无本次微区悬空告警；CLI 核心已用此输入验证，本步骤可选，不必重复才能开始 GUI。去掉 `--slice 0` 但保留 `--min-save` 时应拒绝缺 G-code；普通未切片工程导出则还必须去掉 `--min-save`。

## 10-07 大模型优化后验证（此前交接，剩余项待验）

本节覆盖新增大模型支持。源码基于 `master / a7750c76c7` 的未提交改动；原高级修复、预设、最终网格验收不变。225 核心用例、32 CLI 场景和真实 worker 已通过。10-07 16:19 用户新包回传已确认 GUI 采用网格 197318 面、独立七项完整为零，缓存 112 层均有主体路径；但导出前刷新使切片失效后仍生成了没有 G-code 的 `.gcode.3mf`，导出验收不通过。二次同配置对照复现原件不告警/候选告警，候选第 2/3 层新增三处微小无下层承托截面；尚未证明这些微区各自形成独立悬空挤出，不能用网格通过代替可打印，也不直接判定必须加支撑。导出包的参数仍在，缺网格属于切片包 SkipModel 规则，缺 G-code 才是异常。取消/Undo/重开及 Windows/Linux/实机仍待验，详见[二次分析记录](2026-10-04-smart-print-engine.md#2026-10-07-龙模型二次分析同配置悬空对照与导出语义)。

### 启动与模型

先保存并正常退出旧应用。10-07 16:19 用户已重新打包，`src/Release` 与 `build/arm64/OrcaSlicer` 两条应用路径现均为 SHA-256 `ed22cf22bdaf97fa279f7155645938c61a391652e8741055e0438f9a42c770da`；不再使用之前的 `4efcd19a...` / `89bed37f...` 身份说明当前 GUI。以下命令仍可使用直接构建路径。再次编译/打包后先核对应用/资源身份。

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
large_recon_bin="/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer"
large_recon_run=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/large-recon-manual-XXXXXX")
printf '验证目录：%s\n' "$large_recon_run"
set -o pipefail
"$large_recon_bin" --debug 3 2>&1 | tee "$large_recon_run/gui.log"
```

GUI 退出后继续在此终端使用上述变量；另一个终端需重新设置。测试不连接打印机、不上传、不覆盖原模型。

### GUI 操作及回传

1. 打开已有基准 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-gui-20261007-mkXJqu/missing-top-original.3mf`，另存新验证目录中的 `dragon-before.3mf`。保留 EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard @EP7，有效层高/首层 0.2 mm、classic 墙、支撑关闭、无后处理。若预设缺失先回传，不随意替换后比较。删除基准方块，导入 `/Users/liwei/Downloads/dragon (1).stl`，保持 100% 缩放，无绘制/修改器，不另执行旧高级修复，再保存基准副本。CLI 原始 1808823 面，原生导入可能变为 1808822 面，记录实际面数。
2. 准备页选中这个普通对象，右键“重建修复”。默认半径 0.5、偏移 0.05、偏差告警 0.25 mm、预闭合勾选；在这台 64 GiB 机器应显示 **300 秒/4096 MiB**。这不是预期耗时；8 GiB 机器自动内存上限应为 2048 MiB。截图记录参数/预算，检查中英文和滚动区按钮可见。
3. 确认形状风险→生成候选。预计出现完整检查通过、形状超偏差及需试切片说明；默认龙模型 CLI 约 59 秒，不能把这个时间当硬性 GUI 预期。约 6.08 mm 的采样距离告警不等于外形每处扩大 6 mm，需观察实际形状/内部变化；记录完整预览截图。
4. 先选“保留原模型”，确认对象未替换，另存 `dragon-kept.3mf`。再生成一次，运算中点取消，确认原模型保留、界面恢复；另存 `dragon-canceled.3mf`，保留日志以核对子进程退出。不要关闭系统文件框时强制杀进程。
5. 再生成并采用，记录是否勾“沿 Z 落床”（建议本次勾选以检查底部）；该选项只平移，不缩放。另存 `dragon-rebuilt.3mf`，菜单撤销后另存 `dragon-undo.3mf`，重做后另存 `dragon-redo.3mf`。这些必须是普通工程 3MF，不能只交不含网格的 gcode.3mf。
6. 正常退出重开 `dragon-rebuilt.3mf`，用同一有效配置重新切片，逐层看主体连续、底面、薄处、孔/可动间隙、支撑需求和悬空告警；导出 `dragon.gcode` 或 `dragon.gcode.3mf`。如需加支撑，另存工程/配置和第二份 G-code，不能把不同配置路径差异全部归因重建。
7. 小模型对照可用 `tests/data/model_repair_acceptance/01-missing-top.stl`，同样默认参数应显示 60 秒/1024 MiB（RAM≥4 GiB），仍能封顶生成候选。旧“修复模型”行为不在此批更改范围。

回传新验证目录路径、参数/预览截图、上述普通 3MF、G-code、实际软件路径与预设变更情况。AI 收到后独立抽取保存的网格复检，并比较原件/保留/取消/撤销/重做和有效配置，不能只凭成功弹窗判通过。

### CLI 对照

关闭 GUI 后在同一终端运行；输出子目录需尚不存在，重复测试重新建父证据目录。默认预算自动选择，不用附加大模型参数，也不必先减面。

```sh
"$large_recon_bin" --reconstruct-model "/Users/liwei/Downloads/dragon (1).stl" \
  --outputdir "$large_recon_run/cli" --accept-reconstruction-risk \
  2>&1 | tee "$large_recon_run/cli.log"
"$large_recon_bin" --check-model --outputdir "$large_recon_run/check" \
  "$large_recon_run/cli/candidate.stl" 2>&1 | tee "$large_recon_run/check.log"
```

第一条预期退出 0、`candidate_ready`、`mesh_validated=true`，`resource_limits` 为 3000000/1000000 面、300000 ms、4096 MiB（本机）。默认约 197118 面，实际顺序/导入影响不要求逐字节固定。第二条检查 `model_checks[0]` 的 `check_complete=true`、七项完整为零，而不是只看退出码。这里 STL 复检含原生导入；本轮自动验收另已将实际导出的所有面精确索引转 OBJ、不清理面再复检，证据见主记录。

形状告警、试切片未运行以及 `print_ready=false` 都是正常且必须保留的状态。有限资源内失败不得覆盖原件或残留可采用候选；不承诺任何 300 万面模型都能在预算内修好。

## 10-07 用户手动回传结果

基于已提交 `a7750c76c7`，收到三张参数/候选/采用截图、`manual-reconstruction-ko53bY` CLI 结果和桌面 `01-missing-top.gcode.3mf`。默认 CLI 重建/独立复检退出 0、七项完整为零，1 个开边环预闭合新增 2 面，首档 0.5/0.05 mm 成功。GUI 切片包校验一致，51 层均有主体路径，墙/填充/顶底面齐全，没有复现薄壳碎线；其有效配置与之前默认基准一致。

包内热床告警来自 60℃ 热床与 45℃ 耗材软化温度配置，不是重建失败。该 `.gcode.3mf` 不含三角网格，尚不能从本次回传独立复检 GUI 最终网格或确认采用时的 Z 落床选择；如需补齐这两项，另存普通工程 `.3mf` 即可。截图中的 5 mm 反向采样偏差源于新增顶面到原开口边缘的距离，不等于模型扩大 5 mm；落床只沿 Z 平移，不缩放。细节见[主记录的用户手动回传节](2026-10-04-smart-print-engine.md#2026-10-07-用户手动回传cli-与切片包复核)。本轮仅分析和补记录，不改业务代码、预设或再次提交。

## 10-07 用户手动验证快速清单

这是用户主动复验的操作清单，不要求重新构建。与本节同笔提交的业务源码已经在 macOS arm64 Release 验证；包路径如下，应用 SHA-256 为 `89bed37fcafd3ed31abf6494629a569e60ea3722ad062b29511abe0044b4387e`。Windows/Linux 与实机打印尚未验收。

### 1. 准备与启动

先保存并正常退出当前项目应用，避免同时开旧实例。以下命令在同一个终端执行；工作目录、变量和产物目录只用于本次复验，重复验证时重新执行 `mktemp`，不要删除或复用旧结果目录。

```sh
cd /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft
recon_bin="/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer"
recon_run=$(mktemp -d "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/manual-reconstruction-XXXXXX")
printf '本次结果目录：%s\n' "$recon_run"
set -o pipefail
"$recon_bin" --debug 3 2>&1 | tee "$recon_run/gui.log"
```

启动命令会一直运行到退出 GUI；退出后同一终端继续下方 CLI 命令。也可用另一个终端，但须重新设置 `recon_bin`，并把 `recon_run` 设为刚才打印出的实际目录，不能假设变量跨终端传递。不要执行真实打印/上传；另存工程和 G-code 到该目录。

### 2. 用哪些模型

仓库根目录为 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/`；下表仓库相对路径均以此为前缀。

| 输入 | 作用和预期 |
| --- | --- |
| `planning/repair/reconstruction-gui-20261007-mkXJqu/missing-top-original.3mf` | 本机首选 GUI 基准，已经保存机型/耗材/工艺：缺顶面的 10×10×10 mm 方块、10 面、一个普通对象/实例/盘。不是任意机器都有的仓库资源。 |
| `tests/data/model_repair_acceptance/01-missing-top.stl` | 上述缺面模型的仓库夹具；CLI 使用它。在其他机器可新建 GUI 工程导入，但须自行记录导入后模型及有效配置，不能假设导入没有基础清理。 |
| `tests/data/model_repair_acceptance/08-intersecting-shells.stl` | 两个相交壳体，作为另一类缺陷。新建独立工程，只导入这一件，不拆成多个对象/体积。允许生成近似合并候选，不能要求仍保留原两壳。 |
| `tests/data/model_repair_acceptance/10-only-triangle.stl` | 可选反例：单三角面可被包成薄体，网格检查通过仍可能低于首层高度，不能据此认为能打印。 |

首选 3MF 保持现有有效配置：EP7 0.4 nozzle、QIDI PLA Rapido @EP7 0.4 nozzle、0.20mm Standard @EP7，层高/首层均 0.2 mm、classic 墙、支撑关闭、无后处理。不要切换预设或修改对象覆盖；若本机缺预设而弹窗要求替换，先停止并回传截图，不能随意替换后再比较耗材/路径。新建的双壳工程采用相同配置，默认尺寸/100% 缩放、无镜像、不绘制、不添加修改器，保存其基准工程。GUI 可将导入件自动居中/落床，CLI 原 STL 不含此摆放，比较几何和风险语义，不要求输出顶点字节相同。

### 3. GUI 逐步操作

| 场景 | 操作 | 应看到什么/保存什么 |
| --- | --- | --- |
| 默认修复（必做） | 打开上述原始 3MF→“准备”→选中模型→画布右键“重建修复”（不是旧“修复模型”）。保持细节半径 0.5、表面偏移 0.05、偏差告警 0.25 mm，勾选“包裹前尝试闭合开放边界”。先观察风险确认未勾时，再勾“我理解…”→生成候选。 | 未确认时生成禁用；生成后显示完整网格检测通过、已闭合边界及需检查新增表面。原来顶面开口会被封上，尺寸约 10→10.1 mm，超偏差提示是允许的，不是修复失败。截参数和预览窗口。 |
| 采用/切片（必做） | 保持 Z 落床未勾→采用→另存 `gui-closed.3mf`→重新切片。预览拖动层滑块看首层、中间层、顶层→导出 `gui-closed.gcode`。 | 提示仍需试切片；约 50 层，能看到完整墙体、填充和顶底面，不只是几个角落的碎线。不能因为“切片完成”就跳过路径检查。 |
| 状态恢复（必做） | 回准备页，用“编辑→撤销”恢复原件，另存 `gui-undo.3mf`；“编辑→重做”恢复候选，另存 `gui-redo.3mf`。正常退出软件后重开 redo 工程，再切片、导出 `gui-reopened.gcode`。 | 撤销恢复缺面方块；重做恢复闭合候选。重开无闪退、无明显移动/变形、配置未换，主体路径仍完整。不覆盖原始工程。 |
| 关闭选项对照（建议） | 再打开原始 3MF，参数相同，只取消“包裹前尝试闭合开放边界”，生成。为诊断可采用，另存 `gui-surface.3mf` 并切片导出 `gui-surface.gcode`，不要打印。 | 必须出现开放输入可能包成薄壳/缺墙路径提示。本样例七项虽通过，但切片仅有少量碎外墙、没有正常填充/顶底面，可能报悬空；这是关闭优化后的预期反例，不能当健康可打印模型。 |
| 相交壳体（建议） | 新建独立工程，导入 `08-intersecting-shells.stl`，保持同配置及默认重建参数。生成后检查合并形状；可勾 Z 落床后采用，另存 `gui-shells.3mf` 并切片导出 `gui-shells.gcode`。 | 候选完整检测通过；可能合并邻近表面、出现偏差告警，但不该崩溃或误说结构完全保留。明确落床后最低点为平台，逐层应有主体路径。实际层数按当前摆放/配置记录，不用固定数字硬判。 |
| 原件保留（建议） | 在闭合候选工程上再打开重建，先点参数页取消；再生成一次，在预览选“保留原模型”。另存 `gui-kept.3mf`。 | 参数取消与放弃候选均不改变当前模型；重复生成会给出更大候选，但没有采用就不应累积扩大原模型。 |
| 运算取消（补验） | 在闭合候选的副本上尝试 alpha 0.01、offset 0.001、告警 0.25，生成后立即取消。若进度窗无文字按钮，用 macOS 进度窗关闭控件。 | 明确“已取消”、原件保留、无遗留 worker。若先报内存超限或已经完成，则只记相应结果，不当作取消通过；不反复加压、不修改生产预设。 |

英文/暗色/缩窗可按同一清单查看新选项、警告和按钮是否完整；如临时切换界面设置，记录并恢复。它们不影响上述数学验收，但仍属于待补 GUI 覆盖。

### 4. 命令行：生成候选、关闭对照、独立复检

以下不是旧 `--repair-model cgal`，也不是给 `--check-model` 加高级修复。新入口是 `--reconstruct-model`，目前只接收一个无色 STL；单位为 mm，不直接接收 3MF/OBJ。`--accept-reconstruction-risk` 只是允许生成近似候选，不代表确认形状或可打印。每个 `--outputdir` 必须尚不存在，父目录必须存在，因此不要预先创建 `closed`/`surface`/`shells` 子目录。

在上述 GUI 退出后，同一终端执行：

```sh
"$recon_bin" --reconstruct-model \
  "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/tests/data/model_repair_acceptance/01-missing-top.stl" \
  --outputdir "$recon_run/closed" --accept-reconstruction-risk \
  --reconstruction-alpha 0.5 --reconstruction-offset 0.05 \
  --reconstruction-max-deviation 0.25 --reconstruction-timeout 60 \
  > "$recon_run/closed.log" 2>&1
printf '默认重建退出码：%s\n' "$?"

"$recon_bin" --reconstruct-model \
  "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/tests/data/model_repair_acceptance/01-missing-top.stl" \
  --outputdir "$recon_run/surface" --accept-reconstruction-risk \
  --reconstruction-alpha 0.5 --reconstruction-offset 0.05 \
  --reconstruction-max-deviation 0.25 --reconstruction-timeout 60 \
  --reconstruction-no-close-open-boundaries \
  > "$recon_run/surface.log" 2>&1
printf '关闭预闭合退出码：%s\n' "$?"

"$recon_bin" --reconstruct-model \
  "/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/tests/data/model_repair_acceptance/08-intersecting-shells.stl" \
  --outputdir "$recon_run/shells" --accept-reconstruction-risk \
  --reconstruction-alpha 0.5 --reconstruction-offset 0.05 \
  --reconstruction-max-deviation 0.25 --reconstruction-timeout 60 \
  > "$recon_run/shells.log" 2>&1
printf '相交壳重建退出码：%s\n' "$?"

"$recon_bin" --check-model --outputdir "$recon_run/closed-check" \
  "$recon_run/closed/candidate.stl" > "$recon_run/closed-check.log" 2>&1
printf '候选独立检测退出码：%s\n' "$?"
python3 -m json.tool "$recon_run/closed/reconstruction.json"
python3 -m json.tool "$recon_run/closed-check/result.json"
```

重建预期退出 0，生成 `candidate.stl` 与 `reconstruction.json`；报告应有 `status=candidate_ready`、`mesh_validated=true`、`export_round_trip=passed`。默认缺面样例的 `engine.boundary_closure.used_for_wrapping=true`，关闭对照为 false 且 `engine.metrics.open_surface_envelope_warning=true`。独立检测应 `check_complete=true`、七项各自 complete=true/count=0。**两种候选都可能七项为零，区别须结合上面的真实切片路径。**

`shape_confirmation=not_confirmed`、`trial_slice=not_run`、`print_ready=false` 都是预期保护状态，不是命令失败。CLI 本身不自动切片，也不接受打印预设参数；需要打印验证时将候选导入隔离 GUI 工程、保持记录的有效配置后切片。以上检查的 result.json 只在本次 `closed-check` 目录生成，不要加到仓库。重建非零退出/缺候选时停止后续复检，将对应日志和报告回传，不手改报告。

可选自动跑完整 27 项 CLI 回归（输出目录也必须是新的）：

```sh
python3 scripts/test_cli_model_reconstruction.py \
  --binary "$recon_bin" --output-dir "$recon_run/cli-regression" \
  2>&1 | tee "$recon_run/cli-regression.log"
```

该脚本不构建、不切片、不打印；预期 27 passed、0 failed、0 skipped。与 GUI 共用几何规则，但不能代替 GUI 确认、Undo/Redo 和实际路径检查。

交接命令已于 10-07 提交前用同一包实际试跑，证据 `planning/repair/reconstruction-handoff-jbeaKn`：默认/关闭/相交壳三件均 candidate_ready、mesh_validated=true、export_round_trip=passed、退出 0；默认预闭合为 true，关闭对照为 false 且薄壳告警为 true；默认候选独立七项完整为零、退出 0。这是命令可执行性补验，不与此前 27 项回归相加统计。

### 5. 回传什么

完成后告诉我终端打印出的 `manual-reconstruction-XXXXXX` 完整路径及测试时间段即可，我可直接读取同一电脑上的日志/产物。请保留该目录中的 `gui.log`、各个 GUI 3MF/G-code、CLI 日志/报告/候选及参数/预览/异常截图；说明哪些步骤实际执行、是否改过配置。当前这是可选人工复验，已通过的软件验收项见下节；不要求先实机打印才能提交，也不将入库等同跨平台/实机发布通过。

## 10-07 开放边界优化后的交接（当前）

默认增加“包裹前尝试闭合开放边界”，GUI/CLI 共用；用于解决缺面方块被包成薄壳的问题。用户重新打包后，**默认/关闭对照、风险提示、采用、菜单撤销/重做、保存退出重开及实际 GUI 切片已完成**，结果见[主记录](2026-10-04-smart-print-engine.md)最新节。未验项仍单独保留，不沿用下方旧版 GUI 结论。

常用 `build/arm64/OrcaSlicer/OrcaSlicer.app` 与直接应用 SHA-256 均为 `89bed37fcafd3ed31abf6494629a569e60ea3722ad062b29511abe0044b4387e`，简中 MO 相同，无需再次构建。CUA 以 PID 8127 实操并正常退出，再以 PID 8453 跨进程重开。此前 AppKit 启动阻塞解除；中途系统文件选择框的自动交互卡住，用户关闭弹层后改用最近工程继续。独立 `/Applications` 版本未操作。

继续剩余验证时复用当前实例，或在正常退出后启动以下已核实的打包路径，不同时重复启动。当前自动化证据根为 `planning/repair/reconstruction-boundary-postbuild-VtFcVD`；两次应用日志身份及 hash 见主记录。

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-boundary-gui-manual.log
```

基准工程：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-gui-20261007-mkXJqu/missing-top-original.3mf`。每组从该原件重新打开，只另存新工程，不覆盖原件。保持其 EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard，有效层高及首层 0.2 mm、classic 墙、支撑关闭；不因预设名字相同就忽略对象/工程覆盖。重建参数 alpha 0.5 mm、offset 0.05 mm、偏差告警 0.25 mm。

| 场景 | 操作与预期 | 本轮 GUI 状态 |
| --- | --- | --- |
| 默认预闭合 | 选中缺顶面方块→重建修复。新开关默认勾选，解释可能填上有意开口；勾风险确认后生成。预览应报告已加入封口面，完整检查通过，但仍需试切片/结构人工确认。 | 通过：确认前生成禁用，封口/偏差/待切片提示正确；10 面→3648 面，七项独立复检完整为零 |
| 默认结果切片 | 明确采用后保持原摆放切片；另存 `boundary-closed.3mf`，导出 `boundary-closed.gcode`。 | 通过：实际 GUI 50 层墙/填充/顶底面；脚本逐层无空层/非法数值。未逐层人工点查全部截面，未实机打印 |
| 关闭对照 | 从最近工程打开 `boundary-undo-confirmed.3mf`（网格与原件完全相同），取消新开关，其余参数不变生成；仅另存 `boundary-surface.3mf` / `.gcode`。 | 通过：薄壳提示正确，11302 面/60.70210 mm³，虽七项为零但只有碎外墙且导出悬空警告；与默认结果 565 项 G-code 配置一致，不判为可打印 |
| 原件与撤销 | 默认结果采用后菜单撤销恢复缺面原件、重做恢复实体；保存新工程，正常退出重开再切片。 | 通过：撤销/重做原始网格 hash 对应一致，跨进程重开世界坐标最大差约 5.10×10^-8 mm，配置不变。快捷键尝试未实际撤销，`boundary-undo.3mf` 排除；正式证据是 `boundary-undo-confirmed.3mf` |
| 闭合件/保留 | 在独立检查通过的 `boundary-reopened.3mf` 上默认再生成，检查无不适用的封口/薄壳提示；选择保留，不采用再次扩张候选。 | 通过：未误报封口/薄壳；另存 `boundary-kept.3mf` 的网格/实例变换/配置与输入一致。关闭选项可用，但不能自动识别有意开口 |
| 布局与取消 | 查看中文/英文新开关、说明、预览提示，不裁切操作按钮；生成过程取消后原件保留且 worker 回收。 | 中文当前 Retina 窗口可读、两类新警告已验；英文/暗色/缩窗/当前版运算中取消待验，不能用优化前测试替代 |

本轮新包 CLI 27 项与真实 worker 1 用例/22 断言通过，审计脚本和实际产物均在上述证据根。软件停留在已保存的 `boundary-kept` 工程。后续仅补剩余项，回传启动日志、参数/结果截图、新 3MF 和 G-code（失败则错误截图/日志）、操作时间；其余旧清单中仍适用的未验项继续保留，Windows/Linux 和实机另验。本轮只验证/更新记录，未改业务代码、未提交；网格完整七项通过不等于所有模型可打印。

## 10-07 新包复验结果（预闭合优化前历史）

用户关闭旧项目实例后，CUA 在同一打包版（SHA-256 `5758588c2673568579637ba1dd685ffe9c8d87120848c70108ceb95888c13a2f`）启动 PID 712，正常退出后以 PID 1234 重开成功；10-06 的辅助功能连接阻塞已解除。本轮不重复构建、不改业务代码、不提交，未操作独立的 `/Applications` 版。

| 验证项 | 当前结果 |
| --- | --- |
| 大开口/相交双壳默认候选 | 11302/4810 面，实际保存网格提取原始 OBJ 独立复检，七项完整为零；超偏差警告、有效参数和结构变化风险正确显示 |
| 风险确认/非法参数 | 未确认时生成禁用；alpha 0.5 / offset 0.2 关系拦截通过；非正值未单独 GUI 测试 |
| 保留/采用/落床 | 保留后再生成不累积改变；默认采用保持摆放，双壳显式落床后世界最低 Z=0 |
| Undo/Redo/退出重开 | 撤销恢复原件，Cmd+Y 重做恢复候选；真正退出重开后拓扑不变，世界坐标最大差约 2×10^-8 mm；本轮对象列表反复读取未再闪退 |
| 内存超限/取消 | 分别实测两种结果，均原件保留、worker 回收。Mac 进度窗关闭控件可取消，但未见独立文字取消按钮 |
| 实际试切片 | 双壳 51 层墙/填充/顶底面正常；大开口 50 层仅碎外墙并有悬空警告，不能判为可打印。全部 G-code 层做路径审核，未逐层人工点查全部 GUI 截面 |
| 显示和剩余项 | 中文/亮色/当前 Retina 可读；健康件 0.001 mm 告警、英文/暗色/缩窗/Tab、缩放镜像、绘制/附属结构拒绝、完整对象列表菜单/编辑等未在本轮重验 |

GUI 两件均使用 EP7 0.4 nozzle / QIDI PLA Rapido @EP7 0.4 nozzle / 0.20mm Standard @EP7，层高及首层 0.2 mm、classic 墙生成、支撑关闭、无后处理；与下方历史 CLI 的 EP3 Plus 不冒充同参。完整有效配置和产物在 `planning/repair/reconstruction-gui-20261007-mkXJqu`，详细身份、误操作排除及量化结果见[主记录](2026-10-04-smart-print-engine.md)10-07 节。

**本节记录的是优化前大开口被包成薄壳、主体路径不足的问题；后续默认预闭合的改善以上方当前节为准。**不能用“所有层非空”替代有效形状验收。无实机打印，Windows/Linux 仍只有源码/配置检查，未运行通过；其余未验项继续按适用清单补测。

## 10-06 缺陷优先优化后的清单（历史）

新版尚未执行 GUI 验证；下面原始清单及 17:22 结果是旧保形重建历史，不代表当前新版通过。macOS arm64 已正式升级项目 GMP 至 6.3.0，并重新构建应用/测试；200 次连续重建及核心/CLI 回归通过，21 件实际输出七项完整为零，其中包含先前高级修复失败的 8 件。已复现的旧 GMP 压力崩溃本轮未再出现；额外凸凹角度用例有升级前同样存在的失败，不称全量通过。**仍非完整发布验收**，证据见[主记录](2026-10-04-smart-print-engine.md)最新 GMP 节，现在可按下面清单复验 GUI。

10-06 23:13 用户重新打包后，`build/arm64/OrcaSlicer/OrcaSlicer.app` 与直接产物 `build/arm64/src/Release/OrcaSlicer.app` 的可执行文件 SHA-256 均为 `5758588c2673568579637ba1dd685ffe9c8d87120848c70108ceb95888c13a2f`，包含正式 GMP 6.3.0。打包版 CLI 26 项和真实 worker 集成已通过，无需再次构建。CUA 获取新窗口仍返回辅助功能错误/超时，不能算 GUI 通过；当前还有 17:08 启动的旧项目实例及 `/Applications` 版同时存在。连接打包路径时已启动新实例 PID 84168（23:16），但未成功操作其窗口。先保存工程、正常关闭旧实例，保留/重开下面同一包的新实例再验证，不强制丢弃当前工程。启动命令：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-postbuild-gui.log
```

以仓库绝对根目录 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/` 加下列路径定位模型：`tests/data/model_repair_acceptance/01-missing-top.stl`、`08-intersecting-shells.stl`、`09-nonmanifold-edge.stl`、`10-only-triangle.stl`；后三项也在同一个 `tests/data/model_repair_acceptance/` 目录。每次只导入一件，单实例、普通体积；记录导入后的几何，不能假定 GUI 原生导入没有清理。折叠自交源为 `planning/repair/reconstruction-20261005-cli-final/fixtures/folded_tube.stl`。

| 操作 | 新版预期 | 状态 |
| --- | --- | --- |
| 分别对大开口、相交双壳运行“重建修复” | 默认 alpha 0.5／offset 0.05／告警 0.25 mm，不再因大开口／多壳直接拒绝；得到完整检测通过候选，但提示形状／孔／内腔可变化、仍需试切片 | 待 GUI 验证 |
| 用健康方块将偏差告警设 0.001 mm | 候选允许生成，显示超出告警阈值，不能再显示“不得超过上限”或将告警当失败；显示实际有效 alpha／offset | 待 GUI 验证 |
| alpha 0.5、offset 0.2；或输入非法非正值 | offset/alpha 关系或有限正值防护拒绝，不启动 worker，原件不变 | 待 GUI 验证 |
| 生成后“保留原模型”，再生成并明确采用 | 放弃不改原件，采用才替换；默认保持摆放，只勾 Z 落床才移动 Z | 待 GUI 验证 |
| 采用后 Undo/Redo，另存新 3MF，退出应用重开 | 几何和变换按历史恢复，源 STL/旧工程不覆盖，列表查询不自动落床或闪退 | 待 GUI 验证 |
| 对较大网格在进度窗口取消 | worker 终止、回收，原件保留；若已结束须如实记录，不能当取消已验 | 待 GUI 验证 |
| 新版候选按同一有效配置切片并逐层检查 | 网格通过不消除悬空、首层无路径、极薄体风险；出现这些情况必须保留警告／失败，不报可打印 | 待 GUI 验证 |
| 中英文、亮暗主题、缩窗与键盘操作 | 新告警、完整检查及有效参数文案可读，确认/取消可见，不误译为形状保持保证 | 待 GUI 验证 |

切片对照配置：本轮 CLI 使用隔离 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/` 中三份 `*_settings_1.config`，EP3 Plus／0.4 mm、101×101 mm 平台、105 mm 高、层高 0.2／首层 0.24 mm，支撑关闭。GUI 若采用其他设备，需回传完整工程有效配置，不通过同名预设声称同参。上述 09 和折叠样例在此配置下仍无首层有效路径，单三角形候选约 0.1 mm 厚不能按 0.24 mm 首层切片；这是预期需暴露的可打印性边界，不是七项检查残留。

回传启动日志、重建参数／告警／结果截图、原件和采用后的隔离 3MF、实际导出 G-code（失败则错误截图／日志）、操作时间及设备／材料／工艺与对象覆盖。自动化、CLI、GUI、跨平台、实机分别记录；本轮无需实机打印。没有自动提交或推送。

## 当前范围与身份

这是[模型修复主记录](2026-10-04-smart-print-engine.md)的 GUI 操作附件，不另建执行台账。实现决定、编译／测试命令、二进制 hash 和实际结果统一回填主记录。10-06 原打包版发生对象列表辅助功能回调闪退；随后已修复并完成该问题的局部回归，完整重建 GUI 清单仍非全部通过。以下原始清单保留当时覆盖，后续修复状态见“闪退修复专项复验”，未执行项不因核心测试通过而升级。

- 仓库：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft`，分支 `master`，基准 HEAD `34477d7411f076f4c9fe9200fcfc69c9253b0293` 加本轮未提交重建修改。HEAD 本身不能证明应用含新代码。
- 构建：`build/arm64`、arm64 Release。10-06 用户已更新打包副本，实际操作 `build/arm64/OrcaSlicer/OrcaSlicer.app`；它与 `build/arm64/src/Release/OrcaSlicer.app` 可执行文件 SHA-256 同为 `0cd1f21345729f8a00be4f7eab548fff69dcd5690b5693010f0323b6d5d90191`，简中资源也匹配。具体运行日志与崩溃报告见主记录 10-06 节；没有读取到用户所报的同日构建日志，不能称其构建日志审阅通过。
- 后续修复版：直接产物 `build/arm64/src/Release/OrcaSlicer.app` 的 SHA-256 为 `b3dc2aa1457f47b84a269c998399b4b2c3d2a0833cdec9bd2cea704eb459fcd3`，以下专项复验使用此版本；17:22 用户重新打包后，`build/arm64/OrcaSlicer/` 副本也已是同一 SHA，并完成下述打包版复验。上条 `0cd1f213...` 仅为修复前历史身份。
- GUI：独立“重建修复 / Reconstruct model”，旧“修复模型 / Fix model”行为不变。初版只支持单普通体积、单实例、无面级绘制或附属结构的 FFF 对象；健康对象也能主动尝试。
- 证据边界：可以生成和明确采用近似候选，但独立同有效配置诊断试切片尚未实现。界面必须显示试切片待执行，CLI 保持 `trial_slice=not_run`、`print_ready=false`。此附件不是完整商用／跨平台／实机打印验收，不能据此宣称已经恢复原设计或可上线。

## 启动与输入准备

先保存当前用户工程并正常关闭现有应用，不覆盖用户预设。确认主记录的构建身份后，使用本轮直接产物启动并保存控制台日志；`planning/repair` 是仓库已有的忽略证据目录。

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-gui-20261005.log
```

在新工程中导入以下健康基准，一次只放一个对象、一个实例、一个模型部件；不要在既有用户工程上直接做边界／拒绝实验。

| 输入 | 身份／用途 |
| --- | --- |
| [00-healthy-cube.stl](../../../tests/data/model_repair_acceptance/00-healthy-cube.stl) | 10 × 10 × 10 mm 健康方块；SHA-256 `8618fc713878420a4e66bf21b8c4ee271ccc91eaf19b0cfbd5b821f8564b1065`。使用绝对路径 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/tests/data/model_repair_acceptance/00-healthy-cube.stl` 导入。 |
| [cube_with_hole.obj](../../../tests/data/cube_with_hole.obj) | 孔洞边界样例；SHA-256 `a938b25260e5f52c8ead83ef95cce980f775453173593e96bae66b3b7178bd20`。用于观察“不支持”拒绝，不能把旧修复样例预期套到新重建。 |
| [01-missing-top.stl](../../../tests/data/model_repair_acceptance/01-missing-top.stl) | 大开口拒绝；这是开放顶面的 10 mm 方块，默认预算不能把它当受限小开口直接封闭。 |
| [08-intersecting-shells.stl](../../../tests/data/model_repair_acceptance/08-intersecting-shells.stl) | 两个相交但拓扑分离的壳体，预期不支持多壳，不能拿它证明单壳自相交候选提示已验证。 |
| `planning/repair/reconstruction-20261005-cli-final/fixtures/folded_tube.stl` | 本轮 CLI 脚本生成的人工单壳自交样例，SHA-256 `3b903d8e64c30b28bc44a537a5ab00eef66555173e0e426d5786a5647618e5f6`。CLI 参数 alpha `0.25`、offset `0.025`、偏差 `1.0 mm` 已产生待确认候选；仅供界面风险提示验证，参数不能推广到真实模型。完整路径为 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-20261005-cli-final/fixtures/folded_tube.stl`。 |

导入健康方块后将尺寸确认成 10 × 10 × 10 mm、缩放 100%、旋转 0°、底面贴床，记录放置的 X/Y 和打印板序号。另存为隔离基准工程，例如 `planning/repair/reconstruction-gui-baseline.3mf`；后续每个拒绝实验从该基准重新打开，生成结果另存新名字。

有效配置沿用一套已经能正常切片的当前 FFF 配置。操作前记录：打印机／喷嘴直径／平台、耗材、工艺名称及实际层高、全局／打印板／对象覆盖、界面语言。不要为了运行清单重置预设；配置未记录完整时只能核对几何候选，不能声称完成同参切片对照。

## 操作清单

默认重建参数：包裹细节半径 `0.5 mm`、表面偏移 `0.05 mm`、采样表面偏差上限 `0.25 mm`。资源默认 60 秒／1024 MiB；macOS 内存是 RSS watchdog 采样预算，不能理解为进程绝不会瞬间超过此值。

| 场景 | 具体步骤 | 预期结果／证据 | GUI 状态 |
| --- | --- | --- | --- |
| 两个菜单入口与健康件主动尝试 | 在“准备”页选择健康方块，分别从对象列表右键、画布右键打开“重建修复”；每次取消退出。 | 两处是独立入口，不依赖模型已有警告；原“修复模型”项仍存在。选择 SLA、预览页或打开编辑工具时不能误进入重建。截图两个入口。 | 部分通过：画布入口及旧菜单已见；对象列表出现异常，不能记两处通过；其他拒绝入口未验 |
| 风险确认和参数取消 | 打开参数窗口，先不勾风险确认，尝试生成；勾选后点击取消。 | 加粗显示“实验性近似候选”及自动试切片／实际收益／跨平台发布验收未完成；未确认前“生成候选”禁用；说明明确近似外形、可能封孔／填腔／失去细节。取消不改变对象尺寸、位置、部件和绘制，不增加“采用重建模型”撤销项。 | 部分通过：风险提示与未勾选禁用已验；参数窗口取消未单独验证 |
| 非法参数 | alpha 设 `0.5`，offset 设 `0.2`，偏差上限 `0.25`，勾确认并点击生成；随后取消。 | 提示 offset 不得超过 alpha 的四分之一，不启动重建；原件不变。恢复默认参数。 | 拦截通过；实际随后恢复默认并继续生成，没有取消整个窗口 |
| 候选图形与数值 | 用默认参数生成健康方块候选。 | 顶部仍有加粗的实验性提示。显示真实 XY/XZ/YZ 中截面；紫色虚线原件、青绿色实线候选。原件尺寸应为 10 mm 各轴；候选尺寸、底面高度／ΔZ 和偏差为有限实际数值。双向采样距离不大于指定上限，界面明确其不是保证最大误差；不得把未知指标显示为零。保存整个预览截图。 | 实操通过：10 → 约 10.100 mm，底面 0 → -0.05 mm，偏差约 0.05000/0.05043 mm；截图由工具返回，未另存文件 |
| 放弃候选 | 在对比窗口点击“保留原模型”。 | 回到场景后仍为原件，位置／尺寸／属性未变，无新采用撤销项。重复生成、放弃两次，不能累积菜单项或遗留进度窗口。 | 部分通过：放弃一次后再次生成仍为 10 mm 原件；未验证连续放弃两次及完整撤销栈 |
| 保留摆放的采用 | 再生成，保持“沿 Z 轴移动候选”未勾，点击采用。 | 只替换该对象网格，X/Y/Z 实例变换不被自动修正；允许候选扩张后底面略低于平台，界面已报告此变化。提示试切片仍待执行，不显示“已可打印”，不自动打印。 | 方块采用与落盘通过：实例 (90,90,5) 保持，底面约 -0.05 mm，待切片提示正确；不覆盖后续对象列表异常 |
| Undo／Redo、对象切换 | 采用后执行一次撤销，再重做；选择空白处、该对象和设置行，切换打印板再回来。 | 撤销恢复原网格／尺寸／位置，重做恢复候选；选择和设置面板不引用已释放的体积，不崩溃、不错选其他对象。记录两次状态和截图。 | 未完整通过：方块和自交样例撤销/重做几何正常；后续对象列表显示异常，再次开工程触发闪退；其余切换待复验 |
| 显式落床 | 重新打开基准，生成候选；记录候选底面高度，勾选 Z 落床后采用。 | 只在此选择下移动 Z，使最终最低点约为 0；X/Y、旋转、缩放不变。撤销同时恢复原几何和原摆放。仍需重新切片，不把落床等同可打印。 | 人工自交样例通过：保存网格最低 Z=-0.624504924，加实例 Z=0.624504924 后为 0；X/Y 与旋转缩放不变；撤销/重做已走通 |
| 缩放／镜像毫米语义 | 基准对象取消等比锁定，将 X/Y/Z 缩放设为 `200%/100%/50%`，再按需做一次 X 镜像；关闭工具后运行默认毫米参数。 | 候选比较基于当前物理尺寸，不能把局部坐标阈值直接当 mm 或二次缩放；若精度／尺度守卫拒绝，原件和变换不变且原因明确。逆变换完整性由核心专项另验；CLI 首版不接受这些变换参数，不能声称已完成此项 CLI 对照。 | 待执行 |
| 无损拒绝属性 | 从基准分别做一次面级颜色绘制、支撑绘制、接缝／绒毛绘制，或右键“添加修改器”增加立方体，或添加高度范围／变层高；每次只加一种，关闭工具再点重建。 | 明确不支持对应状态；不自动清除绘制、层设置或体积后继续。取消提示后状态仍在；随后重新打开基准做下一项。 | 待执行 |
| 多实例与拓扑边界 | 对基准生成同对象的第二个实例后尝试；另开工程分别导入大开口、带孔、相交双壳样例。 | 多实例／多壳／孔洞／大开口按实际共享守卫拒绝，不能静默合壳、删部件或填腔；每次失败原件不变。不要把“克隆成两个独立对象”误当同对象多实例。 | 部分通过：大开口默认拒绝；多实例/多壳/孔洞未 GUI 实操 |
| 单壳自交的未知内部提示 | 单独导入上表 folded_tube，以 alpha `0.25`、offset `0.025`、偏差 `1.0 mm` 生成，最后保留原件退出。 | 候选窗口必须加粗说明原件自交、内部实体范围尚未验证，不能显示已恢复原设计或可打印。若 GUI 导入步骤已经改变网格，记录实际网格和拒绝原因，不把 CLI 原始输入直接等同当前 GUI 几何。 | 提示通过：导入选择不换算尺寸，源 3×1×1.2 mm/28 面，明确内部未验证；实际采用后以 Undo/Redo 验证，不是保留原件退出 |
| 运算中取消 | 对健康方块设 alpha `0.05`、offset `0.01`、偏差 `0.25`，生成后立即点进度窗口取消。 | 窗口可响应，worker 被终止并回收，原件不变，不出现可采用候选。如果预算拒绝或运算已结束，记录实际状态，不能冒称本次测试覆盖取消；硬超时／故障回收另看进程专项。 | 待执行 |
| 保存后重开 | 默认参数采用后另存 `planning/repair/reconstruction-gui-adopted.3mf`，正常关闭并重开。 | 候选网格、对象／体积配置、单色材料、实例摆放一致；旧基准工程和原 STL 未被覆盖。备份排队／磁盘内容由 AI 复核；正常重开不能冒充崩溃恢复已验收。 | 未通过完整验收：第一次同进程重开成功；对象列表显示后第二次开同一工程发生闪退。未覆盖正常退出应用再启动或恢复备份 |
| 首次实际试切片 | 对已采用副本使用上面记录的同一 FFF 有效配置，明确点击“切片单盘”，打开预览，从首层到顶层查看外壁、实心区域、孔槽及空层，再导出新的 G-code.3mf。 | 切片必须重新计算；记录成功／错误、层数、警告、底面／平台越界及可见异常。非空路径只是诊断证据，不证明原设计恢复。此步是在采用后另做手测，不改变首版候选报告 `trial_slice=not_run` 的含义。 | 方块基础路径通过：实际导出 .gcode，50 层均有正挤出运动，无空挤出层/NaN/Inf；未导出 G-code.3mf 或验收复杂孔槽及实机 |
| 中英文与显示 | 保留隔离工程，分别在简体中文／英文及亮／暗主题下打开参数和候选窗口，使用当前高 DPI 显示器缩小窗口并滚动正文。 | 初始窗口在当前显示器工作区内；正文可滚动，底部确认/取消始终可见，Tab 键可进入正文和按钮。文案、毫米单位、颜色图例和自交提示可读，中英文含义一致。记录操作系统、屏幕缩放和截图。 | 部分通过：中文/亮色/当前 Retina 尺寸可读、底部按钮可见；取消按钮仍为英文 Cancel。英文/暗色/缩窗/Tab 未验 |

过期候选、worker 异常输出／磁盘故障和实际崩溃恢复不建议通过破坏正在使用的工程制造。核心／进程测试覆盖其可自动化部分；GUI 正常模态窗口阻止常规同时编辑，真实异步改动／Undo 竞态和恢复文件一致性仍需专门受控故障注入，当前保留待验证。单壳自交提示使用上述 folded_tube 单独验证，不能以相交双壳拒绝结果代替。

## 对应 CLI 与自动化

下面命令是同输入、同重建参数的候选对照，不包含机器／耗材／工艺或诊断试切片。输出目录必须尚不存在；若已存在，使用新的同级目录并将实际名字记录在主记录，不能覆盖旧证据。

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --reconstruct-model /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/tests/data/model_repair_acceptance/00-healthy-cube.stl --outputdir /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/repair/reconstruction-gui-baseline-20261005 --accept-reconstruction-risk --reconstruction-alpha 0.5 --reconstruction-offset 0.05 --reconstruction-max-deviation 0.25 --reconstruction-timeout 60
```

核对 `reconstruction.json` 与实际 `candidate.stl`，报告应为独立 `model_reconstruction_v1`；候选成功不表示人工确认或采用，`shape_confirmation=not_confirmed`、`applied=false`、`trial_slice=not_run`、`print_ready=false`。源模型 SHA-256 不变，不应产生旧 `result.json`。比较 GUI/CLI 时分清 GUI 导入后的居中／放置变换：表面及尺寸指标在物理毫米空间比较，不能要求世界坐标和 STL 文件逐字相同。

完整 CLI 专项脚本为 `scripts/test_cli_model_reconstruction.py --binary <同一应用可执行文件> --output-dir <全新证据目录>`；C++ 筛选为 `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess]`。这些用例的实际命令、次数、断言、退出码及原始日志只以[主记录](2026-10-04-smart-print-engine.md)为准；本附件不复制运行次数，也不将尚未完成的构建或测试计为通过。

## 2026-10-06 闪退修复专项复验

用户随后授权修改，实际构建/日志/GUI 证据统一见主记录。此次只改列表查询副作用、非法落床目标和参数取消翻译，不改重建算法。

1. 用修复版打开 `planning/repair/reconstruction-gui-20261006-adopted.3mf`，展开对象列表，读取包含盘、模型、盘外行的辅助功能状态。应正常显示名称，不自动开启编辑器、不切换打印状态、不自动落床：已通过。
2. 双击名称开始编辑后 Escape 取消；单击打印图标关闭/恢复；单击橙色落床图标，再 Undo/Redo。应各自只在实际操作后生效：已通过。另存 `planning/repair/object-list-regression-20261006.3mf` 核对最低世界 Z=0、网格面数不变；原文件未覆盖。
3. 保持对象列表可见，打开 `reconstruction-gui-20261006-folded-on-bed.3mf`，再打开上述方块工程。辅助功能读取不得产生落床撤销项或闪退：已通过；方块沉床提示仍保留，未被只读查询修改。
4. 从画布右键进入重建参数窗口，确认按钮显示“取消”，点击后返回原件且标题无未保存标记：已通过。对象列表右键受当前自动化鼠标定位影响未完成入口确认，不据此升级该项。

专项修复通过不覆盖工作线程运算中取消、多选/所有绘制图标、跨平台、实机或真实模型收益；原始清单中未覆盖项仍待验证。程序当前停留在隔离方块测试工程，未启动打印或上传。

### 17:22 用户重新打包后的复验

实际操作 `build/arm64/OrcaSlicer/OrcaSlicer.app`，PID 12945，日志 `planning/debug/orca-user-gui-run-20261006-172220-530984.log`。完整身份、数值及产物校验见主记录最新复验节；本次未改源码或重新构建。

- 上述列表读取、打印开关、落床/Undo/Redo、方块→自交工程→方块重开、中文取消均再次通过；补验了非模型盘行空落床列点击和改名输入回车完成/撤销。
- 默认参数风险门控、候选生成、三截面显示与“保留原模型”通过。输入是已采用过的 10.100 mm 方块，本次候选约 10.200 mm；保留后仍为原尺寸/3648 面，无未保存标记。未再次采用或试切片，不重复计数此前采用验证。
- 新保存 `planning/repair/packaged-gui-regression-20261006-1722.3mf`，主动落床后最低世界 Z=0，面数不变，工程工艺配置全键无差异；原两个输入工程 SHA 不变。该文件只是隔离验证副本，不入库。
- 本次未再复现闪退，未见新崩溃报告。日志仍有可选 3D 鼠标驱动缺失、临时预设父项查找等外围警告，预设校验成功；不称零警告或完整商业验收。未覆盖的运算中取消、完整守卫、跨平台与实机等项目保留待验证。

## 回传与当前结论

回传本次 `planning/repair/reconstruction-gui-20261005.log`、操作时间范围、参数／候选／撤销前后截图、隔离基准与采用后的 3MF、实际试切片导出的 G-code.3mf，以及完整有效配置名称和覆盖项。若失败，保留原输入和独立报告，不继续自动调大偏差或偏移来争取成功。无需实机打印。

当前 GUI：此前对象列表/辅助功能回调闪退已修复，同一重开序列及相关操作通过专项复验；完整重建功能仍有未执行项，不能称全部验收通过。旧崩溃调用链是 macOS 可编辑性查询 → `ObjectList::OnEditingStarted` → `ModelObject::ensure_on_bed`，不是 worker 运算堆栈；不因此调整重建算法。

已保存方块默认采用 3MF、50 层 G-code、自交候选显式落床 3MF 及专项落床工程；路径、SHA、运行日志和崩溃报告在主记录 10-06 节。方块已有采用后手动切片证据，首版独立诊断试切片仍未实现。GUI 运算中取消、未覆盖守卫、跨平台、崩溃恢复、真实失败模型收益、商业交付与实机质量仍待验证。后续修复已改相关 GUI/平台代码，尚未提交。
