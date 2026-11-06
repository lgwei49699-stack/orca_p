# GUI 启动卡顿、分盘与升级提示排查

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

本轮启动自动升级检查修正、构建身份及待验清单已迁入[2026-10-30 专项记录](2026-10-30-startup-update.md)。以下仅保留此前历史。

### 10-17 历史状态（下述版本和“不修改”决定不代表本轮）

- 最新CLI输出约定已完成：越界独立返回盘的XY仍居中、Z固定0，盘内仍取真实底面；这不改变90mm输入的实际悬空7.5mm或110mm输入的实际下沉-2.5mm。最新验证见[分盘记录](2026-10-17-cli-oversized-arrangement.md)，下方历次Z保留/撤销输出属于对应历史时点。

- 当前模型调整已完成：第三个立方体（此前底面-2.5mm）已另存为110×110×90mm，原中心Z=52.5mm保持，真实底面为7.5mm。当次CLI重放返回Z=7.5；按后续明确的盘外输出规则，最新JSON改为0，实际3MF仍为7.5。其他两模型及原输入未改。新输入及结果在 `planning/debug/gui-arrange-cli-height90-20261017/`。

- 此前撤销阶段（已被最新约定替代）：确认负Z来自原模型后，曾恢复所有实例实际底面Z；目前盘外已按最新要求返回Z0，盘内保留实际Z。实现及最新验证见[JSON分盘记录](2026-10-17-cli-oversized-arrangement.md)。下文历次程序身份和Z归零验证均保留为历史，不代表最新输出。

- 用户后续要求：先提供同场景 CLI 自查；核实 GUI 是否由本轮修改引入，若为原有行为则先不修改。现已准备自包含3MF副本，CLI带/不带JSON两次运行通过，并核对Git历史确认关键GUI路径源自2022/2023年；按用户决定暂不修改GUI。
- 用户反馈：从维护台启动新 App 时明显卡顿，之后在 GUI 分盘，怀疑两个模型重叠，并出现升级弹框；要求分析原因。
- 基准：本仓库 `master` / `302c2a75f575985b366ff602756f90330c38fb6b`，保留既有工作区修改。本轮先分析日志、现有 GUI 状态、自动备份和调用路径，不改变用户模型/配置或自动更新软件。
- 日志：`planning/debug/orca-user-gui-run-20261017-150550-946698.log`；首条程序日志为 15:06:37.426。当前新 App PID 42157，同时另有 `/Applications/OrcaSlicer.app` PID1811 运行；不能将两个进程的状态混用。
- 当前结果：分析完成，未修改产品代码。自动备份证明三个模型均不相交；找到 GUI 盘外对象累计位移及盘号哨兵参与位移的路径。原生 GUI 重新检查更新并采样，确认官方 2.5.0.0-alpha 弹框的长文本布局在主线程耗费数秒；用户也确认原提示为官方 2.5.0。
- 验证边界：复现的是同一进程的检查更新；最初启动时没有采样，不能精确分摊每一秒卡顿。未改模型/偏好、未下载升级、未修改 GUI 规则或 CLI JSON；测试弹框已关闭。后续修复与回归尚未执行。
- 工具：CodeGraph MCP 未暴露，`~/.local/bin/codegraph` 可用，已有索引。只读 SQLite URI 首次失败，绝对路径 immutable 只读可读；8 个目标文件哈希匹配，query/callers 可用。project_metadata 为空，未宣称全库身份已验证；callees 有同名误解析，以实际源码为准，未初始化/同步索引。

## 构建和证据身份

