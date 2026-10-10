# adv3 加入 Custom 的可行性分析

## 当前执行状态

- 2026-10-10 提交复核完成：用户确认分析的是 `orca-user-gui-run-20261010-152909-953828.log`，原消息的 152950 文件不存在。本次日志两次主窗口退出被模态保护拦截，设备框取消后父窗口恢复、15:29:49 完整析构退出，无新崩溃；没有实际下发记录。当前源码及应用与已通过测试的 manifest 一致，不重复构建。本次提交纳入 Advancer3 资源／向导／动态参数生命周期／QA 设备查询／打印设置／取消和退出修复，共 69 个文件；全工作区及选择性暂存的 35 模块检查、diff 检查、简中 PO 校验均通过。无关下载脚本、教材／模型研究记录、result.json 和日志产物留在工作区，不推送。Windows 与新设置实机效果仍按文末清单待验，下方“未提交”为历史阶段状态。

- 2026-10-10 用户续修已编码、未提交：调平默认开启；主动取消改为中性状态；13:51 退出 SIGABRT 定位为模态未退栈时主窗口销毁栈上设备选择框，增加关闭守卫。新日志 `orca-user-gui-run-20261010-135727-329531.log` 证实用户 14:06 四槽位下发已被后端接收。arm64 Release 编译／打包、7 用例 87 断言、wx 87 项检查、35 模块检查通过；完整应用 GUI 与 Windows 待验，保留其他未提交改动。以下旧轮默认关闭和待定位状态由文末续修覆盖，当前身份见 `cancel-leveling-manifest.json`。

- 2026-10-10 Advancer3 打印设置编码完成、未提交：实际 PMC 槽位、默认关闭调平、ms 请求、其他机型保留原协议。最终 arm64 Release 打包／6 项 85 断言／35 模块检查通过，UUID 98ECF6A5-CC4D-32D1-9ABE-A7EAC28B30A2；未实际打印。隔离 GUI 验证遇到旧会话验证码，退出又发生 Plater 子窗口销毁 SIGABRT，尚未定位；新设置窗口、Windows 和实机待验。下一步按文末清单手测，并独立定位退出异常。基准 master / 650b9c8a。

- 2026-10-10 用户已授权实施：仅 Advancer3 接打印前调平与实际 PMC 料站槽位，其他机型（包括 AD5X）沿用本分支逻辑；不迁移用户版其他业务。计划共用现有映射窗口，新增严格协议解析／请求构造与专项测试；保留异步取消、稳定请求 ID 和设备选择模态收尾。完成后最小构建／打包并交付 GUI 清单，不实际下发打印。

- 2026-10-10 打印选项分析已完成：当前 master 缺闪铸 G-code 槽位确认、`ms` 和显式调平参数；`master_v_user` AD5X 实现仅可参考，缺 Advancer3 且其通用耗材接口不是本次目标的有效槽位来源。QA 只读实测同一设备通用接口 slots=[]，PMC 状态返回 1～4 四个已装载 PLA 槽。建议按 Advancer3／AD5X 闪铸能力接入本次打印调平开关和真实槽位映射。仅分析、未改业务代码／未发送打印；详细来源与方案见文末，下一步待用户确认改造范围。

- 2026-10-10 用户已授权更换并核查范围：实施 QA 设备查询专用地址对齐 modelcenter，盘点认证、云配置、动态参数、上传授权和打印命令各自路由；沿用本轮已核实源码与接口证据，其他未提交改动保留。已完成最小构建、同会话只读接口对照及新 GUI 设备可见性检查；MAC 24A10DB2ADDC 已显示为 Advancer3／空闲，未勾选设备或发送打印。arm64 Release 打包完成，最终 UUID `1580E104-33B3-36EB-8AE9-E61432FAA319`；MAC 筛选、窗口关闭和隔离实例退出均通过，用户原实例保留。下一步为用户保存工程、退出旧实例后用原命令复测；Windows／真实打印未验证，代码未提交。
- 2026-10-10 设备查询续查已定位：同一 Orca 会话下，旧网关与 qa-print 的 `/app/print3d/manage/v1/md/query` 均不返回 MAC `24A10DB2ADDC`，qa-3dmodelcenter 的 `/app/print3dModel/manage/v1/md/query` 返回该 Advancer3、ready。只换成 qa-print 域名无效，差异在所选设备查询接口；正式环境代码已使用 modelcenter 专用查询、QA 当时尚未对齐。该条为更换前的只读定位，后续已按用户授权修正 QA 专用查询，证据与范围见文末。
- 2026-10-10 主界面耗材名称续修：用户截图显示 `Advancer3 0.6 nozzle` 的耗材仍为 `Generic PLA` 等简称。确认 `PlaterPresetComboBox::get_preset_name` 使用 alias，完整名称机型白名单漏了 Advancer3；上轮仅修向导／耗材管理网页，未覆盖此原生入口。本轮已补齐同一显示策略，最小 arm64 Release 构建／原脚本打包／35 模块检查通过；隔离 GUI 已目视确认 0.25 专用和 0.8 通用耗材均保留完整名，不改预设标识、参数或云端身份，CLI 不提供此下拉框。测试实例已退出；用户原进程未关闭，须重新启动才能加载修复。
- 2026-10-10 续接：修复用户反馈的喷嘴仅一项、耗材选择缺 Advancer3 标识及动态参数切换后崩溃。基准 `master` / `650b9c8a53d2edfff4649ed6374a97d50290e386`；保留其他文档、下载脚本和 `result.json` 的既有改动，不提交。
- 云端身份只读实测为 `Advancer3`，保持独立查询；AD5X 仅为本地参数来源。QA 设备查询路由按 10 月 10 日授权更换，其余域名保留，不发送打印／云端写入。
- 本轮已完成代码修改（未提交）：Custom 02.03.02.81，4 个喷嘴（0.25/0.4/0.6/0.8）、12 个工艺、6 个耗材预设；向导保留完整机型／喷嘴耗材名，按精确 key 保存并修正默认选择；`Tab::update_changed_ui` 延迟刷新改为由 Tab 本身排队，销毁时清理待处理事件。
- 最终构建、原脚本打包均退出 0；10 个 C++ 专项 / 1,681 条断言、12 个网页逻辑测试、隔离 profile validator、四规格 CLI 指令等价通过。打包文件和当前资源哈希一致，详见 `build/diagnostics/advancer3-followup/validation-manifest.json`。
- GUI 已验证：原 .80 的隔离用户目录升级后保留原 0.4 选择，Custom 四喷嘴可见并可启用，耗材管理显示六种完整标识；动态窗口打开、换机型、换耗材、切回及关闭未再崩溃。首次启动向导完整点击流程、快速连续操作、Windows/同事原机及实机仍待用户复测。以下清单给出具体步骤。
- 用户给出的 090609 日志不存在，实际证据为 `planning/debug/orca-user-gui-run-20261010-090520-028493.log`、`~/Library/Logs/DiagnosticReports/OrcaSlicer-2026-10-10-090614.ips`。早上 09:02 已打包的 .80 资源包含 Advancer3，本次不能归因于旧包。
- 崩溃捕获时间 09:06:09，HTTP 200 后主线程在 `TabCtrl::GetItemText -> Tab::update_changed_tree_ui` 访问损坏的按钮 vector 地址；与当前旧包 UUID `47D23B10-DEB6-3CE8-BC89-462116982461` 匹配。`wxTheApp->CallAfter([this])` 的全局待处理事件不随 Tab 销毁清除，原来的 parent 非空判定无法验证对象有效性，此缺陷已修正；尚不能凭堆栈证明用户那次销毁的具体时序。
- 旧包在隔离 datadir 中经 LLDB 测试：打开动态参数、切换 EP3Plus、换耗材、关闭弹窗，没有再次崩溃；不能把该未复现写成旧版正常。未修改真实用户配置、未云端保存。测试应用已退出。
- 以下 10 月 9 日 .80 的通过结果属于旧基准，不代表本轮 .81 和 C++/JS 修改已通过；最终验证记录将在本文件追加。

