# 模型修复与智能切片引擎：执行与验证记录

## 当前执行状态

- 10-05 最新授权与步骤：用户要求提交已验证修改，并分析后续模型修复方案。按“修复核心/测试/对应文档”和“向导日志/对应文档”拆分两笔提交；本记录为两批共用证据，随第一笔入库。只提交本任务文件，不收其他任务的 `2026-10-04-study-textbook.md`、日志、模型产物或 result.json，不推送。后续方案仅研究分析，不恢复已撤回实现。
- 当前目标：只优化现有高级修复中必要的诊断与确定无用的距离计算；原智能引擎实施方案已撤回，后续章节只保留历史证据。
- 边界：不覆盖用户预设，不改变七项公开检测含义，不自动提交/推送/发布，不默认启用近似重建或自由生成脚本。
- Orca 基准：`master / 9e9381d595`，开始时工作区干净。
- Blender 服务基准：`feature/f_模型打印对比 / 8372a7a`，开始时工作区干净；不切换或合并其分支。
- 参数匹配服务基准：`Print3dParamAutoMatchService / feature/f_模型打印对比 / da99c43`，开始时干净；已定位现有 TemplateRuleEngine 和模板推荐服务，尚未修改后端两仓。
- 前置撤回（2026-10-04）：用户否定本批方案并要求先撤回代码、重新分析两张截图。已撤回本批 35 个跟踪文件的源码、测试、翻译与模块文档改动，恢复为 `master / 9e9381d595` 后再实施下述必要优化；不回退此前提交，不修改后端仓库。
- 可恢复备份：`planning/repair/withdrawn-smart-engine-20261004-151hK1/withdrawn.patch`；撤回前记录和简中 MO 同目录保存。无关 `result.json`、`one_vertex_touch.svg`、`2026-10-04-study-textbook.md` 保持原内容。
- 证据边界：撤回检查时 35 个目标文件与 HEAD 的差异为空；之后新增的必要优化已单独重建、验证。下文历史区的 194/81/86 等结果、构建哈希及 GUI 操作属于已撤回实现，不能作为当前代码的验证，也不要按旧 GUI 清单验收。
- 当前任务（随后获用户授权）：只实施必要的小范围优化——共享 CGAL 修复阶段计时、局部拓扑分支退出原因、已确定拒绝候选的昂贵距离检查短路。不恢复保守模式，不改变修复算法、容差、公开 JSON、取消架构或 20,000 面限制。
- 10-04 自动化结论：Release arm64 构建完成；核心 190 用例、组合检测 CLI 76 场景、普通修复加试切片 78 场景通过。真实托架导出顶点/面与原标准模式结果一致。GUI 当时待验证，其后进展见下一条；Windows/Linux、实机打印未验证。未暂存或提交。
- GUI 更新（2026-10-05）：用户已重新构建；打包路径与 `src/Release` 的应用哈希相同，日志包含新增计时。托架 GUI 修复、备份落盘、重启恢复及重切片已有日志和实际产物证据；颜色、取消、正式另存重开等未覆盖项仍待验证。详见下面的日志复核，不把局部通过扩展为全部 GUI 通过。
- 当前任务（10-05 随后获授权）：仅收敛配置向导的全量 JSON 与逐耗材常规日志，保留数量/加载结果/异常，删除只为日志进行的重复序列化；不改预设解析、继承、选择、保存和网页传输，不处理其他低优先级报错，不自动提交。
- 当前完成（10-05）：向导日志降噪已完成，Release arm64 应用构建及向导 4 用例/184 断言通过。用户随后完成打包，`src/Release` 和 `build/arm64/OrcaSlicer/` 两份应用哈希均为 `07fe16b6...`，源码哈希不变。Computer Use 已验证加载、EP3/EP7 搜索、取消重开及原选择确认；选中配置摘要哈希前后一致，日志降噪生效。新程序保留原工程停在准备页；本轮无需用户重复上述向导测试。取消机制、空间索引和修复策略重构仍暂缓，未提交。

## 2026-10-05 配置向导日志降噪（实现、自动化及最小 GUI 验证完成）

- 仅修改 `WebGuideDialog.cpp`：删除两次仅为日志生成的完整 `m_ProfileJson.dump`；读取耗材不输出文件正文，网页消息不输出完整收发内容，仅保留大小/命令。完整网页消息仍按原机制序列化并发送，不改变业务载荷。
- 高频的逐耗材路径、列表加入和正常父级查找信息降为 debug；info 保留各品牌数量、加载完成汇总、选择/保存/取消和原有失败线索；解析及坏机型 error 原样保留。没有调全局日志等级，所以不会隐藏修复计时。
- GUI 入口是“添加/选择打印机”网页向导；CLI 不提供该窗口和消息处理，且共享配置解析/继承代码未改，本次不新增 CLI 参数或声称改变 CLI 行为。
- 验证：静态核对去掉日志语句和两处日志专用变量后，修改前后业务 token 一致；所有 error 日志保留。实际向导网页交互仍需新程序手测。
- 构建前核对：仍为当前仓库 `master / 9e9381d595`，Ninja Multi-Config / arm64 / Release，无其他构建进程。`cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2` 退出 0，日志 `planning/build/guide-log-noise-20261005.log`。重新编译 WebGuideDialog 并链接应用；已有测试目标未过期。原工具链未知警告选项、未改代码的 override/unused capture、重复库及 zstd 部署目标警告仍存在，不扩展为旧 macOS 或其他平台已验证。
- 自动化：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests '[Preset][Guide]'` 退出 0，4 用例/184 断言通过；日志 `planning/quality/guide-log-noise-20261005-tests.log`。覆盖向导机器元数据继承、喷嘴与源预设保留；不把这些核心测试称为网页日志运行验证。修复源码及测试未再修改，本轮不重复全套修复测试。
- 资源身份：本次无资源改动；新应用 `Contents/Resources` 指向当前仓库 `resources/`。差异审查图谱停留在 2026-08-28/`60148c114...`，不作为本次源码依据，已回到当前差异及源码核对。

| 本轮产物/源码 | SHA-256 |
| --- | --- |
| `src/slic3r/GUI/WebGuideDialog.cpp` | `d136767cf0f84efbc02764a3b2104ddfc4accdef025cb4518e3257c06952d2c8` |
| 新 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `07fe16b6c44f79884e39ffff9ed22789e0e5d7c1768c89fdf4a920bd49b3decb` |
| 旧 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `89a56d28707bd0504ceb7c6bdeb2ce717074908c561bc896b10a1e965e1d0c05` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `8ff1cf92dc002ca1507a0d4888d05eb559d332d829f6a26140013829a8e9e94f` |

### 本次 GUI 最小交接

先正常关闭现有实例，使用以下新程序启动；不要继续使用未更新的打包副本。`--debug 3` 保留 info，未改变全局默认等级：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/debug/guide-log-noise-20261005-gui.log
```