- App SHA256：`f5ce7da238f3b7e51f0abc2610442497cef191099290f6e7bccdff3ccf5ed137`，与用户 14:48:48 构建及此前打包验证一致；arm64，当前运行系统 macOS 26.3.1。
- 本轮证据：`build/diagnostics/gui-startup-20261017/`，含 `gui.log` 快照、`backup/`、`model-bounds.json`、`identity.json`、`update-dialog-sample.txt`、`update-dialog-ax.txt`、`update-dialog.png`。
- 备份来自 `.../T/orcaslicer_model/Sat_Oct_17/15_04_38#41681#56`，`.3mf` 修改时间 15:07:28，与最后一次分盘后的自动保存一致；外部 `.model` 自身为 ZIP，已分别解压解析顶点并组合 component/build 变换。
- 原日志含 NUL，用 `rg -a` 读取；不因普通 rg 的 binary 提示遗漏日志。没有读取/修改根 result.json。

## 时间线与卡顿定位

| 时间 | 实际证据 | 结论 |
| --- | --- | --- |
| 15:06:37.317 / .426 | sample 进程 Launch Time / 第一条程序日志 | 启动至首日志约 109ms；文件名的 15:05:50 是命令预览时间，不能当进程启动时间 |
| 15:06:39.397 | finished post_init | 首日志至主要 GUI 初始化约 1.97s |
| 15:06:40.967–41.020 | 恢复旧自动备份并更新打印空间 | 恢复的是已有立方体项目，不是复杂大网格导入 |
| 15:06:41.520–49.960 | 主线程日志间隔 8.44s，随后注册 WebView script handler | 与弹框构建相符；日志空档本身不是阻塞证明 |
| 15:07:01 / 04 / 22 | ArrangeJob 48 / 17 / 19ms | 分盘运算不是本次数秒卡顿来源 |
| 15:13:43 起 | 对 PID42157 `sample 42157 20 10 -file ...`，同时帮助→检查新版本 | 成功复现官方 2.5.0.0-alpha 弹框，采样抓到主线程文本布局阻塞 |

复现采样主线程共 1780 个样本；`UpdateVersionDialog::update_version_info` 的四个同层分支共 621 个样本（233+200+130+58），在 10ms 采样粒度下是数秒量级，不能当精确秒表。调用链包括 `Label::Wrap`、`wxTextWrapper2::Wrap`、`GetPartialTextExtents`、Cocoa/CoreText 的文本大小计算，并被多次尺寸事件/布局重复触发。`wxDialog::ShowModal` 内的正常等待不计为排版卡顿。

对应源码：`ReleaseNote.cpp:524` 把完整 release note 交给 `Label(..., LB_AUTO_WRAP)`，随后连续 SetMinSize/SetMaxSize/Layout/Fit；`Widgets/Label.cpp:120` 逐行、逐段测量剩余文字宽度，`Label.cpp:322–335` 对尺寸事件重新全量换行，没有宽度/内容缓存。当前 AX 捕获约 8.9 万字符，说明文本规模很大；该数字是 AX 内容长度，不冒充 HTTP body 的精确大小。

原先怀疑 WebView 为主要卡点，复现后修正：WebView 创建和 handler 分支仅少量样本（创建4、handler6），大头是长文本布局。弹框仍无条件创建隐藏 WebView，属于可顺带减少的工作，但不能把本次主要耗时归给它。15:10:07 的 `video.setAttribute` 空对象警告来自 `StatusPanel.cpp`，不是此次更新说明的排版根因。

## 为什么自编译也提示官方升级

`GUI_App::post_init()` → CallAfter → `check_new_version_sf()`；`AppConfig::version_check_url()` 在没有覆盖配置时仍用 `https://check-version.orcaslicer.com/latest`。本机 `check_stable_update_only=false`、`skip_version` 为空，编译版本仍为 `2.3.2.2-Release`；重新编译不会改变版本渠道，也不自动禁用启动升级检查。

HTTP `perform()` 自建后台线程，成功后 QueueEvent；主线程事件处理构造 `UpdateVersionDialog`、填入说明，再 ShowModal。当前官方返回选中了 `2.5.0.0-alpha`，所以出现该提示。源码当前设置 `force_upgrade=false`，下载需用户点击，检查本身未安装更新。Bambu 资源接口日志中的 `02.08.02.61` 是另一资源同步响应，不是用户看到的 2.5.0 软件弹框。