## 结论与范围

Custom 登记独立机型 `Advancer3`，以 AD5X 有效参数为基础生成本品牌可独立加载的机器、工艺、耗材资源，`gfd_device_type` 设置为 `Advancer3`，打印类型白名单同步登记 `Advancer3`。启动网页无需为该机型新增硬编码 UI。

本地名称、`printer_model`、`model_id`、`gfd_device_type` 分别承担显示／预设／协议角色，不应因复用参数而混用。不得把用户所说的 adv3 自动等同于仓库已有的 Flashforge Adventurer 3 Series；后者是另一套独立预设。

用户确认仅复用本地参数，现有 `gfd_device_type` 机制即可接入，无需新增后端接口或到 AD5X 的云端映射。设备查询、云配置查询／上传／保存保持 Advancer3 归属，不自动回退到 AD5X。

## 后台只读核对（2026-10-09）

- 用户现有 Chrome 页面 `qa-3dmodelcenter.wisebeginner3d.com/#/machineMng` 的 Advancer3 编辑表单中，“机型名称”和“机型编码”均为 `Advancer3`；仅查看，随后取消，没有提交保存。
- 点击该条目的“方案配置”，Network 观测 GET `/app/pds/manage/v1/slice-param-config/page?deviceType=Advancer3&sliceType=orca&pageNum=1&pageSize=10`，HTTP 200，页面共 11 条方案。
- 在已有 `qa-system3d.wisebeginner3d.com/#/sliceParams` 页打开 Network 并刷新，观测 `https://qa-print.wisebeginner3d.com/app/print3d/manage/v1/slice-param-config/device-and-slice-type` 返回 HTTP 200，`code:0,msg:success`，`data[8]` 为 `{deviceType:"Advancer3",deviceTypeTitle:"未知",…}`，相邻 AD5X 为独立对象。
- 使用请求类型值而非 `deviceTypeTitle`；后者“未知”不应变成显示名称或请求参数。没有读取／输出认证令牌，没有执行云端新增／修改／打印；关闭开发者工具，返回原机型管理页。
- 上述证据覆盖当前 QA 管理后台与 qa-print 返回，不声称线上或代码中尚未切换的 QA 网关已逐一联调。按用户既有要求不改域名。

共享场景：当前 GFD 设备选择代码解析接口返回的 MAC、使用人、类型、状态及设备 ID，再按条件筛选，没有独立的“共享设备”标志分支。打印命令仍以选中设备的 deviceId 等字段发往现有接口。添加 adv3 类型与本地预设不等于实现共享授权，也不证明当前接口一定返回全部共享设备；后续需用实际共享账号验证可见性、设备状态及下发结果，不扩大接口返回范围。

## 实施前的源码定位证据

| 环节 | 当前行为／必要处理 |
| --- | --- |
| Custom 入口 | `resources/profiles/Custom.json` 当前没有 AD5X 或 adv3；在 `machine_model_list`／`machine_list` 注册模型和喷嘴实例，补封面及床资源。网页 `resources/web/guide/21/21.js` 按 vendor 自动分组。 |
| 向导继承 | `GuideFrame::LoadProfileFamily` 为每个 vendor 建 `GuideMachineMetadataResolver`；解析器只查该 vendor 的 `machine_list`。不能在 Custom 中只写 `inherits: Flashforge AD5X 0.4 nozzle` 而缺本品牌父项。 |
| 已有 AD5X | Flashforge 品牌已有 0.25／0.4／0.6／0.8 喷嘴。0.4 机器继承 AD5M Pro，再继承多级公共项；直接复制单个 JSON 不等于复制完整有效配置。 |
| 工艺／耗材 | `0.20mm Standard @FF AD5X` 和 `Flashforge Generic PLA` 通过精确的 `compatible_printers` 限定 AD5X 名称。新 Custom 实例需要对应默认预设及兼容列表；内部名字／ID 不应与 Flashforge 重复。 |
| 后端类型 | `PresetBundle.cpp` 加载模型时写入模型声明的 `gfd_device_type`；`GFDConfig.cpp` 优先解析显式类型，适合保持 adv3 身份。 |
| 打印选择 | `Plater::priv::show_gfd_device_selection_dialog` 传入 `all_print_device_types()`；`GFDDeviceSelectionDialog::match_filters` 按白名单和精确类型过滤。必须同步 `resources/gfd_button_config.json`，仅增加机型可能仍看不到设备。 |
| 云配置 | `gfd_fetch_cloud_configs` 按返回 `deviceType` 精确筛选，没有 adv3→AD5X 映射。导入参数保留本地 `printer_model`、`printer_settings_id`、`gfd_device_type` 等身份键。 |
| 老版本升级 | Custom 当前资源版本 `02.03.02.79`；实际新增资源时更新版本并验证旧用户目录升级，不能只验证全新安装。 |

AD5X 0.4 资源当前声明 220×220×220、Klipper、单挤出机多材料及 AD5X 起止 G-code。这些是源码参数事实，不是 adv3 实机能力证明；复用范围应以用户确认的同配置设备为前提。首期可先提供 0.4 mm，其他喷嘴按实际设备需求添加。

## 实施后的验证要求

- 对解析继承后的机器／工艺／耗材有效配置做比较：除新身份、兼容列表和资源路径外，应符合选定 AD5X 基准；避免静默回落通用默认值。
- 用 profile validator、向导元数据解析及共享 PresetBundle 加载验证缺父项、默认配置、兼容性和重复名称。
- GUI 首次启动／已有用户添加设备：Custom 中能选择机型、默认耗材和工艺可用、切换机型和重启后选择保留；在隔离用户目录验证资源升级。
- GUI 与 CLI 使用同一有效预设、模型和参数切片，比较业务结果；CLI 没有云端设备选择界面，该界面验证单独执行。
- 使用真实接口返回核对设备列表和云配置分组；确认类型字符串、云导入后设备身份、上传／保存归属。不主动发起实机打印。

上述自动化加载与 CLI 对照现已执行，结果如下；GUI 与实际云端写入／打印仍待验收。

初始分析阶段仅文档变更，当时模块文档检查通过（35 个模块、0 个问题）；不以该阶段结论代替下列实施验证。

## 实现与自动化结果

- 首期机型 `Advancer3 0.4 nozzle`；3 个工艺为 `0.16mm Standard @Advancer3`、`0.20mm Standard @Advancer3`、`0.24mm Draft @Advancer3`；耗材为 `Generic PLA/PETG/ABS @Advancer3`。默认选择 0.20 mm、PLA。
- 机器／工艺／耗材 JSON 递归展开本仓库 Flashforge AD5X 的父项，再替换独立身份、默认引用和兼容列表；不引用另一个 vendor 的机器父项，不改共享切片算法。机器源文件默认工艺指向 AD5M Pro，本次明确改为 AD5X 对应的 0.20 mm 工艺，验证时使用这份 AD5X 工艺对照。
- 封面、床模型、床贴图、热端模型是现有 AD5X 资源的独立副本，不是另行核实的 Advancer3 外观图。按实际文件名处理上游 STL 大小写差异。`print_3mf=false` 沿用普通 G-code 下发能力，未擅自启用未经确认的 3MF 协议。
- Mac arm64 / Ninja Multi-Config / Release：构建 `libslic3r_tests` 与 `OrcaSlicer_profile_validator`。首轮测试代码误调用私有 `merge_presets`，编译失败；改为在临时目录复制已注册 JSON，通过公开 `load_presets` 验证多品牌共存。重建及格式化后的最终增量构建均退出 0。日志为 `build/diagnostics/advancer3-build.log`、`advancer3-build-retry.log`、`advancer3-build-final.log`。
- `xcrun clang-format -i tests/libslic3r/test_advancer3_presets.cpp` 已执行。专项过滤器 `'[Advancer3],[Guide],[EPONE]'`：10 个用例、1,416 条断言，退出 0；日志 `build/diagnostics/advancer3-tests.log`。新用例覆盖独立加载、全有效参数比较、兼容关系、品牌共存、向导元数据和资源引用。
- Profile validator 使用 `build/diagnostics/advancer3-validator/profiles` 的独立资源副本（Custom + OrcaFilamentLibrary），`-v Custom -l 2`，加载 2 个 vendor，退出 0。没有向源码资源目录或用户真实配置目录写缓存。
- CLI 使用 `tests/data/model_repair_acceptance/00-healthy-cube.stl`（10 mm 方块），两组均显式指定机器、0.20 mm 工艺和 PLA，使用独立 datadir/outputdir。首次命令同时给出 outputdir 与绝对 export-3mf 路径导致路径拼接重复，退出 243；改为 `--export-3mf cube.gcode.3mf` 后双方退出 0，不是预设修复。
- CLI 双方均 50 层，2,990 条去除注释后的可执行 G-code 逐条完全一致，SHA256 `9b9f9db42f84cf9419cd84dceb79a2dee6057d488cd6daa6c8c33994c9b5a889`。3MF 中有效配置仅 9 个身份／引用字段不同（含序列化的 filament_ids、print_compatible_printers）；全部列在 `build/diagnostics/advancer3-cli/comparison.json`。双方都有源 AD5X 单点排除区 `0x0` 引起的 `calc_exclude_triangles` 日志，未新增该参数差异，未据此扩大修改范围。
- 本次没有修改应用 C++ 代码，CLI 复用已存在的同仓库应用，显式加载本轮 JSON；开发应用 Resources 指向当前仓库资源。应用 SHA256 `c857ac450586947981d80cd9b933123bb73ad0a628c2e67577be37588fd86282`；测试及 validator 为本轮重建，身份与全部资源哈希保存于 `build/diagnostics/advancer3-validation-manifest.json`，CLI 参数和输入身份保存于 `build/diagnostics/advancer3-cli/manifest.json`。

