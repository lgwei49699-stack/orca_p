# AD5X 打印中跳过失败对象分析

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：分析原厂 AD5X 能否在单对象打印失败后跳过该对象并继续本盘，以及当前 CLI／GFD 平台需要的接入；本轮不修改业务代码、不连接或控制真实打印机、不更新固件。
- 基准：`codex/model-feature-analysis-20261018`，HEAD `40d8be288d7d0299c7886867821499f094974c35`。工作区已有其他任务的模块文档、清单、验证记录及 `result.json` 改动；全部保留。
- 已确认：本仓库 AD5X 0.4 机器预设使用 `klipper` G-code flavor；0.20 标准工艺显式开启 `exclude_object=1`；共享 GCode 导出已有对象定义和开始／结束标记。
- 官方证据已核对：Flashforge Wiki 的 Parts skip 指南说明切片启用 Exclude Objects 后，在打印机屏幕选择对象并确认即可排除；移动端启用该能力的示例明确为 AD5X。文档没有把普通 Klipper 网络控制接口作为前提，也未给出可用于本平台的公开远程取消接口。
- 当前步骤：源码／资料分析完成；仅新增本记录和 README 入口，未修改业务代码。文档检查结果见下文。
- 验证边界：未运行本轮 CLI 切片、GUI 或实机打印。CodeGraph MCP 本会话未暴露；已安装 CLI、目录和数据库文件存在，但相对／绝对只读 SQLite 连接均返回 unable to open database file；按“索引读取失败”降级源码，不推断索引缺失或重建。
- 最新用户证据：2026-10-19 两张 AD5X 屏幕照片已确认打印中出现排除入口，且能进入带多个轮廓的对象选择界面；尚未看到选中确认后的红色状态或后续实际打印，不将入口可用记为跳过执行通过。
- EP7 补充：同一 HEAD 的 EP7 0.4 机器预设为 `klipper`，0.20 标准工艺继承 `exclude_object=1`。用户最新两张照片已确认屏幕入口为“更多 → 跳过”，点击后提示单盘超过 64 个物体或仅打印一个对象时不支持跳过。屏幕入口及数量限制已证实，实际选择对象、执行排除并继续打印尚未验证。
- 最新分析：用户明确 EP7 当前任务并非逐件打印。逐层顺序不阻止对象排除；已核对本仓库共享导出逻辑及启迪公开 Q2 固件，判断排除依据是对象标记与取消名单，不是逐件开关。原厂各代屏幕与 EP7 定制版本不能相互代替。
- 下一步：核对 EP7 当前任务的独立对象数量；后续实机验证可用两个独立对象、保持逐层并启用对象排除，进入“更多 → 跳过”检查对象列表与执行结果。不要为此合并对象，也不要求改成逐件打印。AD5X 用户确认后的实际排除效果仍待回传。平台远程操作另需核实设备适配器协议。

## 已核对入口

- `resources/profiles/Flashforge/machine/Flashforge AD5X 0.4 nozzle.json`
- `resources/profiles/Flashforge/process/0.20mm Standard @FF AD5X.json`
- `src/libslic3r/GCode.cpp`：`set_object_info`、对象开始／结束标记。
- `src/slic3r/Utils/GFDConfig.hpp`、`GFDPrintOptions.hpp` 及 `Plater.cpp`：云端打印请求契约及实际调用方。

## 当前结论与证据

### 原厂能力

官方指南确认多对象同盘可以跳过失败对象，保留其他对象打印；操作为开启 Exclude Objects、切片下发、在打印机屏幕进入零件跳过、选中目标并确认，成功后目标显示红色。该能力与逐件顺序是两个独立设置，官方指南未要求只能逐件打印。

官方同时说明跳过后机头可能仍有额外空移，被跳过对象可能保留已打印的部分底层；不能承诺立即跳到下一件第一行、清除失败残件或完全没有空移。本轮不把屏幕支持扩大为本平台已能远程控制，也不把文档支持扩大为所有旧固件已支持。当前用户设备固件版本、实际下发文件和实机表现尚未知；未确认最低适用固件版本，不提供猜测版本或升级指令。