- 输入/配置：沿用现有用户配置和简体中文界面，不需要导入模型、切片或修改机型/喷嘴/耗材/工艺参数；本轮对象变换、对象覆盖、盘数不参与验证。保留原选中设备，打开向导时记录勾选状态，不清空或重置用户预设。
- 准备页 → 打印机选择列表的添加/选择打印机入口 → “选择打印机”窗口：检查机型图片与列表，搜索 EP3、EP7，再清空搜索；预期列表和搜索与修改前一致，没有缺失、空白或加载错误。
- 点击“取消”，重新打开同一窗口：预期原勾选不变。保持原选择点击“确定”：预期窗口关闭且现有机型/预设不变，覆盖取消及 SaveProfile 路径；不为本测试新增或删除设备。
- 日志预期：info 有各品牌汇总和 `LoadProfileData, finished, models=...`，不再输出 `GetFilamentInfo: Json Contents`、全量汇总 JSON 或逐耗材正常加载信息；解析失败等错误仍可见。不主动破坏资源来制造错误。
- 回传 `planning/debug/guide-log-noise-20261005-gui.log`，注明打开、取消、再次确认的时间；若界面有异常附截图。不需要导出 3MF 或 G-code。

当前结论：构建及自动化通过，GUI 待用户操作；此窗口无对应 CLI 操作，共享配置业务未改。Windows/Linux 未验证，实机打印不适用。已避免原来的大段日志及两次无用序列化，但没有新 GUI 对照计时，不声称已测得具体提速比例。未暂存、提交或推送。

本轮差异检查：`git diff --check` 退出 0；`python3 scripts/check_ai_module_docs.py --all` 检查 35 模块，退出 1，唯一问题仍为用户无关的 `one_vertex_touch.svg` 未归属，受影响模块指纹无报错。未修改或忽略该文件、`result.json` 及其他任务记录。

### 10-05 Computer Use 实测（完成）

- 用户授权 AI 操作 GUI，打包程序和源码均与上表新身份相符。按应用名称匹配时先启动了 `/Applications/OrcaSlicer.app`，停留恢复提示，未用于本次验收；随后通过完整路径打开仓库打包程序，进程 29946，09:22:08 启动。
- 新程序出现恢复提示，选择恢复以保留已有工程；恢复后为 EP7 0.4 nozzle、QIDI PLA Rapido @EP7 0.4 nozzle、0.20mm Standard @EP7、光滑高温打印热床，托架在准备页显示。未改变模型、摆放或切片配置。
- 使用程序自动写入日志 `/Users/liwei/Library/Application Support/OrcaSlicer/log/debug_Mon_Oct_05_09_22_10_29946.log.0`，不另启动带 tee 的第二个测试实例。已完成向导搜索、取消、原选择确认及日志摘要核对。

| 场景 | 实际结果 |
| --- | --- |
| 首次打开向导 | 09:24:06–07 加载成功，图片及完整列表显示；汇总为 models=336、machines=815、filaments=3868、processes=2236 |
| EP3 搜索 | 仅显示 EP3/EP3 Pro/EP3 Plus；前两款未勾选，Plus 0.4 已勾选；未触碰复选框 |
| EP7 搜索 | 仅显示 EP7；0.4 已勾选，0.2/0.6/0.8 未勾选；清除搜索后完整列表恢复 |
| 取消并重开 | 09:26:33 `GuideFrame cancelled`，返回准备页；09:26:42–43 重新打开，数量汇总相同，EP3/EP7 原选择一致 |
| 原选择确认 | 09:28:00 `SaveProfile` → `GuideFrame returned ok` → `GuideFrame applied`；日志选中 Generic Marlin Printer 0.4、EP3Plus 0.4、EP7 0.4，返回准备页无错误弹窗 |
| 配置与工程保持 | 确认后屏幕仍为原 EP7/耗材/工艺/平台和托架。取消前、取消后、确认后，对 `OrcaSlicer.conf` 的 `{models,filaments,presets,orca_presets}` 作 `jq -cS` 规范化再 SHA-256，均为 `ca200235b4cd7104ee83dd79123060aa703032418fa5de358108797bd376f1e9`；未新增/删除设备或覆盖预设 |

- 日志降噪：每次 `GuideFrame before ShowModal` 到 `LoadProfileData, finished` 共 294 行、36,844 字节；原完整 JSON 和高频 `tFilaList Add` / `Load Filament:` 匹配数为 0，取消、保存和加载汇总仍存在。两个区间分别约 1.041 s / 0.992 s，含该日志区间内异步数据加载，不是完整窗口渲染计时；没有旧版本同条件基准，不报告提速倍数。
- 稳定日志快照：`planning/debug/guide-log-noise-20261005-cua-29946.log`，1,950 行、323,479 字节，SHA-256 `d8eb0a40c65b4175807f1bc16cee143694f4330d36282020afb867cb7e29ec27`。原运行日志仍会追加心跳；不要将快照字节数与此前 38 MB 不同时长会话简单换算成性能提升。
- 快照仅一条 error：启动时缺少 3Dconnexion 驱动，早于向导；恢复工程时仍有之前已知的父配置 warning。本轮向导操作期间没有新增 error，不等于整个程序无 warning。没有人工破坏预设制造错误，错误路径保留依赖前轮静态核对。
- 新程序保留恢复的工程，停在准备页，未切片、保存项目或改模型；本次不替代高级修复的未覆盖场景。按名称误启动的 `/Applications` 旧版本仍停留恢复提示，正常退出快捷键未生效；未替用户选择丢弃恢复数据，也未强制结束它。它不参与本次验收。
- 结论：本次日志改动的 macOS 最小 GUI 验证通过；无需用户重复清单。自动化结果保持前轮 4 用例/184 断言，CLI 无对应窗口；Windows/Linux、首次安装/其他配置组合未验证。本轮只更新验证记录，不改源码、不重编译、不提交。