## GUI 验证交接（待用户操作）

应用启动路径：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`（相对本仓库根目录）。它的 Resources 是仓库 resources 的链接，已包含本次资源；当前运行的 `build/arm64/OrcaSlicer/OrcaSlicer.app` 是另一份打包副本，未覆盖它，不能用旧包证明新资源已生效。关闭旧应用前先保存自己的工程。

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
```

| 场景 | 操作 | 预期 | 当前状态 |
| --- | --- | --- | --- |
| 已有配置升级／添加 | 用上面的应用打开打印机添加向导，在 Custom 勾选 Advancer3 的 0.4 mm，完成向导；不要清空原用户目录 | Advancer3 出现，原已启用机型保留；没有缺父项／无默认耗材提示 | 待 GUI |
| 默认和兼容 | 在主界面选 Advancer3 0.4 nozzle，查看工艺和耗材；切换 EPONE 后再切回 | 默认 0.20mm Standard、Generic PLA；能选本机型 3 工艺和 3 耗材，原机型参数不被替换 | 待 GUI，加载自动化已过 |
| 切片对照 | 导入仓库 `tests/data/model_repair_acceptance/00-healthy-cube.stl`，平台不改，0.20 mm／PLA，切片 | 220×220×220 平台；50 层，路径应与 CLI 对照；如有 GUI 自动参数修正则回传实际 3MF 再比 | 待 GUI，CLI 已过 |
| 选择保持 | 保存工程，退出并重启新应用 | 机型和选中参数保持；再次添加向导中显示已勾选 | 待 GUI |
| 云方案 | 登录原环境，点云端导入，选择 Advancer3 | 按 `Advancer3` 获取方案，不查询小写 adv3 或 AD5X；空结果／失败需按具体网关记录 | 待 Orca GUI 联调，QA 后台只读核对已过 |
| 上传／保存 | 使用明确的测试方案验证标准、快速、精细，确认目标 Advancer3 后操作 | 方案归属 Advancer3，其他机型／其他档位不被替换 | 待用户明确测试数据后操作 |
| 共享设备 | 打开下发打印，类型选 Advancer3，查找已知共享测试设备 | 能看到接口返回的对应设备、MAC／使用人／状态正确；实际打印由用户安排 | 待 GUI／实机 |

回传应用启动路径、操作时间段、相关日志；切片比较另附导出的 3MF。Windows 同步后使用包含 02.03.02.80 资源的新包执行同一清单。CLI 不提供共享设备管理界面，相关 UI 验收单独记录；共享预设加载和本地切片规则两端共用。

## 工具及身份

CodeGraph MCP 未暴露，CLI `/Users/liwei/.local/bin/codegraph` 可用，索引存在。`status` 不能证明全量新鲜：核对 SHA256 后，WebGuideDialog、GuideMachineMetadata、PresetBundle、ConfigWizard 与索引一致，GFDConfig 不一致。已用 context 辅助定位向导；GFD 类型链回到当前源码，不使用过期图推断。SQLite 普通只读打开失败，immutable 只读打开成功；未重建索引。

下表为分析阶段的旧基准；当前实现身份以上面的 validation manifest 为准。

| 关键文件 | 分析基线 SHA256 |
| --- | --- |
| `resources/profiles/Custom.json` | `fd3b628c5f716a3cf2fd7b2b5d1ce6174bf386c9d978dee4c1d8b7f79491ba64` |
| `resources/gfd_button_config.json` | `cb30249dc585b5b9cf811bb67cdd43e0c91e9d53d5669c5127fe881c0996b9dd` |
| `resources/profiles/Flashforge/machine/Flashforge AD5X 0.4 nozzle.json` | `7c64b42824f8da0523f2da29b47ed3e9297b946824b8f0f39b01720d8b58139c` |
| `src/slic3r/Utils/GFDConfig.cpp` | `d821efbc610c3a143771b0fdb2d9370a0fb3eae48a41ca24c48fd43c54a70d25` |

### 10 月 10 日打包与复测进行中

使用现有 `build_release_macos.sh -s -a arm64 -b -c Release`，环境 `SLICER_CMAKE_GENERATOR=Ninja Multi-Config SLICER_BUILD_TARGET=OrcaSlicer CMAKE_BUILD_PARALLEL_LEVEL=4`，仅构建当前应用后按原脚本更新用户所用打包路径；日志 `build/diagnostics/advancer3-followup/package.log`。无并行编译进程。

### 10 月 10 日阶段验证结果

- C++ 专项最终 10 例 / 1,681 断言通过；Custom 与 OrcaFilamentLibrary 的隔离 profile validator 成功。
- 页面 22（首次向导）和 23（耗材管理）均改为共用 `guide/js/filament_selection.js`，显示和保存完整预设身份；两入口共 12 个 Node 测试通过。实际 GUI 验证发现第二个入口原先仍去掉 @ 后缀，已同步修正，正在重新打包网页资源。
- CLI 四规格均退出 0，10 mm 方块各与对应 AD5X 可执行指令逐条相同：0.25=6,761、0.4=2,990、0.6=1,875、0.8=1,176 条。证据为 `build/diagnostics/advancer3-followup/cli/manifest.json`。首次对照脚本把 OrcaFilamentLibrary 的同名基础耗材覆盖了 Flashforge 本品牌基础项，产生热床温度差异；修正诊断脚本优先使用 Flashforge 本品牌继承后全部一致，产品参数没有为消除差异而修改。
- 新 C++ 构建 GUI：动态参数打开成功，红色耗材 Advancer3 无远端参数时正常回退；改 EP3Plus 后加载远端 210/200 ℃；切换紫色耗材，再切回 Advancer3、关闭窗口均未崩溃。日志 `build/diagnostics/advancer3-followup/gui-new.log`。只读操作、不保存云端、不下发打印。此为有限场景回归，原偶发崩溃未稳定复现，不声称其全部时序已覆盖。
- 由于 .80 之后新增了共享网页文件，执行第二次相同最小打包命令，日志 `build/diagnostics/advancer3-followup/package-final.log`，随后检验两个页面的实际显示。

## 10 月 10 日交付与复测清单