### 切片与配置

通过 Python 只读递归合并 Flashforge 品牌清单内父／子 JSON，得到以下资源配置值（不等于用户任务的最终有效配置）：

| 配置 | 解析值 | 作用 |
| --- | --- | --- |
| AD5X 0.4 机器 `gcode_flavor` | `klipper` | 输出 Klipper 风格对象命令 |
| AD5X 0.4 机器 `use_relative_e_distances` | `1` | 相对挤出配置 |
| AD5X 0.4 机器 `extruder_clearance_radius` | `58` mm | 逐件避让参数之一 |
| AD5X 0.4 机器 `extruder_clearance_height_to_rod/lid` | `26/130` mm | 逐件高度避让参数 |
| `0.20mm Standard @FF AD5X` 工艺 `exclude_object` | `1` | 已开启可排除对象标记 |
| 同工艺 `gcode_label_objects` | `0` | 普通对象注释关闭，不等于排除标记关闭 |
| 同工艺 `print_sequence` | `by layer` | 默认仍是逐层；逐件需显式改 `by object` |

共享 `GCode::apply_print_config` 读取 `exclude_object`；`set_object_info` 输出 `EXCLUDE_OBJECT_DEFINE NAME=... CENTER=... POLYGON=...`，路径段输出 `EXCLUDE_OBJECT_START/END NAME=...`。GUI 和 CLI 复用导出核心，独立对象的不同实例有独立名字；这些名字应作为未来平台控制映射的依据，不能直接假设 UI 行号就是设备对象 ID。

逐件单色验证的工艺值为 `print_sequence="by object"`、`exclude_object="1"`，实际机器仍使用 AD5X 预设。不改固件 flavor 冒充能力开启，不依靠 CLI `skip_objects`（后者是切片前筛选）。输入 3MF 还须核对盘级顺序覆盖。

首轮限单色、无实际擦料塔路径。当前 `GCode.cpp` 的逐件导出分支以 `ByObject && !has_wipe_tower` 为条件；不能将单色结果自动推广到 AD5X IFS 多色、多对象内换料和擦料塔组合。排除命令、换料段与共享路径的归属需要另验。

### 当前平台缺口

`Plater::priv::gfd_queue_print_upload` 准备文件并上传，`gfd_send_print_command_once` 调用 `GFD::build_print_command`，通过 `/app/pmc/api/v1/deviceCmd/print` 发起打印。非 Advancer3 分支的请求包含 `url/deviceId/printSn/requestId/fileType` 及可选 `consumable`。这些是创建打印任务的字段，不能直接当作取消对象的接口。

本轮检查的 Plater、GFDConfig、GFDPrintOptions、GFDAuthManager、GFDDeviceSelectionDialog 和原生 Flashforge 主机类未见运行中按对象排除的调用；`SaveStrategy::SkipModel` 是 3MF 导出策略，与此无关。Flashforge 主机类当前提供连接、上传及启动打印，未暴露统一的运行中发送对象排除命令方法。本仓库不含 PMC 后端／设备适配器源码，因此不能断言后端绝对没有能力。

若要由自有平台实现，建议先确认设备协议，再补：能力／固件检测、当前任务与对象列表读取、UI 对象到导出 NAME 的映射、按任务发送排除请求、读取排除后状态并显示结果。标准 Klipper 的 `EXCLUDE_OBJECT NAME=...` 可说明语义，但不能据 `gcode_flavor=klipper` 假设原厂 AD5X 开放标准 Moonraker 地址或任意 G-code 注入端口。没有已证实协议时不编造 endpoint。

自动判断失败另需检测来源与对象定位；对象排除标记不承担检测。断料、堵头等整机故障不等同于可通过跳过单个对象恢复。

## 最小 GUI／实机验证交接（全部待执行）

本清单供后续明确安排的实机验证使用，本轮未连接打印机、未发送打印或固件命令。