### 10-05 入库检查收尾与 result.json 来源

- 用户明确要求删除 `one_vertex_touch.svg`：确认它是仓库根目录未跟踪文件后，移至 `/Users/liwei/.Trash/one_vertex_touch-20261005-orca.svg`，可从废纸篓恢复。未删除或回退 `result.json`。
- 删除后 `python3 scripts/check_ai_module_docs.py --all` 退出 0（35 模块，问题 0），`git diff --check` 退出 0；原未归属阻塞已解除，未提交。
- `result.json` 是 CLI 的结果报告，不是配置输入。当前源码 `record_exit_reson` 默认使用相对路径 `result.json`，传 `--outputdir` 时改写到该目录；CLI 正常结束及多种失败出口都会写报告，`--help` 分支打印帮助后也会继续进入统一成功收尾。GUI 正常启动分支直接返回 `GUI_Run`，不会经过这个 CLI 成功收尾，因此并非每次 GUI 启动或编译都会生成根目录文件。
- 仓库根目录的 `result.json` 已在历史提交中被 Git 跟踪；当前改动仅新增空 `obj_exports` / `output_files`，文件时间仍为 10-04 14:14，未被本轮 10-05 GUI 验证改写。仅凭空成功报告无法反推具体是哪次命令，但生成/覆盖路径已由源码确认。本轮仅解释原因，不改结果协议、输出默认路径、Git 跟踪或忽略配置。

### 10-05 模型修复入库复审

- 用户要求仅检查是否应入库、有无必须追加的修复改动。本轮仍为 `master / 9e9381d595`；未修改生产代码、测试或修复策略，未提交。检查开始时 `result.json` 已不在 Git 差异中，本轮未操作该文件。
- 逐项复审当前 `MeshBoolean.cpp` 和测试差异：阶段计时的日志异常不会从计时器析构传播；退出原因仅细化原有 false 出口和资源保护；两处距离短路保持原有连通部件/包围盒/面积/体积/距离容差，只对已经被原条件拒绝的候选省去距离计算。没有新增网格验收步骤或放宽通过条件；接受路径、部分成果接口、失败回滚及公开协议未变。
- 共享入口复核：`ModelRepair`、GUI 工作线程/切割修复路径、普通 CLI 以及检测组合入口继续调用同一个 CGAL 核心；本次无需分别追加 GUI/CLI 策略开关。未恢复被用户否定的保守模式、智能参数引擎等改动。
- `MeshBoolean.cpp`、`test_meshboolean.cpp` 和测试程序 SHA-256 仍与前轮记录完全相同；直接复核现存原始日志末尾，核心 190 用例/14,574 断言、检测 CLI 76 场景、普通修复及切片 78 场景均通过。本轮复用证据，不重复构建或把旧日志冒充新执行。新打包应用仍为 `07fe16b6...`，此前托架 GUI 修复/备份/重启重切片的证据范围不扩展。
- 审核结论：没有发现本批改动的入库阻塞，建议先入库这些必要优化，不追加算法重构。日志增强提供后续优化依据，确定无用的距离计算短路有实际价值，但不承诺成功模型提速或提高修复成功率。
- 延后项：CGAL 内部即时取消、大模型局部边界空间索引、备用修复引擎与全模型策略升级仍按实际样例和耗时证据单独立项，不是本批前置条件。彩色/复杂场景完整 GUI 回归、Windows/Linux 和实机打印仍未覆盖，不能把本批可入库解释为整个高级修复已经全面验收。
- `git diff --check` 和文档全量检查均通过（35 模块、问题 0）。正式提交仍需用户授权、选择性暂存及 staged 检查；模型修复和向导日志可分开提交，其他任务文档不混入。

## 2026-10-04 必要优化（实现及自动化完成，待 GUI）

- GUI：GUI_ObjectList → FixModelByCgal → ModelRepair；普通 CLI 与检测组合入口也经 ModelRepair 调用 MeshBoolean。计时与提前拒绝放在共享 MeshBoolean，不新增两端策略差异。
- 范围：分阶段单调时钟计时、局部拓扑修复区分预算/不适用/证明失败；保持原候选选择与拒绝阈值，只有原本必定失败的廉价条件才能跳过表面距离。
- 验证计划：日志专项（成功/拒绝/超限）、既有模型修复全专项、普通修复及组合检测 CLI；不运行旧二进制。本轮未提交，用户无关文件继续保留。
- 首批修改：仅 `MeshBoolean.cpp` 及 `test_meshboolean.cpp`；计时使用单调时钟、单次调用 ID 和异常安全析构日志；诊断码不改公开 JSON。两处保形判据先计算原有部件数/包围盒/面积/体积条件，仅在全部满足时计算距离，阈值和最终布尔条件不变。
- 构建前核对：工作目录与 `CMAKE_HOME_DIRECTORY` 一致，Ninja Multi-Config / arm64 / Release；无其他构建进程。运行 `cmake --build build/arm64 --config Release --target libslic3r_tests OrcaSlicer --parallel 2`，首轮日志 `planning/build/repair-diagnostics-20261004.log`。
- 首轮构建退出 0。新日志专项首次执行 2 用例 / 103 断言，1 条断言失败：测试将 Boost 日志的布尔 `true` 错写为数字 `1`，已有日志机制确实输出 `true/false`。只修正精确日志字符串断言，不修改业务行为；失败日志 `planning/quality/repair-diagnostics/focused-r1.log` 保留。随后按差异行格式化并重建，重新执行专项，不用首轮二进制证明后续代码通过。

### 本轮最终构建与验证