最终应用：`build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，arm64 Release，UUID `28730459-03CA-3ADF-A85A-9FE20885AD6D`；Custom `02.03.02.81`。应用 SHA256 `f7d0255c6afae5dad9db974838f984a1680439e4fbddfce4c27c1c0ef61fca9a`。C++ 修复后实际 GUI 测试所用的程序和最终程序相同；最后一次打包仅同步两耗材网页及共用 helper。无需先清空用户配置，也不以旧安装版替代此构建。

GUI/CLI 对照：预设都走 PresetBundle，全部机器／工艺／材料参数经同源加载比较；网页列表和 Tab 的延迟刷新仅存在于 GUI，CLI 没有对应窗口。四规格 CLI 对照均明确指定机型、该规格 Standard 工艺、PLA，输入 `tests/data/model_repair_acceptance/00-healthy-cube.stl`，无对象变换和覆盖。命令／哈希／输出写在 `build/diagnostics/advancer3-followup/cli/manifest.json`。

| 场景 | 操作 | 预期 | 状态 |
| --- | --- | --- | --- |
| 已有安装升级 | 用上述新构建启动，打开打印机配置，搜索 Advancer3 | 显示 0.25/0.4/0.6/0.8，原来的 0.4 勾选保留 | 隔离 .80 配置已通过 |
| 完整耗材标识 | 在隔离配置启用全部四喷嘴，再打开“配置可选择的材料” | PLA/PETG/ABS 均有 `@Advancer3`，0.25 专用项名称含 `0.25 nozzle`，既有选择保持 | 实际页面23已通过 |
| 首次向导 | 新安装或测试数据目录首次启动，Custom 勾选 Advancer3 四喷嘴，再进耗材页 | 完整名称显示，默认耗材选中；仅勾选所需项目后完成，不额外选中其他型号同名耗材 | 页面22逻辑测试通过，完整 GUI 点击待用户 |
| 新喷嘴默认值 | 主界面依次选择四个 Advancer3 喷嘴，观察工艺/耗材 | 默认 Standard 层高对应0.12/0.20/0.30/0.40；0.25 对应专用耗材，其余普通项 | 共享配置/CLI通过，逐一GUI切片待用户 |
| 动态参数切换 | Advancer3 0.4 + Generic PLA 打开“动态参数”；切换两种耗材，再来回切换 Advancer3/EP3Plus，关闭并重新打开 | 有该机型参数则刷新；缺少时明确提示保留本地值；主界面和关闭按钮可用，无闪退 | AI已做有限切换/关闭；请用户复测原操作节奏（尤其快速连续切换与切换后立即关闭） |
| 共享设备/Windows | Windows 同步同一代码资源后重复动态切换，确认云操作仍为 Advancer3 | 不映射 AD5X；共享设备可见性和真实打印按原业务 | 本轮未操作Windows或实机，不计入通过 |

复测可继续使用原启动命令，把日志文件名换为新的时间；若出现异常，保留新日志和 macOS DiagnosticReports 中对应时间的 `.ips`，并注明最后切换的耗材／机型。AI 没有向云端保存参数、下发打印或修改真实用户配置；隔离应用已退出。

残余日志：缺少可选 3Dconnexion 驱动，以及 AD5X 源参数 `0x0` 排除区的 `calc_exclude_triangles` 提示，旧包及源 AD5X 对照也存在，和本次失效 TabCtrl 堆栈不同；本轮未扩展修改这些项目。

结论：完整机型/耗材接入与选择问题已修正，针对崩溃堆栈暴露的全局延迟回调生命周期缺陷已修正并通过上述有限回归；未能稳定重现原偶发崩溃，不宣称其全部触发时序已经验证。代码未提交、未推送。模块文档及 GUI 清单已同步。


### 主界面耗材名称补齐（2026-10-10）

- 根因：网页向导与原生下拉框是不同入口。原生 `gfd_filament_displays_full_name` 白名单缺 Advancer3，`preset.label(false)` 展示自动生成的 `Generic PLA` alias。仅向导完整名修复不能覆盖此处；本轮增加精确机型 Advancer3。
- 选择映射：`get_preset_name_by_alias` 在不匹配 alias 时返回传入完整名；耗材编辑页原本也使用 `label(true)`。本次未修改名称、ID、兼容表或参数；GUI 标签变更不涉及 CLI 业务，未重复执行上轮四规格切片对照。
- 验证：`cmake --build build/arm64 --config Release --target OrcaSlicer -j 4` 退出 0；复用项目打包脚本，限定 OrcaSlicer 目标、arm64 Release，退出 0。最终打包程序 UUID `F2EF8C53-5D8E-3210-BD79-B2260CD0756C`，身份见 `build/diagnostics/advancer3-followup/sidebar-manifest.json`；构建／打包日志分别为 `sidebar-build.log`、`sidebar-package.log`。
- GUI 使用原隔离 `gui-data` 启动开发构建，已目视确认 `Advancer3 0.25 nozzle` 下的 `Generic PLA @Advancer3 0.25 nozzle`，切至 `Advancer3 0.8 nozzle` 后显示 `Generic PLA @Advancer3`。原生自绘弹出菜单未由自动化捕获到全部条目，不把这些截图写成所有选项切换均已验收。已退出测试进程，用户原运行实例保留；无云端写入／实机打印。
- 用户复测：保存工程并退出旧实例，用原 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 命令重开；选 0.6 喷嘴展开耗材，检查 `Generic ABS/PETG/PLA @Advancer3`，分别选择再点耗材编辑，确认标题和选中项一致；切 0.25 检查专用喷嘴后缀。Windows 同步代码重建后做相同检查，状态待用户验证。
- `python3 scripts/check_ai_module_docs.py --all`：35 模块、0 问题；`git diff --check` 通过。本轮未提交。


### 设备 24A10DB2ADDC 不可见：接口对照（2026-10-10 10:37）

- 用户浏览器现有 `qa-3dmodelcenter.wisebeginner3d.com/#/testDevicePrint` 页面已显示目标 MAC、类型 Advancer3、状态空闲。没有点击打印、编辑、新增或删除。
- 当前用户配置环境为 qa；运行日志 `orca-user-gui-run-20261010-095851-149208.log` 确认本地 `printer_model`／`gfd_device_type` 为 Advancer3，多次设备查询正常返回任务结果，但未记录完整响应，因此不能仅凭日志解释缺失。
- 使用 Orca 配置中既有会话，按程序相同 Authorization／Biz 头，只发 GET 读取；认证值不输出、不写入诊断文件。所有请求 HTTP 200、code=0、msg=success。已保存脱敏计数及目标字段到 `build/diagnostics/advancer3-followup/device-query-comparison.json`。

| 查询 | 返回数量 | 目标设备 |
| --- | --- | --- |
| 当前 Orca：qa-appgw-hwsh.gongfudou.com + `/app/print3d/manage/v1/md/query` | 47 | 不存在 |
| 只换域名：qa-print.wisebeginner3d.com + 同一路径 | 47 | 不存在；其中 Advancer3 为另一台离线设备 `88A9A79BE0F1` |
| qa-3dmodelcenter.wisebeginner3d.com + `/app/print3dModel/manage/v1/md/query` | 24 | 存在，deviceType=Advancer3、deviceStatus=ready |
| modelcenter 同接口 + `deviceId=&operator=&deviceType=Advancer3&status=ready` | 1 | 目标设备 |
| 当前旧网关同接口 + 相同筛选参数 | 0 | 不存在 |