输入与准备：记录 AD5X 固件／IFS 版本、切片器版本及二进制身份；创建两个独立且名字可区分的单色小立方体（例如 10×10×8 mm），同盘按 AD5X 逐件避让重新排布，不使用实际多色擦料塔。保存工艺／机器／耗材 JSON、项目、最终下发 G-code 和任务标识。使用用户认可的正常打印准备，不人为制造故障。

| 场景 | 操作 | 预期观察 | CLI／文件核对 | 状态 |
| --- | --- | --- | --- | --- |
| 文件准备 | 同参 CLI 或 GUI 切片两个独立对象，`by object`、`exclude_object=1` | 参数校验完成、文件含两个独立对象 | 检查两个 DEFINE 名称、对应 START/END；确认对象1全部层后才进入对象2，不能只查配置注释 | 未执行 |
| 屏幕手动跳过 | 经用户授权启动上述任务，在第一件正常打印中选择该件并确认跳过 | 目标标红；后续不再挤出其剩余路径，另一件继续；允许官方说明的剩余空移 | 记录时间、选中对象名、屏幕和实际输出；不把一次 HTTP 成功当执行成功 | 待实机 |
| 逐层对照 | 同样两个对象改为 `by layer`、保持排除开启，另起任务 | 能单独排除其中一个，另一件继续，用于区分排除与逐件顺序 | 核对仍有两个独立标记以及正常逐层路径 | 待实机 |
| 自有云下发 | 将已验证的同一份最终 G-code 通过现有 GFD 链路发送，再从设备屏幕跳过 | 标记和对象列表未被后端重写丢失 | 比较下发前文件 hash 与设备实际文件／可获取证据，记录任务号 | 待接口与实机 |
| 平台远程跳过 | 取得经确认的控制协议后，绑定当前任务并指定对象取消 | 返回可核对的排除状态，其他任务和其他对象不受影响 | 后端协议／状态回读测试；本仓库当前无此端到端证据 | 未接入 |

## 验证与源码身份

- 本轮只读 JSON 继承解析：成功；得到上表实际资源值，未修改 JSON。
- 静态源码检查：已确认共享导出标记、GFD 发起打印入口及本仓库未发现的远程排除接入；不等同运行验证。
- 应用构建、CLI 实际切片、GUI 操作、设备固件及实印：本轮均未执行。纯分析／文档任务不需要为此重建应用。
- 文档检查：`python3 scripts/check_ai_module_docs.py --all` 退出 0，35 个模块、12946 个归属文件、2498 个明确排除、问题 0；README 的 `git diff --check` 通过。这只验证文档归属／链接／指纹，不证明设备功能运行通过。

| 文件 | SHA256 |
| --- | --- |
| `src/libslic3r/GCode.cpp` | `5667c4ac0135836665cd492dfa95e76db6c4fd15079b1c6497cdfc5b70692db9` |
| `src/slic3r/GUI/Plater.cpp` | `b6f8e880a6c7a5d1b1e10cfd150620027fcaf86f857616dc433693582a0f8bcb` |
| `src/slic3r/Utils/GFDPrintOptions.hpp` | `b4c6d2ccfd7b3e135721b44a6570f8e6f9fd08cf154f9c1f5382e1363de487c2` |
| AD5X 0.4 机器 JSON | `7c64b42824f8da0523f2da29b47ed3e9297b946824b8f0f39b01720d8b58139c` |
| AD5X 0.20 标准工艺 JSON | `a00fd9e940cd8d249c57d640edd48dfa262ba885d80e7cb2c96e8b0b0e43ddb9` |

## 2026-10-19 用户屏幕回传

用户提供两张照片：第一张显示 AD5X 正在打印 `DAFFY+DUCK_plate_7.3mf`，进度 17%，模型预览下方有灰色零件跳过按钮；第二张已进入对象排除选择界面，可见多个轮廓、视图平移箭头和缩放按钮。