- 第二轮构建退出 0，日志 `planning/build/repair-diagnostics-20261004-r2.log`；此后没有修改 C++。原工具链的未知警告选项、重复库和 zstd 部署目标警告仍存在，不证明旧 macOS 已通过。
- 测试程序：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests`。日志专项过滤 `[ModelRepair][Diagnostics]`：2 用例 / 103 断言通过，日志 `planning/quality/repair-diagnostics/focused-r2.log`。完整过滤 `[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`：190 用例 / 14574 断言通过，日志 `core-r2.log`。专项包含在完整结果内，不重复计数。
- 检测组合入口：`python3 scripts/test_cli_model_check.py build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，76 场景通过，退出 0；日志 `planning/quality/repair-diagnostics/cli-check-r2.log`，证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-model-check-w86ie3aa`。该脚本不证明切片/打印通过。
- 普通修复与试切片：`scripts/test_cli_repair_acceptance.py` 使用同一应用，`--load-settings` 为 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/machine_settings_1.config;planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/process_settings_1.config`（实际传绝对路径）；`--load-filaments` 为该目录 `filament_settings_1.config` 两份、分号分隔。78 通过 / 0 失败，退出 0；日志 `planning/quality/repair-diagnostics/cli-repair-slice-r2.log`，证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-repair-acceptance-7wfvz_bz`。每场景命令和产物保存在该证据目录。

| 当前产物/源码 | SHA-256 |
| --- | --- |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `c30c4facc29fed08c731e0691a5825b651eae686e7ee7ae3b4533933596a7cfd` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `8ff1cf92dc002ca1507a0d4888d05eb559d332d829f6a26140013829a8e9e94f` |
| `src/libslic3r/MeshBoolean.cpp` | `b3245cf9a821414f9cdc8036ccd7cc0ceb50ac9bc0214a846670934e36428560` |
| `tests/libslic3r/test_meshboolean.cpp` | `290c469262d8560ac64709be2a98f57441e38a668704ea279c83b3325dc932fb` |

### 真实托架与耗时解释

- 输入：`/Users/liwei/Library/Containers/com.tencent.xinWeChat/Data/Documents/xwechat_files/wxid_naol68t5y06922_46e9/msg/file/2026-09/(圆角)永久方案托架20260918.stl`，SHA-256 `5df90e7a3122aae5dbb09d5897b074ebc4db2f3197aa0c839741e4e66eebbca8`。单对象/单体积、原始坐标，无人为缩放或旋转。
- 新应用使用 `--debug 3 --repair-model cgal --export-obj`，隔离 datadir 和 outputdir 在 `planning/quality/repair-diagnostics/bracket/`；日志 `run.log`，结果和 OBJ 在 `output/`。退出 0，862 面/114 开边 → 856 面/0 开边，`success=true / committed=true / mesh=passed`，无部件删除。
- 对比当前 OBJ 与 `planning/quality/smart-engine/bracket-standard/(圆角)永久方案托架20260918.obj` 的全部 `v` / `f` 行，`diff` 退出 0，顶点和面一致。这里旧产物仅作几何对照，不用旧二进制冒充新代码测试。
- 单次 CGAL 核心总耗时 12.5662 ms：局部拓扑准备 10.5046 ms、拓扑验收 1.32604 ms、表面距离 0.211125 ms。该样例的距离检查不是主要耗时，不能据此删掉保形验收；也不能外推大型模型的耗时。
- 计时只覆盖 CGAL 候选处理，不含外层检测、绘制恢复、导出或 GUI；没有同机重复基准与 2.4.2 整链路对照，不报告提速倍数。确定失败的候选可少算一次距离，但不保证成功模型加速。

### 本轮 GUI 最小交接（待用户操作）

先关闭其他 Orca/WiseBeginner 实例；从当前仓库启动新构建，`--debug 3` 用于记录 info 级耗时：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/repair-diagnostics/gui-run.log
```

本轮未新增策略选择框。沿用已有“高级修复”及属性保留规则，不按后文已撤回的“保守/标准”清单操作。

- 托架验证只做几何修复：新建单盘工程、导入上面的原始 STL、一个实例、保持 100% 缩放和原始朝向，不自动摆正、不做切割。机型/喷嘴/耗材/工艺不参与此次 CGAL 判定；记录当前实际机型即可，不为该修复测试改动用户预设。原托架的尺寸须保持，孔道不应被封闭。
- 彩色验证及切片使用已有未修复工程 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-input/repair-strategy-input-3slots.3mf`（SHA-256 `f636015d602d46d42d06723d31b4f111e529d9ee5315b983c88cb980269c9b13`）。它是测试输入，不含已撤回策略配置：单盘/单对象 10×10×10 mm 缺顶面立方体、10 面、红绿面着色、100% 缩放/无旋转；EP3 Plus 0.4 nozzle，0.20mm Standard @EP3 Plus，层高 0.2/首层 0.24 mm，Cool Plate，支撑关闭，3 槽 EP3-PLA悬垂优化 @EP3 Plus（白/红/绿），无对象覆盖。若加载时替换了预设，请回传提示和最终参数，不视为同参通过。
- CLI 对应配置来自上面三份展开 config；机器/工艺/耗材 SHA-256 分别为 `525f96226d82b47a33e5b6ee69bbda6d3291deb8855e967b013764b0d30df9ab`、`9f5cd82ce8a5f33995bb38cc36dab0677a225d43bcddbeeffc2ff1c60fb762ec`、`778e0f655f7c5895db968ed9e733b572b52be1ab4dd2cf75ea288e153951255f`。CLI 彩色 OBJ 使用两份耗材配置，GUI 工程显式包含默认白色及红绿两色，故不声明 GUI/CLI G-code 逐字节相同。

| 场景 | 操作 | 预期及回传 | 状态 |
| --- | --- | --- | --- |
| 原托架 | 准备页选对象 → 右键高级修复；完成后另存 `bracket-repaired.3mf` | 对象仍完整、原孔道保留；对应 CLI 为 856 面/0 开边；日志有阶段、拓扑分支结果和计时汇总 | 待 GUI |
| 彩色及撤销重做 | 打开上述原始彩色工程 → 高级修复（保留现有颜色）→ Cmd+Z → Shift+Cmd+Z | 补齐顶面且红绿面保留；撤销恢复缺顶面，重做恢复补面；对象/盘/位置不变 | 待 GUI；核心及 CLI 属性/事务测试通过 |
| 保存重开及切片 | 彩色修复后另存 `colored-repaired.3mf` → 重开 → 切片单盘 → 导出 `colored-repaired.gcode` | 补面和颜色仍保留；10 mm 高度有连续挤出路径，不是空切片；新增顶面允许默认颜色，所有警告原文回传 | 待 GUI；CLI 修复后切片通过 |

回传 `planning/quality/repair-diagnostics/gui-run.log`、上述两个 3MF 和 G-code，注明操作时间段及异常截图。无新增界面/翻译，未改变取消机制，本轮不重复要求慢 CGAL 硬取消验收。GUI、Windows/Linux、实机打印均未执行；自动化通过不替代这些结论。

### 本轮差异与文档检查

- 生产源码仅 `MeshBoolean.cpp`；测试仅 `test_meshboolean.cpp`，模块文档及本记录同步。没有恢复保守模式、没有更改补洞/删部件/保色策略或公共参数/JSON。
- 无关 `result.json`、`one_vertex_touch.svg` 和其他任务记录未修改；其他任务可能并行更新其文档，不回写或覆盖。
- 最终 `git diff --check` 退出 0；`python3 scripts/check_ai_module_docs.py --all` 复核 35 模块、12853 归属文件，退出 1，唯一问题为无关的 `one_vertex_touch.svg` 未登记归属。受影响模块指纹无报错；未删除或忽略用户文件规避检查。GUI 输入和三份配置哈希均复核一致。未暂存、提交或推送。

## 2026-10-05 用户 GUI 日志及重启恢复复核

本次只分析，没有改业务代码、启动 GUI 或重新构建。

### 身份与证据

- 分支仍为 `master / 9e9381d595`；`MeshBoolean.cpp` 哈希仍为上表 `b3245c...`。用户 08:38:14 新构建的 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 与 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA-256 同为 `89a56d28707bd0504ceb7c6bdeb2ce717074908c561bc896b10a1e965e1d0c05`。因此这次打包副本不是上一轮提醒避免使用的旧程序。它不是昨天自动化所用二进制，但本次修复源码未变且 GUI 输出新诊断事件。
- 首次日志：`planning/debug/orca-user-gui-run-20261005-083841-368302.log`，38,126,356 字节、1,063,318 行，SHA-256 `8ac036b6c1dbb7938ee0e87e69e760e09481180017db5af93699d6999b3d9dc2`。
- 重启日志：`planning/debug/orca-user-gui-run-20261005-084143-526136.log`，复核时仍持续追加备份心跳；核心恢复及切片完成于 08:41:49–08:41:52。日志含 NUL 字节，检索使用 `rg -a`；不能因普通 rg 报 binary matches 就认定无修复事件。

### 修复与备份结果

| 项目 | 证据及结论 |
| --- | --- |
| 第一次修复 08:39:52 | `status=repaired`，862→856 面、118→0 开边，failed/unresolved 均为 0；scene_committed 和 backup_queued 成功。CGAL 候选耗时 14.7103 ms，其中局部拓扑准备 12.0957 ms、距离 0.239083 ms |
| 第二次修复 08:40:40–41 | 同为 862→856 面、118→0 开边；CGAL 候选耗时 15.4462 ms，其中拓扑准备 12.7197 ms、距离 0.2605 ms。未移动顶点、未补洞，删去 6 个经过内部性证明的面；不是删除整个部件 |
| Undo/Redo | 08:40:26 和 08:40:36 有 snapshot reloaded；随后再次修复的输入为 862 面。可证明快照恢复入口执行，不能只凭该日志证明完整撤销/重做操作顺序和界面效果全部通过 |
| 备份写盘 | 第二次提交后 08:40:44.030 写入对象备份，08:40:44.043 元数据备份导出完成；不只停留在排队事件 |
| 重启恢复 | 08:41:49 明确走 `IMPORT_STAGE_RESTORE / m_load_restore=1`，读入 1 个对象，随后重新切片，完成事件 status=0；这是自动备份恢复，不是正式另存 3MF 后重开的验收 |

两次修复均为保留标注策略，不允许主动清空属性；当前 STL 样例不能证明彩色模型保色通过。计时只覆盖 CGAL 核心，不是整个对话框耗时；这一托架的距离验收不足 0.3 ms，无证据表明应该为提速删掉保护。

### 恢复模型与 G-code 实物核对

证据目录：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orcaslicer_model/Mon_Oct_05/08_38_52#71715#56/`。文件属运行时备份，未修改、搬移或删除。

- `3D/Objects/(圆角)永久方案托架20260918.stl_1.model` 是 ZIP 包而不是裸 XML；打开其内部模型检查，426 个顶点、856 个三角面、按坐标焊接统计开边 0、超过两个邻面的边 0。局部包围盒约 135×102.5×70.0001754 mm。此只读结构检查不替代全套几何自交验收。
- 重启前 `Metadata/.71715.1.gcode` 与重启后 `Metadata/.79905.0.gcode` 均为 3,309,900 字节、126,376 行，去注释后的 115,905 条非空指令全部相同；逐行对比仅第 2 行生成时间不同，内嵌配置无差异。
- 两者均为 350 层、45.76 g、预计 1h25m43s；预览路径包围盒均为 `{10.838, 11.119}–{150.983, 124.33}` 且在平台内，不是空 G-code。此为软件一致性，不证明实机打印效果。
- 两份 G-code SHA-256 依次为 `e070be1ff224b74b55fe8fd70e069e1a130818462e923325e56d4cf460611a99`、`9e8c0f57d54c4dc4686d1814f0482564149b5679eb82bbcc518fb1a6904d6653`。

### 其他日志问题的分级

1. **建议后续优化：配置向导日志过量。** 首份日志在 08:39:29–30 加载各品牌/耗材时输出大量配置内容，未带时间前缀的 JSON 行约 100 万。源码 `WebGuideDialog.cpp` 的 `GetFilamentInfo` 在 info 级记录完整文件内容，`LoadProfileData` 还记录汇总 JSON。与新增 CGAL 计时无关；可增加终端/磁盘输出负担，但本次未测量开关日志的性能差值，不能断言造成了几秒卡顿。建议降到 debug/trace 并只保留摘要，不为降噪改动预设加载业务。
2. **此次无切片差异：重启后三条父预设警告。** 备份的机器/耗材/工艺嵌入配置 `inherits` 均为空；`PresetCollection::load_project_embedded_presets` 无父项时使用默认配置后覆盖工程内配置，仍无条件记录缺父项 warning。日志随后 `validate presets success`，本次前后 G-code 除生成时间外一致，因此没有证据表明参数丢失。未概括为所有真正缺父项的情况都无害。
3. **低优先级：`calc_exclude_triangles` 三条 error。** 发生在 EP3 Plus 平台初始化/新建工程时，来源为平台禁入区域绘制，不是托架三角网格修复。空/退化禁入区域亦可能走到该错误日志；本次未独立复现具体运行态区域，不直接断言其几何内容。之后切换 EP7，切片完成且路径在平台内，没有本样例打印失败证据。
4. **环境提示：3Dconnexion 驱动缺失。** 两次启动各一条，影响专用 3D 鼠标；不使用该硬件时不需要处理，不是 CGAL 或切片错误。网页导航被记为 warning、网络插件未启用等信息不代表本次模型处理失败。

未发现修复失败、切片失败或崩溃堆栈。第一份结尾记录“取消关闭”后只剩备份心跳，没有正常退出完成记录，不能从此断言用户是正常退出还是强制结束。当前可确认的是修复成果已落盘且第二次启动成功恢复、重切片一致；颜色、工作中取消、正式另存重开及实机打印仍未覆盖。

## 2026-10-04 撤回与重新审查

- 用户明确：先剔除刚才的未提交实现，关注现有算法是否过度复杂、能否直接对齐 2.4.2；本轮不授权新的修复算法改造。
- 撤回范围：`repair_conservative` CLI/GUI 策略、外层保形验收、组合检测分流，以及对应测试、翻译、模块正文和指纹；保留以前已经提交的模型修复和备份修正。
- 当前参考：本地 `upstream-v2.4.2` 源码与当前 HEAD；图谱早于相关代码，不作为现状证据。性能差异须同模型/同机器实测，不能从检查数量推定耗时倍数。

### 两张截图的重新分析（撤回后的 HEAD）

- 本地 `upstream-v2.4.2` 为 `8500fcdccaa10b5099ac20d252af3a7c560046f1`，并核对官方 v2.4.2 的 MeshBoolean.cpp、FixModelByCgal.cpp。官方 GUI 拆壳/清理后以开放边非零作为调用 CGAL 修复的条件；底层执行 soup 清理、退化/非流形整理、与自身布尔并集尝试、补洞及朝向整理，末尾主要以闭合作为显式失败条件，没有本分支同等的最终自交/表面距离验收。其布尔与补洞同样可能昂贵，未做对照计时不能断言官方一定快。
- 对齐基础修复步骤可以讨论，但直接搬回官方的跳过条件和成功标准不适合无人值守的检测→修复→切片放行。保留有效性检查、结果提交/回滚和有标注时的属性保护有实际理由；重复网格转换、自交/距离验证及每个中间候选的验证成本需要计时后分层，不能以“更严格”证明所有开销合理。本轮不增加新模式、不替换算法。
- 真实反例：`planning/repair/bracket-242-comparison-20260929.md` 中用户标为 2.4.2 产物的 fix.stl 与原 STL，本轮 SHA-256 均与旧报告相符，完整哈希及输入路径保留在该报告。旧只读比较记录了开放边归零但残留非流形/共面重叠，并确认一处原孔道被新增面阻挡。本轮仅验证文件身份并复核报告，没有重新运行修复或用旧应用做回归；不能据一个样本推断总体成功率。
- 图 2：备份刷新已在 `87dd192c17` 提交，不需重复开发；`doc/model-check-workflow.md` 的 2026-09-29 16:09–16:24 记录还包含实际落盘与单对象 Undo/Redo，但未覆盖真实崩溃恢复。阶段失败日志已部分存在，局部拓扑分支超面数/预算耗尽/证明失败和阶段耗时仍未细化，建议优先补齐。当前 CGAL 调用结束前无法真正停止，先改善取消反馈和自有循环检查，独立进程硬取消另行评估，不为该体验问题立即大改架构。边界查询仍为遍历且有 20,000 面保护，建议有实际超限/性能证据时优化空间查询，不能直接移除保护；其他位置已有 AABB 树不代表此处已优化。
- 撤回检查：35 个目标跟踪文件与 HEAD 完全一致，`git diff --check` 通过；简中 MO 按恢复后的 PO 重新生成。文档全量检查唯一问题为用户无关的 `one_vertex_touch.svg` 未登记归属，没有修改或忽略该文件。本轮不提交，不重建应用，不将撤回前测试证据当作当前测试结果。

以下章节保留为**已撤回实现的历史记录**，不代表当前生效功能或继续执行指令。

## 关键决策

| 决定 | 理由与适用范围 | 状态 |
| --- | --- | --- |
| 保形优先、必要时确认 | 通用模型兼顾功能件，不从拓扑合法推导设计正确 | 用户已确认 |
| 保留 v4 公共检测摘要 | 后端任务身份不回填到已经精简的协议 | 用户已确认 |
| 部分修复不自动进入正式打印 | 保留现有安全进展，但不能冒充完整验证 | 用户已确认 |
| GUI/CLI 共用业务策略 | 同一有效选项应有相同结果；保留已明确的导入规则差异 | 用户已确认 |
| AI 完成必要最小构建和自动化，用户 GUI 手测 | 按 2026-10-04 AGENTS/validation/gui-validation；无实机打印授权 | 用户已确认 |

## 阶段与步骤记录

| 阶段 | 范围与验收 | 状态 |
| --- | --- | --- |
| 1. 修复可靠性 | 检测分流、策略解耦、保形/风险判断、核心及 GUI/CLI 回归 | 进行中 |
| 2. 参数闭环 | 同一产物身份、特征事实、受限参数覆盖、最多两次调整、切片验收 | 未开始 |
| 3. 备选方案 | 固定局部/重建候选、独立验收、失败与需确认结论 | 未开始；不上线自由生成脚本 |

## 构建与自动化证据

- 构建配置：现有 `build/arm64`，Ninja Multi-Config / Release / arm64；配置的源码根与本工作目录一致，无其他构建进程。
- 计划命令：`cmake --build build/arm64 --config Release --target libslic3r_tests OrcaSlicer --parallel 2`。
- 状态：首轮构建退出 0，日志 `planning/build/smart-engine-20261004.log`；沿用的第三方编译选项与 zstd 部署目标警告仍存在，不代表旧 macOS 已通过。
- 首轮定向回归：`[ModelRepair][Conservative],[ModelRepair][Routing]`，5 用例 / 76 断言通过，退出 0；日志 `planning/quality/smart-engine/focused.log`。此结果为专项子集，不与全专项叠加计数。
- 首轮完整核心专项：`[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`，193 用例 / 14547 断言通过，退出 0，日志 `planning/quality/smart-engine/core.log`。
- 首轮普通 CLI 修复：59 通过 / 0 失败，退出 0，日志 `planning/quality/smart-engine/cli-repair.log`；未提供切片配置，该脚本本轮不证明试切片通过。
- 首轮组合检测 CLI：退出 1，`cap-repair-obj` 被新增的 `!check_complete` 总开关拦截。其七项检查完成且缺陷已确认，只是缺陷导致包含关系不确定，不能等同于资源超限。修正为：unknown、基础检查未完成、部件上限或面积统计未完成才拦截；确认缺陷仍允许修复，并保留导出复检的完整验收门槛。失败日志 `planning/quality/smart-engine/cli-check.log`，不得覆盖作通过证据。
- 第二轮构建退出 0：`planning/build/smart-engine-20261004-r2.log`。核心 194 用例 / 14556 断言、检测 CLI 81 场景、旧修复加试切片 78 场景全部通过（各退出 0），日志分别为 `core-r2.log`、`cli-check-r2.log`、`cli-repair-slice-r2.log`。
- 第三轮补普通 CLI 保守策略端到端覆盖：84 通过 / 2 失败（退出 1，`cli-repair-slice-r3.log`）。新增测试错误地要求“未修好的开放模型禁止诊断性试切片”，而原入口 best_effort 明确允许试切片，且报告正确保留 `unresolved / mesh=not_passed / success=false / committed=false`。按已批准的“保留诊断性试切片、普通 CLI 默认不变”修订测试：要求诊断完成但绝不能宣称网格通过，不改业务代码掩盖结果；第四轮复测结果见下一条。切片退出 0 不能被后端解释为正式打印许可。
- 第四轮普通 CLI：86 通过 / 0 失败，退出 0，`planning/quality/smart-engine/cli-repair-slice-r4.log`。覆盖保留/忽略材质下的保守健康导出/切片、开放模型拒绝修复导出、开放模型诊断性试切片不冒充网格通过。证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-repair-acceptance-si0qbzoo`；每场景有命令、结果和 G-code（如适用）。86 为当前完整结果，先前 59/78/84 不叠加。
- `msgfmt --check-format` 退出 0；已只生成当前简体中文 MO，没有批量改其他语言。`python3 scripts/check_ai_module_docs.py --all`：35 模块、12853 归属文件、0 问题，退出 0；`git diff --check` 通过。未暂存/提交。

### 当前构建身份

第二轮之后未再修改 C++；后续只扩展 Python CLI 回归与文档。程序均来自本目录的 Release arm64 构建，不使用另一份旧打包目录。

| 产物 | SHA-256 |
| --- | --- |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `5ed539b89ed473a676e27431286dd88910c7b5f5069b1c3c1b58a2b78332da3a` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `5e9bbc413e9ebf05fa4241d9c370a56d05df51fa8b3d7926ca534b9c18e9ea5d` |
| `resources/i18n/zh_CN/OrcaSlicer.mo` | `7f797bd25bca182299c6881aa0175bef29d5d6eb019fdffff027c2779efbac8b` |

应用 `Contents/Resources` 指向本仓库 resources；代码/测试实际内容由本次 Git diff 与对应模块 review 指纹记录。日志、输入产物和 MO 构建产物不提交。

### 第一批实现

- 未确认缺陷或检查超限时保留 prepared 模型、unknown/needs_repair 及 not_started；确认缺陷导致的包含关系不确定，不阻断原有修复。
- 新增显式 `--repair-conservative 0|1`，不改变旧命令默认。保守模式不删除小部件、不填洞，worker 返回的候选也接受双向表面距离/面积/部件数检查。
- GUI 高级修复增加保守/标准/取消选择，保色与几何策略独立；临时切割修复保持旧行为。
- 标准模式的历史材料相关删除默认暂保兼容；新保守模式不会因忽略材质放宽几何处理。保守模式并非功能孔/薄壁与实机质量的完整证明。

## GUI 验证交接

以下 GUI 项目全部**待用户验证**。先关闭其他 Orca/WiseBeginner 实例，不要打开旧 `build/arm64/OrcaSlicer/` 打包副本。使用本次已构建应用：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-run.log
```

### 输入与有效配置

- 已准备可直接打开的工程：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-input/repair-strategy-input-3slots.3mf`。哈希 `f636015d602d46d42d06723d31b4f111e529d9ee5315b983c88cb980269c9b13`。
- 单盘、一个 10×10×10 mm 缺顶面立方体，10 面，红绿面着色，位置已摆盘、无缩放/旋转。工程存有面颜色，未进行修复或切片。来源为 `tests/data/model_repair_acceptance/11-colored-missing-top.obj` 和同目录 `colors.mtl`；可从源重建，不需改原文件。
- 工程有效参数：EP3 Plus 0.4 nozzle；0.20mm Standard @EP3 Plus；层高 0.2、首层 0.24 mm；3 槽均为 EP3-PLA悬垂优化 @EP3 Plus，槽色白/红/绿（模型使用后两色）；Cool Plate；支撑关闭；无用户对象参数覆盖。界面语言先简中，再按下表核对英文。加载时如提示缺失预设/自动更改参数，请先记录提示和实际配置，不直接视作同参测试。
- CLI 切片回归使用本地已展开配置 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/{machine,process,filament}_settings_1.config`；机器/工艺/耗材哈希分别为 `525f96226d82b47a33e5b6ee69bbda6d3291deb8855e967b013764b0d30df9ab`、`9f5cd82ce8a5f33995bb38cc36dab0677a225d43bcddbeeffc2ff1c60fb762ec`、`778e0f655f7c5895db968ed9e733b572b52be1ab4dd2cf75ea288e153951255f`。CLI 脚本对彩色 OBJ 使用两份耗材设置并按原入口映射，手测工程显式包含白/红/绿三槽；该槽位表达差异已记录，不能据此宣称逐字节路径相同。

### 操作与预期

每个独立场景先重新打开上述**原始工程**，不沿用上个场景修复后的模型。

| 场景 | 操作 | 预期与回传 | 状态 |
| --- | --- | --- | --- |
| 策略取消 | 准备页选择对象 → 右键高级修复 → 在新策略窗口点取消 | 不出现修复提交，缺顶面与红绿面不变；保存 `cancel.3mf` | 待 GUI |
| 保守修复 | 同一入口选“保守修复” | 显示无安全进展/仍有问题，说明不补洞；不封顶、不丢色、不删除对象；保存 `conservative.3mf`，截取提示 | 待 GUI；对应 CLI conservative-open-* |
| 标准修复 | 重新打开原始工程，选“标准修复” | 顶面补齐、旧红绿面保持；新增顶面可使用默认部件颜色，不要求推断新花纹 | 待 GUI；对应 CLI 彩色缺顶面修复/导出回读 |
| 撤销/重做 | 在标准修复完成后 Cmd+Z，再 Shift+Cmd+Z | 撤销恢复缺顶面与原着色；重做恢复补面；对象/盘归属不变 | 待 GUI；核心事务测试不替代交互 |
| 保存重开及切片 | 标准修复完成后另存 `standard.3mf`，关闭后重开；切片并导出 `standard.gcode` | 补面与颜色保留；10 mm 模型有连续挤出路径，不为空；有效配置变化和警告均回传。不要真实发送打印 | 待 GUI；CLI 有修复后切片，尚无此工程 GUI/CLI 同参路径比对 |
| 健康模型 | 新建工程导入 `tests/data/model_repair_acceptance/00-healthy-cube.obj`，选择同机型/工艺，运行保守修复 | 提示无需修复，几何与位置不变 | 待 GUI；对应 CLI conservative-healthy-* |
| 工作中取消 | 在需要耗时修复的实际模型上启动修复，进度窗口点取消；若瞬间完成，记为“未覆盖”而不是通过 | 取消后模型、颜色、选择和盘不变，不留下半个事务；保存取消后工程与完整日志 | 待 GUI；核心已有 worker/分壳取消覆盖 |
| 英文 | 切换英文、按应用提示重启，在原始工程再次打开策略窗口并选保守 | Conservative repair / Standard repair / Cancel 与拒绝原因英文完整；不执行自动重建 | 待 GUI；中英文源串与 MO 已检查 |

回传到 `planning/quality/smart-engine/gui-results/` 或用户指定目录：`gui-run.log`、`cancel.3mf`、`conservative.3mf`、`standard.3mf`、`standard.gcode`、策略及结果提示截图，注明操作时段。文件保存和重开是否成功须实际操作确认。

## 真实模型与仍不能安全自动处理的情况

- 用户原托架：`/Users/liwei/Library/Containers/com.tencent.xinWeChat/Data/Documents/xwechat_files/wxid_naol68t5y06922_46e9/msg/file/2026-09/(圆角)永久方案托架20260918.stl`，SHA-256 `5df90e7a3122aae5dbb09d5897b074ebc4db2f3197aa0c839741e4e66eebbca8`。
- 当前普通 CLI 标准模式：退出 0，862 面/114 开边 → 856 面/0 开边，`repaired / mesh=passed`；导出在 `planning/quality/smart-engine/bracket-standard/`。保守模式：进程 231 / JSON -25，无法证明表面保真，保留 862 面/114 开边，`unresolved / committed=false`，不输出冒充修复成功的 OBJ；证据 `bracket-conservative/result.json`。
- 上述真实模型首个试跑因隔离 datadir 的父目录未建而在应用初始化退出 134（未开始修复），日志 `bracket-{standard,conservative}.log`；创建父目录后重新运行的真实业务结果在 `bracket-{standard,conservative}-r2.log`。不将初始化错误解释为修复算法错误。
- 保守模式不处理需新增表面的开放孔、无法建立可靠源表面的非流形/退化组合，不能仅因部件小就自动删；标准模式仍可由用户确认采用。体素或 AI 重建尚未接入，不能宣称这些失败已解决。

## 后续实施边界

第一阶段尚需完成：特征前单位/变换/壳体方向准备契约，风险和差异的细化报告，以及 GUI 验收。本批不改变公开七项检查、正常壳体空腔方向和导入默认行为；也不声称局部坐标距离阈值能证明任意缩放下的世界尺寸保持。

第二阶段尚未实施：后端任务/产物身份、Blender v2 特征、有效配置解析/锁定和设备限制、可解释覆盖、两轮试切片闭环。已有后端 TemplateRuleEngine 不能当作该新闭环已完成。

第三阶段尚未实施：同一问题模型库的 Blender 局部/体素候选对照及固定策略准入；本轮只建立现有 CGAL 两策略的真实模型基线。

## 未验证项

- 自动化/CLI 以上述明确范围通过；GUI 全部待验证。尚未形成后端闭环、备选修复验收或实际质量提升的打印证据。
- Windows/Linux 与实际打印尚未验证；不得从 macOS 自动化推断通过。