- 因此不是 Advancer3 大小写／AD5X 映射问题，也不是本地白名单过滤导致：源码和打包资源允许类型均包含精确 Advancer3；目标在当前接口原始返回中就不存在。上述差异不能简化成“旧网关没人维护”，因为 qa-print 同路径也没有目标；服务路由／设备集合不同，后端具体数据库或维护状态未经核查，不推断。
- 用户图 1 的 POST `/app/print3d/manage/v1/device/page` 是另一个分页查询，不能直接把 GET md/query 地址替换成它；请求方式与响应解析契约不同。本轮无需切到该接口即可在图 2 查到目标。
- 当前 `Config::device_query_url` 已为 production 返回 `https://3dmodelcenter.wisebeginner3d.com/app/print3dModel/manage/v1/md/query`，QA 则仍拼接通用 api_base_url 与 `/app/print3d/manage/v1/md/query`。建议只把 QA 设备查询对齐 QA modelcenter 专用接口，保持其他业务／认证域名及正式查询不变；继续沿用现有数组解析、筛选和打印字段，实际改动后需核对 deviceId/sn 等下发字段并做只读 GUI 查询。用户此前明确域名先不动，本轮仅定位，未修改 C++、未下发打印。
- CodeGraph MCP 未暴露，CLI 可用；三个定位文件（GFDDeviceSelectionDialog.cpp／GFDConfig.cpp／Plater.cpp）哈希与索引一致，使用 callees 辅助导航并核对当前源码。没有重建索引。本轮仅分析记录，无需应用重编译。


### QA 查询更换及接口范围核查（2026-10-10）

用户在上述定位后明确授权“更换下吧，看下哪些需要更换”。本轮只改 GFDConfig 的 QA 设备查询地址，新增 `QA_DEVICE_QUERY_URL`，复用正式环境同一路径；移除已无调用方的旧 `PATH_DEVICE_QUERY` 常量。没有引入自动跨域回退或两个列表合并。

| 业务 | QA 地址／路径 | 本轮处理及验证 |
| --- | --- | --- |
| 打印设备列表 | qa-3dmodelcenter.wisebeginner3d.com + `/app/print3dModel/manage/v1/md/query` | 已切换；返回目标 Advancer3，mac/operator/deviceType/deviceStatus/deviceStatusTitle/lastVersionFormat/sn/deviceId 均存在、非空且为字符串，可沿用现有解析器 |
| 登录公钥、登录、二次验证 | qa-datacenter.gongfudou.com + `/manager/auth/login/*` | 保留认证路由；本轮复用现有会话，无需重新登录 |
| 云方案列表、详情、新建、编辑 | qa-appgw-hwsh.gongfudou.com + `/app/print3d/manage/v1/slice-param-config/*` | 保留；列表 GET 在旧网关／qa-print 均 HTTP 200、code=0，10 个分组且包含 Advancer3，data 规范化 SHA256 完全一致。未执行新增／编辑写入 |
| 动态耗材列表、详情、参数更新 | 同旧业务网关 + `/app/print3d/manage/v1/filamentTemperature/*` | 保留；列表 GET 两域均成功，29 条且 data 哈希一致。未为本次路由验证提交参数更新 |
| 打印设备耗材槽位 | 同旧业务网关 + `/app/print3d/manage/v1/device/filamentInfo` | 保留；目标 MAC 两域 GET 均成功，mulColorFlag/slots 数据哈希一致 |
| 文件上传授权 | 同旧业务网关 + `/app/print3d/api/v1/obs/token` | 保留原入口；本轮未申请新授权或上传文件 |
| 打印命令 | 同旧业务网关 + `/app/pmc/api/v1/deviceCmd/print` | 保留原入口；只检查调用关系，不发送实际打印 |
| 正式环境各接口 | 现有生产认证、print、modelcenter 地址 | 保留；device_query_url 的 production 分支仍返回原常量。未改用户真实环境选择 |

同一 Orca 会话的各域对照摘要见 `build/diagnostics/advancer3-followup/qa-endpoint-audit.json`，只保留路由、状态、计数、数据哈希及字段存在性，无认证值。不能将三类读取等价扩大成所有写接口已经联调，也不能据此判断域名的长期维护状态。

验证与交接：

- GUI／CLI 范围：设备选择仅 GUI 提供；CLI 无云设备管理命令。本次不改模型、预设、共享切片或导出逻辑，不重复上轮四喷嘴 CLI G-code 对照。
- `cmake --build build/arm64 --config Release --target OrcaSlicer -j 4` 退出 0，日志 `device-query-build.log`；现有编译告警仍在，不宣称零告警。
- 隔离目录 `build/diagnostics/advancer3-followup/gui-data`，输入 `tests/data/model_repair_acceptance/00-healthy-cube.stl` 10 mm 方块；Advancer3 0.8 nozzle、Generic PETG @Advancer3、0.40mm Standard，中文。切片后点击顶栏“打印”进入选择窗口，默认 Advancer3 显示 1 台：24A10DB2ADDC、wangwc、空闲、固件 1.1.6-1.1.1。输入完整 MAC 后仍有 1 台。未勾选设备、未点确认、未发送打印。
- 用户复测：保存当前工程并退出旧 Orca，用原打包路径启动新应用；保留 QA 环境，切片后打开“打印”，机型选 Advancer3、状态选全部、MAC 填 24A10DB2ADDC。应显示目标设备，取消／关闭后主窗口应可操作。用户原进程没有被关闭；Windows 需同步源码重建后按相同步骤检查。

- 最终打包：限定 arm64／Release／OrcaSlicer 目标执行 `build_release_macos.sh -s -a arm64 -b -c Release`，退出 0；`device-query-package.log` 为完整日志。开发构建与打包程序 UUID 均为 `1580E104-33B3-36EB-8AE9-E61432FAA319`，打包程序 SHA256 `703bad163030aa861f198cf2f2c0f5483a498caba31e83f682d7c1a8e9e53eff`；重新签名导致两文件 SHA 不同。完整源码／资源／程序身份见 `device-query-manifest.json`。
- GUI 收尾：关闭设备选择窗口后主窗口恢复可操作，丢弃隔离测试方块工程并退出测试实例，退出码 0；用户原实例仍运行，未被关闭。未进行真实打印、云端新增／编辑或 Windows 验证。
- 最终检查：`python3 scripts/check_ai_module_docs.py --all` 35 模块、0 问题；`git diff --check` 通过。新增代码限于 GFDConfig 两文件，其他既有业务改动保留，未提交／未推送。


### Advancer3 打印前调平与颜色槽位：前后端及用户版对照（2026-10-10）

#### 核查基准

- 当前 Orca：master `650b9c8a53d2edfff4649ed6374a97d50290e386`，包含本记录已有未提交改动；本次仅新增分析记录。
- 用户版工作目录：`/Users/liwei/.codex/worktrees/docs-master-v-user/orca-slicer-soft`，master_v_user `df171d5f84562a92732db60441bc4cbf807c22a1`，工作区干净。
- 后端本地源码：Print3DPmcService `1c01426046a91583192c68bfb60de145191afdf9`（feature/f_模型打印对比-master）；Print3DService `018e72a0eebc9baf9dd38fc41c9441bb15adbfcb`（feature/f_裙边垫层）；Print3dShareService `8dc0a231a705189fee482ee5ebd9231163bac7ce`（feature/f_gcode预览图_20260623）。这些是本地分支，不声称与 QA 部署提交一致。
- 前端补充：3d_model_library_admin master `c1fe47052655089264e9d71b3d613072f1b44990`；Flutter feature/1.19.6 `52a6d413941dd614b992875472161fbf6871286e`。只读查看相关调用，未修改其他仓库。
- CodeGraph MCP 未暴露，CLI 实际存在。当前 Plater.cpp、GFDDeviceSelectionDialog.cpp 哈希与索引一致，GFDConfig.cpp 已过期，因此相关配置读源码；静态请求构造函数的 callees 查询未检出符号，不能认定调用不存在。用户版／PMC 目录无索引，按模块文档与源码继续，未初始化或复制索引。

#### 当前 master 的缺口

1. `resources/gfd_button_config.json` 的 Advancer3 `print_3mf=false`，走普通 G-code。`Plater.cpp:10931` 普通 G-code 直接进入上传，完全跳过槽位查询及映射对话框；现有映射仅用于 3MF，不能因代码里已有窗口就判定 Advancer3 已接入。
2. `Plater.cpp:10526` 请求仅含 url、deviceId、printSn、requestId、fileType，以及可选 consumable。没有 `levelingBeforePrint`、`ms`、`fileName`／`source`。
3. `GFDConsumableMappingDialog.cpp:552` 当前只读通用 `mulColorFlag/slots[].slotNo/ftSn`；`Plater.cpp:10755` 在 mulColorFlag=false 时略过映射，且过滤 ftSn 为空的槽。闪铸设备的实际状态槽位为 `matlStationInfo.slotInfos`，使用 slotId/hasFilament/materialName/materialColor，没有 ftSn，不可硬套现有过滤。
4. Advancer3 当前 0.4 本地预设的 machine_start_gcode 未含 G29；复制 AD5X 本地预设不能替代云端打印选项传参。