## 模型是否重叠及为何移得很远

有效打印空间 101×101×105mm；最后一次分盘日志为 movable=1、unprintable=2、locked=0。三个对象均为8顶点/12三角面的轴对齐立方体，组合顶点变换后：

| 对象 | 尺寸 mm | 中心/实例偏移 X,Y,Z mm | 世界范围 Y mm | 世界范围 Z mm |
| --- | --- | --- | --- | --- |
| 1（盘内） | 10.1³ | 50.5,50.5,5.05 | 45.45～55.55 | 0～10.1 |
| 2（盘外） | 110³ | 50.5,-434.3,55 | -489.3～-379.3 | 约0～110 |
| 3（盘外） | 110³ | 55.55,-4671.25,52.5 | -4726.25～-4616.25 | -2.5～107.5 |

三对 AABB 都不相交，因此网格也不相交；两个大模型 Y 方向中心相差4236.95mm。当前视图只看到一个大块不能证明真实重叠。对象3还存在底面低于Z=0约2.5mm的状态，分盘没有自动重设其Z；不将实例偏移Z误认为模型底面Z。

GUI 当前路径：

1. `ArrangeJob::check_unprintable()`（450行）先把高度110>105的对象移出 m_selected，进入 m_unprintable，跳过共享装盘求解；XY尺寸也超过平台，但此次已先被高度筛除。
2. `ArrangeJob::finalize()`（739行）令每个不可打印对象 bed_idx=beds+1，在原 translation 上调用 `PartPlateList::postprocess_arrange_polygon()`。
3. `PartPlate.cpp:4794–4797` 根据 row/col 增加世界盘偏移；这些被提前筛除的对象没有经过排布器归一化到盘内局部坐标，因此重复分盘会在原世界位置再次累计偏移。当前单列盘间距=101×1.2=121.2mm。
4. 当前盘排布还有放大路径：`preprocess_arrange_polygon_other_locked()` 把不在任何盘的对象设为 MAX_PLATES_COUNT=36，放入 m_locked；finalize 计算 `beds=max(ap.bed_idx,beds)` 时把这个盘外哨兵当真实盘号，导致另一个不可打印对象可能被送到虚拟第37行，位移约4484.4mm。本次 job1 为 current_plate_only=true、locked=1、unprintable=1，与该路径吻合；不能凭最终坐标还原未记录的每一次历史操作。

这些是 GUI 原有路径，本轮没有修改。此前 CLI `--export-model-transforms` 的“一越界实例一返回盘、XY居中、Z保留”只作用于返回 JSON，没有改上述 GUI 盘位置。

## 建议处理顺序与验证边界

1. 优先修复更新说明显示：固定大小的只读长文本控件或按需展示摘要/完整说明，避免整篇说明经 LB_AUTO_WRAP 多次测量；按需创建 WebView。复测同一份长说明，抓主线程采样并验证窗口缩放/中英文，保留版本筛选和下载行为。
2. 自有发行版如不应引导安装上游原版，应明确独立更新渠道或开发构建开关；不要只为消除卡顿关闭安全更新。`仅检测正式版` 只能过滤 alpha，不能修复长文本排版问题。
3. 若后续授权修复盘外摆放，维持“放不下仍在盘外”的业务规则，修正世界/局部坐标转换、过滤盘外哨兵，不把 GUI 也改成一件一盘。分别验证连续分盘坐标稳定、当前盘/全部盘、多个超高/超宽模型和Z保留，CLI JSON仍单独验证。

本轮 GUI 已实际执行帮助→检查新版本→捕获弹框→关闭测试弹框；模型坐标通过用户操作后的自动备份核对。没有再次分盘改变用户模型，没有执行修复后回归，也没有用此前 CLI 测试宣称 GUI 已修好。启动原始卡顿没有当时栈，但同进程复现与采样已确认更新说明布局能够造成该类卡顿。macOS14最低版本/实机兼容验证仍沿用打包记录的待验边界。

