# 独立重建候选：GUI 验证交接

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
