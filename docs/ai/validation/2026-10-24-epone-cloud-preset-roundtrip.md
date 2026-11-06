# EPONE 云端工艺导入参数丢失修复

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：在 master 修复云端导入差量工艺后线宽归零、树状支撑变普通的问题，完成检查后提交并推送。用户已明确授权本次提交和推送。
- 基准：master，`4a674a7114159769284dec045bfbcfdb75764ba0`；工作目录 `orca-slicer-master`。已 fetch 确认与 `codeup/master` 相同。
- 原有未提交修改：`2026-10-19-cli-fixed-position-slicing.md`，保持原样并排除提交；原分析分支工作目录不切换、不清理、不提交。
- 本次修改：导入工艺按 `Preset::print_options()` 优先读取工程有效配置，缺项才取嵌入预设；新增无 GUI 依赖的合成函数及工艺保存/重载回归。机器、耗材分支、上传格式、云协议和资源默认值不变。
- 当前状态：master 代码修复与自动化验证完成，回归 27 用例/1922 断言全部通过，CLI --help 退出 0；本记录随修复提交，目标远端 codeup/master，实际提交号/推送结果见 Git 和任务回执。此前分析分支的测试结果不作为本次 master 验证结果。
- CodeGraph：会话未暴露 MCP；已安装 CLI 位于 `~/.local/bin/codegraph`，本工作目录无 `.codegraph` 索引，按模块文档和源码定位，未初始化或复制其他目录索引。
- 验证：应用/测试构建和 CLI --help 已通过。首次回归发现测试夹具临时目录未初始化，修正后已重建复测通过（过程见下文）。GUI 真实云端往返和 Windows 验证保留待验。

## 原因与修复边界

用户附件 `71a5bd6b-719d-42b7-bdcb-6bb450f30ffd.3mf`，SHA256 `69aaea52e05f70b02c03285b86fb2938fa9a1e0a42bd11bf9fb45a6d3e22a1eb`。
其中 `project_settings.config` 保存完整有效配置，所选 `process_settings_1.config` 只有 36 个 JSON 字段，继承 `0.20mm Standard @EPONE`；仅外墙线宽显式为 0.45，其他七项线宽和支撑类型没有显式保存。

原导入优先取嵌入工艺 config，没有展开继承，也没有优先取工程快照；新建工艺缺项落到通用默认值，替换工艺缺项可能保留本地旧值。修复后新建、替换、另存使用同一有效工艺：工程值优先，只有缺键才从嵌入工艺补充，合法的 0/百分比保持原值。继续保留现有本地名称、机型绑定和取消流程。

这项修复兼容已有差量/全量包。历史导出代码可以从工程预设取得差量，也可以克隆当前完整预设，同名优先保留前者；现有证据不足以证明各样例上传当时的具体状态，不将这种条件分析写成已确认的上传操作历史。

GUI 入口是 `Plater::priv::gfd_apply_prepared_cloud_config`；共享工艺合成为 `GFD::cloud_process_config`，持久化继续使用 `PresetCollection`。CLI 没有云方案导入管理命令，普通 CLI 预设加载和切片路径未改；CLI 启动检查不代替云业务验收。

## 验证记录

构建目录：本工作目录 `build/arm64`，Ninja Multi-Config / Release / arm64，SDK MacOSX26.5，最低系统 14.0，MeshLib OFF；依赖前缀沿用已配置的原仓库静态依赖。开始前未发现并行 CMake/Ninja/Clang 构建。