#### 后端协议已支持

- PMC `DeviceTypeEnum.java:28` 定义 Advancer3 的 realCode=AD5X，描述为“闪铸AD5X(共享)”；`FlashDeviceServiceImpl` 同时注册 AD5M、AD5X、ADVANCER3。
- `DeviceCmdPrintReq.java` 已有 Boolean levelingBeforePrint（默认 false）以及 `List<MsConfig> ms`。`FlashDeviceCmdService.java:159` 透传调平，设置 tCount=ms.size；对 Advancer3/AD5X 强制 useMs=true，再透传 ms。`PrinterControlMessageService.java:234` 最终写入设备命令的 leveling/useMs/tCount/ms。
- 因而直接调用 PMC 又省略调平字段，按这份源码默认为 false；它不会自动经过 Print3DService 的账号默认值回填。缺 ms 时形成 useMs=true、tCount=0、ms=null，不能将下发接口可调用等同于颜色映射完整。
- `MsConfig` 包含 slot（物理槽号）、tNum（G-code 逻辑 T 号）、mt（逻辑材料）、sRgb（选中实体槽颜色）、tRgb（模型/G-code颜色）；现有用户版按 T0 起算、物理槽1起算生成。不能把 EP7 的 consumable 整数数组当成闪铸 ms。
- PMC 控制器用 deviceId 按 MAC 查设备。目标现场查询的 deviceId 恰等于 MAC，但实现应按协议显式传 MAC；源 sn、注册记录 sn、deviceSn 不可混作用户设备 SN。
- PMC 还用 req.fileName 加 URL 后缀拼文件名；当前缺 fileName 会形成 `null.<suffix>`。迁移时应补文件基本名及 source=slicer，但保留 master 现有稳定 requestId／失败重试和模态生命周期处理。
- 共享服务 `PrintJobHandler.java:108` 同样传 printParam 的调平和 ms；`PrintJobServiceImpl.java:336` 的 transferMS 默认设调平 false。家用服务另有账号绑定默认值逻辑，并不等于共享设备的默认值。

#### master_v_user 能借鉴的范围

- `Plater.cpp:10985` 精确支持 AD5X；Flashforge 判定只有 AD5X/AD5M，未含 Advancer3。
- G-code AD5X 也进入耗材映射，单槽也确认；生成完整 ms，槽位显示数字。此结构可复用，但必须补 Advancer3，并保留当前逻辑 T 号的稀疏编号，不能把 T1 重排为 T0。
- `gfd_fetch_print_leveling_once` 读取用户端 `/app/print3d/api/v1/devices/info?deviceSn=...` 的 levelingBeforePrint，失败回退 false，`gfd_queue_print_upload` 在上传前读取。全局检索只有读取／存储／序列化，没有打印前调平选择控件。
- 用户版请求使用用户登录 Biz=ZXB、用户绑定 SN；当前 master 是管理登录 Biz=ZXBMan、modelcenter 测试设备。共享 Advancer3 不能假定有该账号绑定设置，不应直接复制用户端登录／详情路径。
- 用户版槽位仍来自 `/app/print3d/api/v1/devices/filamentInfo`。本地 Print3DService 用户／管理两个 filamentInfo 均读取 DeviceFilamentSlotService 数据库槽位；它们不是闪铸 PMC 实时料站状态。用户版的宽松字段解析也不能凭空补齐不存在的 slots。

#### QA 只读证据

使用现有 Orca 会话及原 Biz=ZXBMan，仅 GET，未输出凭据、未申请上传、未发打印或控制命令。

- 2026-10-10 11:08，目标 MAC `24A10DB2ADDC` 的管理 `/app/print3d/manage/v1/device/filamentInfo`：HTTP200/code0，`mulColorFlag=false, slots=[]`。
- 2026-10-10 11:09，同域 PMC `/app/pmc/api/v1/deviceCmd/deviceStatus?deviceId=24A10DB2ADDC`：HTTP200/code0，返回 matlStationInfo.slotInfos 四槽，hasFilament 全 true：1 PLA #F330F9、2 PLA #FFFFFF、3 PLA #0ACC38、4 PLA #161616。当时设备已在 printing，本轮没有发任何打印。
- 脱敏证据：`build/diagnostics/advancer3-followup/advancer3-slot-readonly.json` 和 `advancer3-pmc-slots-readonly.json`。通用接口返回空不能推断物理设备没有线盒；前一轮仅核对不同域名返回相同，不代表此数据源适合闪铸真实槽位。
- 家用 Flutter 实际料站 UI 读取 `devicePrintInfo.matlStationInfo.slotInfos`，并有默认调平／默认槽位设置；与通用 filamentInfo 不同。模型库 Web 的 MultiColorPrint 页面用通用槽位和 colorBindings 调模型库切片接口，不能直接当成 Orca 已切片 G-code 的闪铸下发范例。

#### 建议改造与验证范围

- Advancer3 与 AD5X 共用打印能力判断，仍保持各自云端 deviceType；从 PMC 状态解析1～4真实槽，使用 hasFilament 校验，保留材料及颜色，不要求 ftSn；未知／无料／查询失败明确报错，不能静默按默认槽打印。其他机型沿用原数据源与 consumable。
- 选择设备后、上传前提供一次“打印设置”确认：打印前调平开关（建议共享默认关闭，与现共享流程一致，可本次勾选），每个实际使用的逻辑材料 T0/T1... 对应一个实体槽（数字1～4、颜色块、材料）；单色也允许指定槽位，不以颜色数决定是否展示。
- 下发明确带 levelingBeforePrint 和完整 ms。沿用 G-code，不为了出现映射窗口强行启用 Advancer3 的3MF。第一期映射限定单设备，批量需要逐设备独立映射，不能复用一台设备的选择。
- 沿用原认证／下发域名及 master 请求生命周期，选择取消不上传不打印。后台已有所需下发字段，优先客户端完成；若产品希望统一槽位接口，再单独扩展后端，不假定本次必须新增协议。
- 后续测试：单色 T0→槽2、多色各T独立映射、仅T1使用、同槽复用、空槽／离线／网络失败、开关true/false、取消、EP7 consumable不变、Windows与Mac弹窗生命周期；实机需用户确认并执行。CLI 无云设备选择／下发入口，不改变本地切片规则。

本轮只做源码分析与上述 GET 核对，不构建应用、不调用打印 POST，不把解析源码写成已完成 GUI／实机验收。

本轮文档检查：35 模块、0 问题；git diff --check 通过。

## 10 月 10 日 Advancer3 调平／真实颜色槽位实现与交接

### 实现范围与构建身份

- 工作目录 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft`，分支 `master`，HEAD `650b9c8a53d2edfff4649ed6374a97d50290e386` 加当前未提交改动。本轮不提交，不修改 backend 或 `master_v_user`。
- 仅云返回类型 `Advancer3` 进入新分支。使用实际 PMC 状态的 `matlStationInfo.slotInfos`，不借用 AD5X 身份、不编造槽位、不用返回空 slots 的通用 filamentInfo 接口。支持读取已装载的实体槽位 1～4、材料和 RGB；未装载槽位不供选择，缺失／错误响应在上传前报错。
- 复用耗材映射窗口，Advancer3 使用数字槽位标签；逐一映射当前盘使用的零基 T 编号，单色同样显示窗口。新增“打印前调平”，默认关闭，每次下发重新确认，不写本地切片配置。
- 后端命令包含 `levelingBeforePrint` 和 `ms`，每项保留 `tNum`、`slot`、逻辑材料 `mt`、实体颜色 `sRgb`、逻辑颜色 `tRgb`；deviceId 使用 MAC，fileName 去扩展名。稀疏 T 编号及不同 T 使用同槽不会压缩／去重。重登仍复用请求 ID 和选项。
- 新窗口取消后不启动 OBS 或打印命令；槽位查询仍使用后台任务的取消、认证重试、窗口存活及 submission ID 校验。Advancer3 暂限单台 G-code；其他机型（包括 AD5X）的 G-code 跳过映射、3MF 的 mulColorFlag/consumable 及失败语义保持既有实现。
- GUI 入口为切片完成后的“打印”→设备选择→确认。CLI 没有云端下发命令，此次不新增 CLI 入口，未改共享切片／G-code 生成。协议抽为无 wx 依赖的 `GFDPrintOptions.hpp`，便于自动化验证同一请求构造。
- 最终 arm64 Release 打包退出 0，Mach-O UUID `98ECF6A5-CC4D-32D1-9ABE-A7EAC28B30A2`。可复测程序：`build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。已有运行实例不会加载新二进制，复测前保存自己的工程并重新启动。