本轮文档检查：`python3 scripts/check_ai_module_docs.py --all` 通过（35模块、问题0），`git diff --check` 通过；GUI检查更新前后自动备份 `.3mf` SHA256一致，模型未因本次复现改变。

## CLI 复查命令与历史归因（用户后续要求）

输入 `planning/debug/gui-arrange-cli-20261017/input.3mf` 从上述15:07:28自动备份制作：原 `.3mf` ZIP成员逐字节复制，把外部三个 `.model` ZIP中的XML成员内嵌，未改模型、变换、对象覆盖或项目配置。文件已在本地落盘，不要求用户另找输入；它是用户最后一次GUI分盘后的场景，再执行一次CLI全盘摆放，并非最初分盘前场景。

仓库根目录执行：

```sh
./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --debug 4 \
  --datadir planning/debug/gui-arrange-cli-20261017/data \
  --outputdir planning/debug/gui-arrange-cli-20261017/cli \
  --orient 0 --arrange 1 --auto-plate 1 \
  --arrange-spacing 0 --allow-rotations 0 \
  --export-model-transforms --export-3mf arranged.3mf \
  planning/debug/gui-arrange-cli-20261017/input.3mf \
  2>&1 | tee planning/debug/gui-arrange-cli-20261017/cli.log
```

采用3MF内有效配置，不额外覆盖工艺/机器/耗材。方向关闭、旋转关闭、全盘、间距0对应用户最后一次GUI日志；不传 `--ensure-on-bed`。AI运行时使用相同参数的绝对路径并以证据目录为cwd，避免改根result.json；`command.json`保存实际argv。管道末端tee退出码不代表CLI退出码，AI以subprocess直接记录CLI退出0并核对JSON return_code=0。

实际结果（plate_index零基；三个对象都叫“立方体”）：

| JSON实例ID | 模型 | plate_index | position |
| --- | --- | --- | --- |
| 8 | 10.1mm正常立方体 | 0 | [50.5,50.5,0] |
| 12 | 第一个110mm立方体 | 1 | [50.5,50.5,0] |
| 16 | 第二个110mm立方体 | 2 | [50.5,50.5,-2.5] |

JSON的position是XY包围盒中心及底面Z（bbox.min.z），不是上一表的实例原点偏移Z。第三个-2.5符合备份实际底面；本轮没有强制清零。越界独立返回盘号符合约定。两次运行分别开启/关闭export-model-transforms，导出3MF的几何、变换、打印开关、实际盘成员完全相同；实际仍1盘、盘内1对象。不能按JSON虚拟盘号声称3MF创建了三盘或超大模型已可打印。

证据目录同上，`validation.json`包含配置比较、物理结果对照与根result.json哈希未变；床面/高度、支撑、筏层、brim、skirt、擦料塔和打印顺序等核对项一致。程序SHA256与前次新构建一致。本轮未重复构建/运行无关全量测试。

Git历史核对：

- `ArrangeJob.cpp` 的超高筛除、m_locked最大盘号、m_unprintable追加虚拟盘，以及 `PartPlate.cpp` 的盘外哨兵和世界坐标追加，blame均指向 `1555904beffef41195003f184dec0af46b308eee`（2022-07-15，`Add the full source of BambuStudio`）。核心问题路径已在上游导入历史存在。
- 更新说明 `Label(...,LB_AUTO_WRAP)` 与Label的尺寸事件全量换行，指向 `0162fceb98f5d60b767f8d8f648172b1cda56414`（2023-07-04，`FIX: [STUDIO-3547] use Label instead of ErrorMsgStaticText`）；周边布局代码更早。
- 上述四个GUI文件相对当前HEAD无工作区差异；本轮CLI JSON及打包更改未改这些路径。这里确认的是本仓库继承历史，不宣称已测试官方最新2.5.0是否仍有相同行为。按用户最新决定，盘外摆放和升级弹框暂不修改。