已执行 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 6`，退出 0；日志 `planning/epone-cloud-roundtrip/build.log`，后续增量检查见 `build-final.log`，夹具修正后重建见 `build-test-fixture.log`。已运行 `[EPONE],[3mf]`；检查完整/差量来源、工程显式值优先、旧包回退、隔离临时用户目录中的新建/替换/另存与重载。

## GUI 验证清单（待执行）

使用本工作目录 `build/arm64/src/Release/OrcaSlicer.app` 的本次构建，不能用其他分支或旧打包副本代替。

1. 在 EPONE 0.4 喷嘴下导入附件对应的“树状支撑、常规生成、15%填充”云方案，手动切到导入工艺（现有导入行为会恢复导入前选择）。八项线宽应依次为 0.42、0.5、0.45、0.42、0.45、0.42、0.42、0.4；首层高度 0.24，支撑类型树状(自动)，填充 15%。
2. 将该本地工艺改为其他线宽和普通支撑，再导入同方案选择“替换”，应恢复上述数值；重复导入选择“另存为”也应一致。
3. 保存并重启应用、重新选中该工艺，核对数值；保存项目并重新打开后核对相同参数。取消替换应保留原本地参数。
4. 导入原本正常的 EPONE Plus/Pro 全量方案，应保持云包原值；显式线宽 0 或百分比也应保留。中英文界面均无新增文案，本轮未改机型资源。

回传本次应用路径、操作时间、质量/支撑截图及重新保存的 3MF；日志不包含登录凭据。真实上传/导入端到端与 Windows 需单独验证，不能以单元测试代替。

### 首次回归与测试夹具修正

应用和测试构建退出 0；最终增量检查仅执行两个编码检查。`[EPONE],[3mf]` 首次退出 2，27 用例中 26 通过、1 个新增归档用例失败（2 个差量/全量分支断言）；原因是 Model::read_from_archive 内部新建 Model 使用未初始化的全局 temporary_dir，尝试写 `/orcaslicer_model` 而失败。没有放宽断言，改用现有 ModelBackupPathGuard 给 BBS 解析器提供隔离临时目录，沿用云导入的 LoadConfig/CheckVersion 标志。此为测试夹具修正，业务代码未改；初次日志保留在 `regression-tests.log`，复测另存。

### 最终自动化结果与身份

- 夹具修正后重建仅包含 test_3mf 对象和测试程序链接，应用源码/程序未变化；构建退出 0。
- `libslic3r_tests '[EPONE],[3mf]'`：27 用例、1922 断言全部通过，退出 0；日志 `planning/epone-cloud-roundtrip/regression-tests-final.log`。包括实际 BBS 3MF 解析差量/全量、显式零/百分比、缺项回退及临时用户预设新建/替换/另存重载。
- 当前应用 `--help` 退出 0，日志 `cli-help.log`；CLI 无云管理命令，此项只证明启动。
- `python3 scripts/check_ai_module_docs.py --all`：35 模块、0 问题；差异检查通过。选择性暂存后 `--staged` 模块检查及 `git diff --cached --check` 均通过；原有无关文档、运行日志和 result.json 未纳入提交。
- 构建前核对源码/资源身份，测试前再次验证；完整 SHA256 记录在 `planning/epone-cloud-roundtrip/identity.json`。
- build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer: `3915c50fa7c35777d4d46ad27415e2a72d26433d41de71021732ea4297ae5528`。
- build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests: `d2c668d99d8c10d98853bd9c04b18490d49f2570be6b790430add9256717fbd4`。
- src/slic3r/GUI/Plater.cpp: `6ddc0c05fb81f1bae2c75dd79324fe55a99ff5c6efaf5db834a893e1d210e793`。
- src/slic3r/Utils/GFDCloudPreset.hpp: `5e774b9810e6ded63966ce7d9a044132ca9ebefdc059d6c707034cbfe14f3432`。
- tests/libslic3r/test_epone_presets.cpp: `3a25f9eb82676891182118a2e2deb69534120dd168d226c1191e61a5775bb33d`。
- tests/libslic3r/test_3mf.cpp: `788338ef95a9d2b4708244406d9daa7e47243db51d4436881533832f8558801e`。
- 未验证：真实云端上传/导入交互、GUI替换/取消/重启和 Windows 构建；GUI 操作步骤见上文。自动化不代替这些验收。


## 合并前本地诊断记录（历史归档）

以下为快进合并前同路径未跟踪文件的原始内容，保留分析和未执行事项的当时时点；当前已提交修复与验证状态以上文及集中测试合并记录为准。

# EPONE 云端工艺导入参数丢失

> 2026-10-26 分支状态：本修复已独立保存在 master 提交 `20d621103a`。用户决定后续统一合并，本 feature 分支已移除其未提交副本（实现及专属测试）；下文为当时验证历史，不能视为本分支当前包含该修复。

## 当前执行状态

- 目标：核对用户提供的树状支撑、15% 填充 3MF，修正云导入丢失继承参数的问题；不改云端数据、切片默认值或用户本地预设。
- 基准：`codex/model-feature-analysis-20261018`，HEAD `f9bcf0afe0abc42f327828342b168a806e5763f8`。开始时已有模块文档、分析记录、下载脚本及 result.json 改动，均保留。
- 已确认：完整 project_settings 有正确值；process_settings_1 是继承 `0.20mm Standard @EPONE` 的差量，仅外墙线宽显式为 0.45，缺其余线宽与 support_type。
- 源码：云导入优先采用嵌入差量预设；新建工艺以通用默认预设补齐，替换工艺则残留本地值。两者都没有优先恢复工程完整有效值。
- 当前步骤：已修改工艺合成，新增无 GUI 依赖的 `GFDCloudPreset.hpp`，按工艺键优先采用完整工程值，缺项才取嵌入预设。新建/替换/另存共用；不改机器/耗材导入分支、云协议和默认值。
- 测试已增加：原样关键值、已有本地错误值、显式 0/百分比、完整/差量单独输入、不同本地父预设下保存重载。机器/耗材分支也优先取嵌入预设，但本次用户证据与修复范围是工艺；未验证其完整往返一致性。
- 构建已通过：`cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2`，日志 `planning/epone-cloud-roundtrip/build.log`。已核对本目录、arm64、多配置 Release、无其他构建进程；CMake 自动重生成后有 619 项待构建。确认本机 64 GiB/16 逻辑核后，为缩短等待在约 104 项后中断（退出130），以相同目标 `--parallel 6` 续建，日志 `planning/epone-cloud-roundtrip/build-resume.log`；未并行启动两个构建。
- 最后验证：arm64 Release 应用/测试构建退出0，`[EPONE]` 7用例/1287断言通过（包含新增CloudProcess用例的4个分支），应用 `--help` 退出0，文档和差异检查通过。
- master 交付：独立 master 工作目录已提交并推送 `20d621103a` 到 codeup/master，该目录重新构建后 27 用例/1922 断言通过。本分析分支仍保留原未提交改动，两个分支的验证记录分开。
- 当前追查：继续只读核对上传结果不同的原因，重点是包内元数据、预设状态和导出调用链。GUI 真实云端往返仍待验证。

## 输入与证据

### 上传来源状态的进一步核对

已核对 master `20d621103a` 与本目录的 Plater.cpp、Preset.cpp、PresetBundle.cpp 内容相同。

- 各样例的 Application 均为 `BambuStudio-2.3.2.2-Release`；EPONE 嵌入预设版本 2.3.2.67，Plus 为 2.3.2.81。预设版本不等同上传应用版本。
- EPONE 包含两份工艺、两份耗材、两份机器，顺序为工艺→耗材→机器且均为差量；Plus/当前Pro各一份，顺序为工艺→机器→耗材且为完整值，分别吻合工程收集和当前预设追加顺序。顺序只是辅助证据。
- Plus 完整工程包含 `inherits_group[0]=0.20mm Standard @EPONE Plus` 及非空 `different_settings_to_system[0]`。`full_fff_config` 在解析到父预设后才计算该差异列表；当前资源存在此精确名称且无 renamed_from。在当前代码及同一稳定状态下，这支持完整克隆路径，而非父预设缺失回退。
- 决定状态的代码：`get_project_embedded_presets` 只收集 `is_project_embedded=true`；`save_current_preset` 新建时按 `save_to_project` 设置该标记；正常打开工程的 `load_project_embedded_presets` 遇到本地同名项会跳过，后续加载可以复用本地项。因此同样打开3MF、点击上传，也会受是否已有同名本地预设及是否另存本地影响。
- 结合包内信息反推：EPONE 36键差量符合工程嵌入分支，同名去重丢弃后补完整克隆；Plus 350键及其父项差异元数据支持从当前本地预设完整克隆进入归档。此为源码和文件证据推断，不是上传时变量日志，也不证明当时具体点击了哪一步。
- 没有按 EPONE/Plus 机型选择保存格式的分支；差异取决于预设来源状态。master 提交修复了导入兼容，未统一上传格式。

输入：`/Users/liwei/Downloads/71a5bd6b-719d-42b7-bdcb-6bb450f30ffd.3mf`。
SHA256：`69aaea52e05f70b02c03285b86fb2938fa9a1e0a42bd11bf9fb45a6d3e22a1eb`；17 个归档条目，完整工程 JSON 581 键，第一份差量工艺 JSON 36 键。
工艺：`EPONE 树状支撑 常规生成-15%填充-1022`。
按当前 `Preset::print_options()` 核对，完整工程含 344 个工艺键，差量含 32 个，其中 313 个有效工艺键只在完整工程内；这不意味着 313 项都与默认值不同。字段证据在 `planning/epone-cloud-roundtrip/sample-fields.json`。
完整参数：线宽依次为 0.42、0.5、0.45、0.42、0.45、0.42、0.42、0.4 mm；首层层高 0.24 mm；support_type=`tree(auto)`，support_style=`default`，sparse_infill_density=`15%`。
包内另有第二份“基础-无支撑无垫层”工艺，不能按第一条/最后一条无条件代替工程当前工艺。

CodeGraph：本会话未暴露 MCP；项目配置指定的 `~/.local/bin/codegraph` 可用，索引存在。普通 Python SQLite 只读连接失败（unable to open database file），CLI status 可读；immutable 只读核对 Plater.cpp、Preset.cpp、bbs_3mf.cpp SHA256 均匹配当前源码。结构查询已使用 CLI callees，但解析结果存在同名误匹配，结论以源码为准，未重建索引。

## 两端接入

GUI 云方案导入：`prepare_cloud_config_import_with_token` → 共享 3MF 读取 → `gfd_apply_prepared_cloud_config` → 新建/替换/另存工艺。上传通过共享 export_3mf 同时保存完整工程参数与嵌入差量。
CLI 没有云方案管理命令；普通 3MF 解析与配置类型共用 libslic3r。拟修正云导入的工艺合成，不修改普通 CLI 切片规则。

## 引入提交追溯

已通过 `git log -S`、`git blame HEAD` 及提交/父提交源码对照，确认本次错误优先级引入于 `9606d3661c64780f834ebfd846d7e35fed033c46`（2026-09-08 14:51:09 +08:00，`feat: 云端导入模块逻辑重构`）。该提交新增“只要有嵌入工艺就使用其 config，否则才用完整工程”的选择逻辑，并在新建工艺时以通用默认预设补齐缺项。

其父提交的 `gfd_apply_prepared_cloud_config` 在存在完整工程参数时，经 `load_config_model(..., config_loaded, ...)` 应用工程快照；本次重构移除了这条应用路径。当前 HEAD 对应选择逻辑各行仍归属 9月8日该提交，后续机型隔离/工艺命名改造保留了它。此结论为提交差异与源码追溯，未构建运行历史版本，也不推定该提交所有其他行为都有问题。

### 为什么导出有差量也有全量

继续追溯确认：两种导出来源并存不是9月8日才新增，而是4月22日 `b9754332a7`（`feat: 云端导入、上传配置以及ui相关调整`）引入的 `collect_current_project_export_presets` / `build_project_export_preset` / `append_project_export_preset` 组合。9月8日改的是下游导入策略，并未修改这些导出函数。

1. 先通过 `get_current_project_embedded_presets` 收集已标记 `is_project_embedded` 的工程预设。底层既有 `get_preset_differed_for_save` 如果找到其父预设，输出 `preset.config.diff(parent.config)` 的差量；没有父预设则保留完整副本。此底层机制源于既有3MF继承格式。
2. 再完整复制当前编辑的工艺/机器及使用中的耗材，准备追加到导出列表。
3. 按“类型+名称”去重。若第一步已有同名工程预设，第二步的完整副本会被删除，保留先收集的差量；没有同名条目时，完整副本才会追加。最终3MF写入器直接序列化各条目的config，不再次统一展开继承。

因此，同一机型乃至相同参数，使用工程嵌入预设且父预设可解析时可导出差量；使用普通本地预设、未命中前面的同名条目，或父预设不可解析时可导出全量。差量与全量可同包共存；`inherits` 非空本身不能判断是差量，当前Plus/Pro全量包也有inherits。完整 `project_settings.config` 是另一路工程有效快照，本次三个样例均包含它。

缺陷形成链：4月22日导出保留两种表示 → 9月8日导入改为优先直接采用嵌入config、未展开继承/未优先完整快照 → 差量包缺项落到通用默认值。已确认代码条件和包内结果；缺上传方操作日志，仍不把样例反推为确定的点击历史。本次修复处理导入兼容，不改导出格式。

## 10月24日 EPONE / Pro / Plus 差异追查

用户反馈 Pro/Plus 正常并要求检查刚刚的日志。本节只读日志、下载日志明确引用的配置文件并核对本地预设；未写云端或本地真实预设，未新增业务改动。

- 当前运行进程 PID 32476 启动于10月23日17:19:40，路径为旧打包副本 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，不是本轮新构建。因此11:57的Plus结果来自旧逻辑。
- 当前日志：`~/Library/Application Support/OrcaSlicer/log/debug_Fri_Oct_23_17_19_41_32476.log.0`。15360/15434/15438行记录11:34:38导入EPONE方案，完整解析577项、6份嵌入预设，新建本地工艺；17810/17874/17878行记录11:57:15–16导入Plus方案，完整解析577项、3份嵌入预设，新建本地工艺。
- EPONE配置文件与用户附件同名。选中工艺 `process_settings_1.config` 只有36个JSON键，缺7项线宽和support_type；新建本地文件实际写入线宽0、普通自动支撑、首层0.2。日志中这次测试当前机器是EP3 Plus，所以文件绑定EP3 Plus；不改变差量缺项的根因。
- Plus当前文件 `f860a5a7-b378-41e5-adc2-3a4ea31b8467.3mf` 的选中工艺有350键，八项线宽、tree(auto)、首层0.24均已显式写入，与完整工程一致；本地11:57:16保存的Plus工艺也确认这些值。故它在同一旧逻辑下没有触发缺项补默认的问题。
- 三款内置资源并无“Pro/Plus特殊导入线宽”规则；Pro/Plus标准工艺都继承EPONE标准工艺。导出代码确实存在两种输入形态：已有工程嵌入工艺经 `get_preset_differed_for_save` 生成差量；当前编辑工艺被完整克隆。同名时 `append_project_export_preset` 保留已有条目，所以最终包可含差量或完整工艺，取决于来源状态。包结构差异已确认，但缺上传方日志，不能确定这些包各自的完整操作历史。
- 当前日志未找到刚刚的Pro导入。10月22日14:11:46列表日志引用的Pro树状15%文件 `a5fa88e9-9a2c-4552-be12-6a57f9124173.3mf` 中匹配的 `process_settings_6.config` 仅36键，同样缺继承线宽/支撑类型；按旧逻辑新建仍有风险。不能据此断言用户现在的Pro方案已坏或已正常，需当前实际导入文件。
- 另核对10月22日14:12:03实际导入的Pro无支撑15%文件 `3b6aa3a1-5a27-4a1d-8ad4-8f7acda141fb.3mf`：工艺350键，但完整工程和工艺都已是线宽0/普通类型/首层0.2；它只能说明导入前已有这些值，不能作为“树状15%正常”的证据，也不能用此次修复推断恢复上传前未知值。
- 三类包的SHA256、匹配项与字段对照已保存至 `planning/epone-cloud-roundtrip/machine-comparison.json`，未保存认证信息或签名URL。已请用户再导入当前Pro树状15%方案，以读取最新事件和文件。

### 用户补做 Pro 后的确认

用户确认“已操作”。同一日志18394/18456/18460行新增12:01:58–59事件：方案ID `2000000013`，实际选择的是 **EPONE Pro 树状支撑 仅构建板生成-15%填充**，文件 `ec729f28-90bf-4424-9c09-ed02656572c3.3mf`。下载后SHA256为 `4a6a3a7eaf4db6746a3b3e7ec55b5d8f1add8b2662ed17ec4e43c526d654eb6a`。该包选中工艺350键，八项线宽、tree(auto)、首层0.24、15%填充及仅构建板开关1均显式存在；本地新建工艺文件也一致。与10月22日的“常规生成”旧包是不同方案，不能混用结论。

本次三机型差异已明确：EPONE当前包的36键差量触发旧导入缺项补默认；Plus与当前Pro包的350键完整工艺避免了该触发条件。问题取决于文件内容/来源，而非机型有单独的安全分支。当前Pro/Plus导入来自同一个10月23日已启动的旧打包程序，不是本次修复生效后的结果。

逐键核对当前包的工程有效配置与实际导入后的本地文件时，排除名称、inherits及机器绑定键；每份按当前源码工艺键列表比较344项JSON序列化值，EPONE有65项差异，Plus与当前Pro均为0项差异。65是文件值差异计数，不代替逐项兼容性/切片行为验收。比较与差异保存在 `planning/epone-cloud-roundtrip/current-import-comparison.json`。该文件只含相关工艺配置与文件身份，无认证信息。后续仍需按前述GUI清单验证新构建导入EPONE；本次读取旧进程的正确Plus/Pro结果不替代修复后验收。

C++ 修改已用临时隔离环境中的 clang-format 18.1.8 按变更行格式化；未全文件格式化既有源码。模块全量检查与 `git diff --check` 已通过；尚未暂存/提交。

## 验证状态

静态文件核对、应用构建与自动化已完成。GUI 上传/导入/重启重载待验证；不能以包内数据正确或自动化通过推定真实云端端到端通过。

## GUI 操作清单（待执行）

使用本目录 `build/arm64/src/Release/OrcaSlicer.app` 的已完成新构建，不要使用旧的 `build/arm64/OrcaSlicer/OrcaSlicer.app` 打包副本。Windows 需以相同源码另行构建，本轮不代替 Windows 验收。

1. 打开上述 3MF，选择 EPONE 0.4 喷嘴和“EPONE 树状支撑 常规生成-15%填充-1022”工艺；确认全局参数为上述八项线宽、首层 0.24、树状(自动)、填充 15%。检查无对象覆盖干扰。
2. 以单独测试方案名上传标准档，再从云端导入。按既有行为导入后恢复之前工艺，需要手动切到新导入的工艺。逐项核对上述参数。
3. 在本地将导入工艺改成普通支撑和不同线宽并保存，再导入同一云档位，在工艺行选“替换”。应恢复云端完整值；再重复导入并“另存为”另一名称，也应一致。
4. 保存项目、重启应用并重新选中工艺，确认全部值不变；重新打开保存项目，再核对支撑类型/填充/线宽。合法的线宽 0 或百分比若是上传时显式设置，应原样保留。
5. 取消工艺替换后确认原工艺未改变；现有其他档位、EPONE Plus 工艺未受影响。补做英文界面对应参数检查，本次无新增文案。

回传：新构建启动路径、操作时间段、导入前/后“质量”和“支撑”截图，以及重新保存的 3MF；日志取该次运行的应用日志。不得上传或记录登录 token。CLI 没有云管理命令，自动化覆盖实际工艺合成与预设持久化，不声称真实网络往返已通过。


## 自动化与构建结果

- 基准 HEAD 加本次未提交业务改动：Plater 工艺配置来源切换、新增 GFDCloudPreset.hpp、GUI CMake头文件登记、test_epone_presets 回归扩展。未改资源、默认值、原3MF或真实本地预设。
- 构建：本目录 `build/arm64`，Ninja Multi-Config / Release / arm64，SDK MacOSX26.5，deployment target 14.0；沿用既有依赖缓存。续建退出0；最终再次调用同一目标仅运行两个编码检查，无额外编译/链接，退出0，证据 `build-resume.log` / `build-final.log`。
- 自动化命令：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests '[EPONE]'`，退出0，7用例/1287断言；日志 `planning/epone-cloud-roundtrip/epone-tests.log`。新用例包含默认/已有错误值合成、显式0/百分比、旧包兼容、新建/替换/另存后重载，使用隔离临时预设目录。
- CLI启动：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --help` 退出0；日志 `planning/epone-cloud-roundtrip/cli-help.log`。CLI无云方案管理命令，不将help计为云导入业务验证。
- 应用 SHA256：`b9deee3fb3942dbd0f471f97da3bcea4e46e1d87eef6ff96b0b147dcbb1a0b75`。
- 测试程序 SHA256：`f794b3f255e3e7fae66f7a11080ca17a1bf02dbb9844f88832a1431bf33634c1`。
- Plater.cpp SHA256：`6ddc0c05fb81f1bae2c75dd79324fe55a99ff5c6efaf5db834a893e1d210e793`；GFDCloudPreset.hpp：`5e774b9810e6ded63966ce7d9a044132ca9ebefdc059d6c707034cbfe14f3432`；测试源码：`03fa7f99c148d2e0c90d5ef1c2ab78d0ca177027981d8597bc4dfe49f4aa0f5b`。完整身份记录 `planning/epone-cloud-roundtrip/identity.json`。
- `python3 scripts/check_ai_module_docs.py --all`：35模块、0问题；`git diff --check`：退出0。未暂存/提交，因此无staged提交验收。
- CodeGraph 的一致性核对发生在修改前；之后以当前源码/差异审阅，不复用旧索引对修改位置作事实断言。
- 未验证：真实云端端到端、GUI参数显示/取消/重启操作、Windows构建与界面。机器/耗材的同类差量来源行为不在本次修复范围，仍需独立样例验证。