### 自动化结果与实际限制

- `cmake --build build/arm64 --config Release --target gfd_print_options_tests OrcaSlicer -j4`：初次构建通过；文案调整后按原 `build_release_macos.sh -s -a arm64 -b -c Release` 最小增量构建／打包再次通过。日志 `build/diagnostics/advancer3-followup/print-options-build.log`、`print-options-package-final.log`。
- 最终 `gfd_print_options_tests`：6 个用例、85 条断言、退出 0，日志 `print-options-tests.log`。覆盖真实 IFS 结构、不要求 ftSn、空／离线／串设备／格式错误拒绝、缺颜色／材料、编号越界／重复、稀疏 T／共用槽、调平 true/false、无选项拒绝和 EP7/EP3Plus/AD5X 原请求逐字段保持。
- 只读 GET 实测目标 `24A10DB2ADDC` 返回 code=0、ready，1～4 号槽均为 PLA，颜色 `#F330F9/#FFFFFF/#0ACC38/#161616`。脱敏结构存于 `build/diagnostics/advancer3-followup/pmc-slots-implementation.json`；未发送打印、未上传模型。
- 中英文新增文案加入简中 PO，映射窗口加入 gettext 提取清单；msgfmt 格式检查、脚本 MO 生成通过。其他语言回退英文，未批量改译。`git diff --check`、35 模块文档检查通过。
- 隔离 GUI 使用 `00-healthy-cube.stl`（10×10×10 mm、12 面）、Advancer3 0.8 / Generic PETG @Advancer3 / 0.40mm Standard / 光滑高温热床 / 简中，切片后显示有效结果和打印入口。打印入口触发隔离目录旧会话的验证码提示，尚未进入设备确认或新槽位窗口；未解决／提交验证码，因此不算新窗口验收通过。
- 随后隔离实例退出时 SIGABRT（exit 134）。`OrcaSlicer-2026-10-10-135119.ips` 的 PID 16127 堆栈为 malloc invalid free → wxWindowBase::DestroyChildren → Plater::~Plater → MainFrame::~MainFrame；当时没有执行 Advancer3 槽位查询／确认分支。原因尚未定位，不能仅凭堆栈断言无关或已修复。日志 `print-options-gui.log` 未出现本轮 OBS 上传或打印命令。后续工具自动拉起的空实例已关闭，用户原有实例未关闭。
- Windows、新映射界面点击／取消、实际打印机执行调平／选色均待验证；单元测试不替代这些场景。

### GUI 复测清单

从仓库根目录按原命令启动新包（先保存并关闭旧实例）：

```sh
./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer 2>&1 | tee planning/debug/orca-advancer3-print-options.log
```

| 场景 | 操作 | 预期 | 当前结果 |
| --- | --- | --- | --- |
| 实际槽位与调平默认值 | 使用已有可打印模型，选择 Advancer3 的实际喷嘴及匹配设备已装载材料的耗材，重新切片；点“打印”，选 MAC 24A10DB2ADDC，再点设备选择框确认 | 显示 Advancer3 打印设置；调平默认不勾选；右侧卡片显示真实数字槽位／材料／颜色，不显示 1A 等其他机型编码 | 待用户手测 |
| 切换颜色槽位 | 在上述设置窗口点右侧槽位卡片，改选另一已装载槽位；先不要点最终“确认并下发” | 弹出可用槽位 1～4，选定后对应 T 的槽位／色块更新；空槽不出现 | 待用户手测 |
| 取消与恢复 | 勾选调平、改槽后点“取消”，返回设备选择；再次选择同一设备 | 第一次不上传、不发送打印；窗口恢复可用，再次查询实际槽位，调平默认关闭 | 待用户手测 |
| 多耗材对应 | 用现有多色测试工程重新切片，再走上述流程 | 每个实际使用的 T 单独映射；不使用的材料不占行；映射共用物理槽时不丢失 T | 协议测试通过，GUI 待验 |
| 调平／实际下发 | 用户确认设备空闲、材料／喷嘴匹配后，分别选择调平开／关，再点最终“确认并下发” | 日志命令包含相应布尔值和所选 ms，设备按选择执行；成功／失败收尾后界面可用 | 仅协议测试通过，实机未执行 |
| 老机型回归 | 选择现有 EP3Plus／EP7／AD5X 原有入口，重复正常下发前操作 | G-code 无新增 Advancer3 设置，原 3MF 映射／consumable 规则保留 | 协议逐字段测试通过，GUI 待验 |
| 退出异常复核 | 当前测试包在登录完成／取消登录两种状态下分别关闭应用；操作前保存工程 | 正常退出，不出现崩溃报告；若复现回传同一时间段日志和 ips | 已发现隔离实例退出异常，待定位 |

请回传本次完整日志和设置窗口截图；若实际下发，再说明调平开关、T→槽位选择及打印机实际动作。Windows 同步源码构建后重复以上关键场景，不以 Mac 构建替代 Windows 验证。

## 10-10 调平默认、取消提示与退出崩溃续修

### 现场与原因

- 用户日志 `planning/debug/orca-user-gui-run-20261010-135727-329531.log`：13:57:59（submission 1）、14:01:34（submission 3）均完成状态查询，未进入上传或打印请求；与取消截图一致。旧代码在 `mapping_cancelled=true` 时仍调用 `complete_print_submission(false)`，把主动取消当作失败显示红字。
- 14:06:50 用户确认四个逻辑材料的映射，`levelingBeforePrint=true`；实体槽为 T0→2、T1→4、T2→3、T3→1，上传成功，14:06:53 PMC 响应 HTTP 200 / code 0 / data true。它证明后端接收成功，不代替设备实际打印、颜色和调平动作验收。
- 退出异常对应隔离实例 PID 16127、旧测试二进制 UUID 16A13C6D-B907-3017-BDBE-3BC86EADA839，报告 `~/Library/Logs/DiagnosticReports/OrcaSlicer-2026-10-10-135119.ips`。13:51:13 进入 MainFrame shutdown；崩溃线程仍在 `GFDDeviceSelectionDialog::ShowModal → show_gfd_device_selection_dialog`，同时 `MainFrame → Plater → wxWindowBase::DestroyChildren → malloc free 未分配地址 → SIGABRT`。设备选择框是该函数栈变量，父窗口先销毁时会错误 delete；验证码在 13:50:30 已正常关闭，并非验证码窗口卡住或 PMC 数据解析闪退。
- 此轮守卫在主窗口任何清理之前拦截可否决退出，保留最内层模态，让用户先完成／取消弹窗。没有修改槽位接口或打印业务规则；其他机型的请求载荷仍不变。不可否决的系统强制结束保留平台语义，未作为安全退出验证范围。

### 其他日志条目（截至 14:24）