按第二张照片当前竖向显示方向：先点击要排除的模型轮廓，再点击照片左下方、返回箭头上方的“三个小方块＋弯箭头”按钮确认。按官方说明，选中目标为蓝色，确认排除后为红色。四向箭头及加减放大镜用于查看轮廓。

照片证实入口和选择界面已出现，不证明固件版本、选中的轮廓对应哪个实体、实际切片顺序或跳过成功；不替用户决定要取消哪件，不远程执行设备命令。此前“本轮未执行”指 AI 未操作设备；现在增加用户提供的界面证据，实际取消与继续打印仍待回传。

## 2026-10-19 EP7 对照

用户追问 EP7 是否也能跳过失败模型。本轮只读递归解析 Custom 预设，机器继承链为 `EP7 0.4 nozzle → fdm_machine_x_common → fdm_qidi_x3_common → fdm_machine_common`；工艺继承链为 `0.20mm Standard @EP7 → fdm_process_n_common → fdm_process_common`。继承名称不证明 EP7 使用其他品牌的固件或屏幕界面。

| 配置 | 当前资源值 | 来源 |
| --- | --- | --- |
| `gcode_flavor` | `klipper` | `resources/profiles/Custom/machine/EP7 0.4 nozzle.json:30` |
| `exclude_object` | `1` | `resources/profiles/Custom/process/fdm_process_n_common.json:233` |
| `gcode_label_objects` | `0` | 同文件第 232 行 |
| `print_sequence` | `by layer` | 同文件第 114 行 |

共享导出核心具备 Klipper 对象排除标记能力，EP7 标准资源已启用相应参数；仍须核对实际输入及盘级配置是否覆盖、最终下发文件是否保留标记。本轮未运行 EP7 切片，不将资源解析记为产物或设备验证通过。

标准 Klipper 需要启用 `[exclude_object]`，控制端按当前任务的对象名称发送 `EXCLUDE_OBJECT NAME=<name>`，并回读排除状态。当前未取得 EP7 固件配置或该版本官方对象排除操作说明；屏幕入口已由后续用户照片确认，远程协议仍未知。不据 G-code flavor 假设存在 Moonraker 或可直接发送任意指令的接口。EP7 尚无实际跳过成功证据。

随后用户明确说明“就是启迪的”，并提供当前打印中主页照片：显示 11%、25/350 层，操作区可见“暂停”“停止”“助手”“更多”。接受用户提供的品牌信息，但不由品牌推断具体 QIDI 型号及固件版本。当时主页未直接显示“跳过／排除对象”，只建议打开“更多”查找，尚未确认其菜单内容；后续证据见下文。

### 逐层任务与启迪固件特性补充

用户进一步明确未下发逐件打印，要求分析启迪特性。此后优先分析固件与文件条件，不把菜单探索当作能力结论：