## 后续CLI返回Z优化（已按用户决定撤销）

按用户新要求，仅独立返回盘上的底面归零。上述原命令重跑后，实例8/12/16返回盘号仍0/1/2，position均为[50.5,50.5,0]；只有实例16的JSON底面从-2.5改为0，实际3MF仍保留原底面。JSON `planning/debug/gui-arrange-cli-20261017/cli/result.json` 已更新；对应新程序身份及逐项对照见 `z0-validation.json` 和[分盘验证](2026-10-17-cli-oversized-arrangement.md)。GUI升级弹框/原有盘外摆放未改，不把此CLI优化写成GUI修复。

## 撤销Z归零兼容完成（最新）

用户确认-2.5来自原模型后，取消仅JSON归零处理。CLI恢复所有实例的真实底面Z，越界独立返回盘及XY居中不变。已重建并更新用户原App路径，20次CLI成对回归通过；本场景重新返回盘0/1/2、Z=0/0/-2.5，JSON与初次归零前逐字段一致，实际3MF不变。最新 `planning/debug/gui-arrange-cli-20261017/preserve-z-validation.json` 记录程序身份，详情见[分盘验证](2026-10-17-cli-oversized-arrangement.md)。上述归零版本记录仅为历史，GUI调查结论不变。

## “原本Z不为负”的证据边界澄清

用户追问最初模型的Z。重新直接解析当前CLI输入及导出3MF确认：第三个立方体实例变换Z为+52.5mm、模型高约110mm，变换后实际底面约-2.5mm；CLI运行前后底面相同。这里的输入来自15:07:28 GUI操作后的自动备份，不是最初创建/导入模型的原始状态。不能由该备份认定模型最初创建时就下沉，也不能仅凭它确定哪次GUI操作引入了-2.5。此前“输入模型已有负底面”仅针对这份CLI输入成立；实例平移Z是正数与底面Z为负并不矛盾。本轮仅核对，未改代码或用户模型。

## 第三个立方体改为90mm高

按用户要求，修改自包含CLI输入副本中的对象3（CLI实例16），仅将Z缩放从10.8910887改为约8.91089075，使世界高度110→90mm；XY尺寸仍110×110mm、位置及中心Z=52.5mm保持。同步该对象的assemble Z缩放，其他对象及所有网格字节、项目配置、盘归属保持。源输入未覆盖。

新输入 `planning/debug/gui-arrange-cli-height90-20261017/input-height90.3mf`；复用现有已验证App（SHA256 f5ce7da238f3b7e51f0abc2610442497cef191099290f6e7bccdff3ccf5ed137）和原CLI参数，独立data/output目录，退出0且JSON return_code=0。导出3MF实测高度90、底面7.5，JSON实例16仍独立返回第2盘、position=[50.5,50.5,7.5]。宽深仍超过101mm床面，缩短高度并未消除XY越界。

证据为同目录 `command.json`、`cli.log`、`cli/result.json`、`cli/arranged.3mf`、`validation.json`。该操作不调整底面贴床、不改变GUI当前场景，不改源码或构建；实际输入及输出几何均已读取核对，根result.json哈希未变。

## 最新约定：仅盘外返回Z为0（完成）

用户明确要求盘外XYZ由CLI返回层处理，XY按原有盘中心、Z为0；盘内实际坐标保持。已修改并更新原App路径，20次回归及110/90mm两场景通过。两份JSON中的第三实例均为[50.5,50.5,0]，实际3MF仍分别为-2.5/7.5mm，输入几何及GUI未改。最新身份在两任务目录的 `overflow-output-z0-validation.json`，完整记录见[分盘验证](2026-10-17-cli-oversized-arrangement.md)。非零Z不是盘外独有；本轮盘内-2.5/+7mm回归均保留真实值。