| 条目 | 次数 | 源码解释与本次影响 |
| --- | --- | --- |
| 中文文件名解析错误 | 3 | `PrintStatistics::finalize_output_path` 再用模板解析器解析“立方体／圆盘”开头的实际文件名，非 ASCII 报错被 catch，原名回退；切片完成状态 0、后续上传与命令成功。独立的文件名解析问题，本轮仅分析，未修改切片核心。 |
| SupportSpotsGenerator 多耗材提示 | 6 | 支撑问题分析 Params 仅采用第一种材料；不代表打印命令只有一个槽，实际 ms 有 4 项。异材质支撑分析有这一既存限制。 |
| calc_exclude_triangles | 3 | 复用 AD5X 的 `0x0` 单点排除区无法生成三角网格；前轮 AD5X/Advancer3 CLI 对照双方已有，未发现新增差异。 |
| 3Dconnexion 驱动未加载 | 1 | 可选 3D 鼠标驱动缺失，当前返回未加载；与普通鼠标／打印下发路径无关。 |
| get_version 返回 00.00.00.00 | 1 | 网络插件没有 get_version 函数，使用默认版本；并非打印机固件版本。 |
| 首页 Navigation／Script warning | 若干 | WebView 加载和消息事件被记录为 warning；内容未显示加载失败。 |

14:24 仍有正常备份事件；收尾复查到 14:33:42，没有新增 warning/error。未发现这次用户实例的新崩溃报告或打印请求失败；不能把仍在增长的日志当完整退出记录。

### 本轮实现与验证

- Advancer3 共享默认与 UI 初始化统一为调平 true；允许用户明确关闭。其他机型不增加该字段。
- 完成回调增加取消标志：映射窗口取消、导出取消（无导出错误）、槽位查询取消、命令发出前上传取消显示中性状态并恢复操作。已经成功／结果未知分支继续关闭选择框、提示先核对设备。
- MainFrame 调用 `GFDModalDialog` 的共用关闭守卫，覆盖原生／自管模态；检测按 owner 父链，优先最内层窗口；普通无模态退出不受影响。
- 构建 `gfd_print_options_tests gfd_modal_dialog_tests OrcaSlicer`，Release/arm64/Ninja Multi-Config，退出 0；日志 `build/diagnostics/advancer3-followup/cancel-leveling-build.log`。翻译 `msgfmt --check` 退出 0。
- 请求单测 7 个用例、87 条断言通过，包含新默认开启与主动关闭、稀疏 T 编号／重复实体槽、坏状态拒绝、原机型请求不变；日志 `cancel-leveling-tests.log`。
- wx 回归 87 项检查、0 失败，覆盖原生／自管／嵌套模态期间 owner 退出被否决，非关联窗口不受影响，模态退栈后 owner 正常销毁；日志 `cancel-leveling-modal-tests.log`。完整应用 Cmd+Q 和 Windows 仍待手测。
- 打包命令 `SLICER_CMAKE_GENERATOR='Ninja Multi-Config' SLICER_BUILD_TARGET=OrcaSlicer CMAKE_BUILD_PARALLEL_LEVEL=4 bash build_release_macos.sh -s -a arm64 -b -c Release`，日志 `cancel-leveling-package.log`；身份由同目录 `cancel-leveling-manifest.json` 保存。
- 按变更行执行 clang-format 后仅 Plater 排版变化，重建及最终打包均退出 0，日志 `cancel-leveling-build-final.log`、`cancel-leveling-package-final.log`；测试目标源码／二进制未变化，不重复计数。`check_ai_module_docs.py --all` 为 35 模块、0 问题，`git diff --check` 通过；首轮文档检查提示 7 项指纹待复核，完成正文和原因更新后通过。
- CLI 没有 PMC 打印管理或 GUI 退出入口，本次不存在 CLI 新字段入口；本地切片核心和 G-code 不变。GUI 自动化与真实设备动作分别验收，AI 本轮未发送打印。

### 本轮 GUI 交接（待重启新包）

使用仓库 `tests/data/model_repair_acceptance/00-healthy-cube.stl`，Custom / Advancer3 0.4 nozzle、Generic PLA、0.20mm Standard，简中；也可沿用用户本次四材料圆盘工程验证多槽。新包路径为仓库 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。先保存已有工程，关闭旧实例再启动新包；不要用旧进程验证新二进制。

| 场景 | 操作 | 预期 | 状态 |
| --- | --- | --- | --- |
| 默认调平 | 切片后选 Advancer3 设备 24A10DB2ADDC，进入打印设置 | 调平默认勾选，实际槽位数据正常 | 待用户 |
| 取消 | 在打印设置点取消，再从设备列表重进；再试标题栏关闭／Esc | 不出现下发失败红字或错误弹框，显示“已取消打印下发。”；不上传、不发送打印，重新进入仍默认勾选 | 待用户 |
| 退出保护 | 打开打印机选择，或其内打印设置时按 Cmd+Q／退出菜单（Windows 尝试退出主窗口）；随后依次取消弹窗再退出 | 弹窗存在时主窗口不被清理；关掉弹窗后可正常退出，不崩溃 | 独立 wx 回归通过，完整应用待用户 |
| 明确关闭调平 | 取消勾选后仅在用户安排的实机测试中确认下发 | 日志字段 false；保持勾选时 true。AI 不执行打印 | 协议单测通过，实机待用户 |
| 其他机型 | 同样进入此前已支持机型的下发选择流程 | 保持原打印设置与请求；取消恢复操作 | 载荷单测通过，GUI 待用户 |

回传本轮启动日志、操作时间和取消后的截图；四槽位已下发成功只记录为旧包的用户联调证据。

## 10-10 15:29 用户复测与提交复核

- 输入：`planning/debug/orca-user-gui-run-20261010-152909-953828.log`，1,506 行；配对应用日志 `debug_Sat_Oct_10_15_29_13_47660.log.0`，PID 47660。当前包 UUID `E4BF51A3-A226-3B31-A51F-99CF59F8F508` 及 SHA256 与 `cancel-leveling-manifest.json` 一致，工作区相关源文件没有变更。预设／网页的 33 项源码和资源身份也与 `validation-manifest.json` 一致。
- 15:29:28 打开设备选择；15:29:33、15:29:38 两次主窗口关闭均记录 `Main frame close deferred until the active modal dialog returns`，未提前进入清理。
- 15:29:40 设备框以 wxID_CANCEL=5101 返回，`shown=false, modal=false, parent_enabled=true`；15:29:43 再次请求关闭，15:29:49 完成 MainFrame shutdown、GUI_App shutdown 和 `~GUI_App: exit`。未产生新 ips；此日志证明 Mac 设备框下退出保护及关闭恢复通过，不只是单元测试推测。
- 本次是 EP3Plus 主界面／设备选择／退出流程，无 Advancer3 映射确认、上传或打印命令。因此不宣称本次日志验证了默认调平、映射取消提示或实机效果；这些仍按上轮协议测试及 14:06 用户联调记录分别列证据。
- 日志只有 4 次既存床面排除区三角化提示、可选 3Dconnexion 驱动缺失、网络插件版本函数缺失及 WebView 事件 warning，没有新的崩溃、网络下发失败、中文文件名解析错误。床面提示并非此次改动新增，未发现阻塞本批入库的问题；此前中文文件名问题仍按独立已知项保留，不能用本次未切片记录宣称其已消失。
- 本批自动化复用已核对身份的结果：预设 10 用例／1,681 断言、网页 12 用例、打印协议 7 用例／87 断言、wx 模态 87 检查；四喷嘴 CLI 指令等价及隔离 validator 见前轮记录。不重复计数或用旧身份替代新代码。
- 提交范围保留 Custom 四喷嘴完整本地 AD5X 参数复用，云端身份为接口实际 `Advancer3`；其他机型原请求不变。模块清单与 test-tools 文档含无关 Thingi10K 工作，暂存时只纳入本任务段落，工作区原内容保留；未提交日志、凭据或用户配置，不推送。
- 最终 `check_ai_module_docs.py --all` 与 `--staged` 均为 35 模块、0 问题，`git diff --cached --check`、简中 `msgfmt --check` 通过；新增文本未发现凭据模式。日志中现有非阻塞提示没有触发本轮产品代码修改，按用户授权提交上述已构建验证的实现；提交不表示 Windows／实机验收完成。