- 本仓库 `GCode.cpp:4521` 和 `4686` 的对象开始／结束标记以 `m_enable_exclude_object` 与共享筏层边界为条件，没有限定 `print_sequence=by object`；`2134` 的对象定义输出同样由排除开关控制。逐层 A/B/C 的打印中排除 B，应跳过后续各层 B 的对象区段，继续 A/C；不会因此改成逐件打印。
- 启迪官方 `QIDITECH/QIDI_Q2` 的查询基准提交为 `3f6bcc8bc295ecf067b45345c5bdb2139b287f17`。`config/printer.cfg:465` 已有 `[exclude_object]`；`klipper/klippy/extras/exclude_object.py:167` 用 `current_object in excluded_objects` 判断排除区段，`cmd_EXCLUDE_OBJECT` 接受对象名称／当前对象取消，未要求逐件顺序。这是该公开固件的证据，不等于 EP7 实机固件已经一致。
- 启迪官方 `QIDITECH/QIDI_SMART3` 的查询基准提交为 `2adb47605453561985d6cfd3089bd185a3f4e01d`。抽查 `src/ui.cpp`、`event.cpp`、`MakerbasePanel.cpp`、`MakerbaseParseMessage.cpp`、`MoonrakerAPI.cpp` 及 `include/ui.h`、`event.h`，未找到 EXCLUDE_OBJECT／按对象取消接入；已看到打印中暂停／停止等独立 UI 事件。此为有限源码范围的负向证据，不证明所有版本或 EP7 屏幕都没有按钮，也不能把 EP7 认定为 Smart3。
- Smart3 官方 README 第 42 行明确提供 Fluidd 网页访问方式：机器 IP 后加 `:10088`；Plus3 官方 README 也有该说明。它证明这些公开机型有独立网页控制面，不保证 EP7 定制系统保留同端口或相同功能。若 EP7 保留该入口，可只读核对当前任务对象信息；本轮未访问任何真实设备。
- 可取消的单位是导出中的独立对象／实例名称。一个文件或一个盘可以含多个对象；反过来，一个打印在一起的可动模型／多个外观零件若被切片成同一个对象，就不能仅凭模型外观任意取消其中一块。照片只有整盘缩略图，不能由此确定对象数量。
- 当前任务是否可排除，取决于最终 G-code 原有对象标记及设备识别状态。给新工艺设置 `exclude_object=1` 不会给已经开始执行的无标记文件自动补齐对象边界；有标记的逐层任务则无需为了排除对象改成逐件任务。

本批仍仅做源码／公开资料分析和记录更新，没有重建应用、运行 CLI 切片、改固件或下发控制命令。公开源码写入系统临时目录供只读审阅，未导入仓库。Fluidd 独立文档搜索未返回结果，后续 GitHub API 请求触发匿名限额 403；未把此当作设备端不可用，网页入口依据仅限启迪官方 README。

### EP7 屏幕入口与对象数量限制的实机证据

用户新增两张照片，同一手机支架任务进度 16%、40/350 层：第一张打开“更多”，菜单依次为“跳过”、Z 偏移及“照明”；第二张点击“跳过”后显示“以下情况不支持跳过对象：单个板块中有超过64个物体；仅打印一个对象”。

由照片可以确认该设备当前屏幕版本提供“更多 → 跳过”入口，并声明对象数量限制。按提示，单盘 2～64 个独立对象满足该数量条件，但不保证仅靠数量即可执行成功，仍需有效对象标记及设备识别。结合手机支架预览，当前任务可能被识别为一个对象；弹窗同时列出两个条件且未展示实际数量，不能把推测写成已读到设备对象计数。此限制不是用户未启用逐件打印的证据。

后续验证：两个独立对象同盘，保持 `print_sequence="by layer"`、`exclude_object="1"`，核对最终文件中两个对象名称与完整边界后，由用户按正常流程启动，在“更多 → 跳过”中选择一个并确认，观察另一个继续打印。不得将两个对象合并为一个再用外观数量判断。当前仅确认入口及阻止提示，没有用户成功排除对象和继续打印的回传；本轮 AI 未操作设备。

## 资料

- [闪铸官方 Parts skip 指南](https://wiki.flashforge.com/en/Flash-Studio/Flash-Studio-Desktop/Parts_skip)，2026-10-19 查询。
- [Klipper 对象排除协议](https://www.klipper3d.org/Exclude_Object.html)。协议本身不证明原厂 AD5X 暴露标准 Moonraker／控制端口。
- [启迪 Q2 对象排除配置](https://github.com/QIDITECH/QIDI_Q2/blob/3f6bcc8bc295ecf067b45345c5bdb2139b287f17/config/printer.cfg#L465)及[对象排除实现](https://github.com/QIDITECH/QIDI_Q2/blob/3f6bcc8bc295ecf067b45345c5bdb2139b287f17/klipper/klippy/extras/exclude_object.py#L167)。
- [启迪 Smart3 屏幕服务与 Fluidd 说明](https://github.com/QIDITECH/QIDI_SMART3/blob/2adb47605453561985d6cfd3089bd185a3f4e01d/README.md#L40)。
