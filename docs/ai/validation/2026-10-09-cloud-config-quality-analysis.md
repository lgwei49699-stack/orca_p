# 云配置类型与加载界面改版分析

## 当前执行状态

- 目标／边界：用户已授权实现云端导入加载条及标准／快速／精细配置的上传、保存、导入；明确保持所有域名不变。用户现已授权审查通过后提交；提交仅包含云配置业务、界面、测试与对应文档，不含无关研究/日志或真实云写入。
- 基准：`master`，HEAD `d76038c3e4ff184fe26d3171fd45a11be47b9585`。工作区已有 cloud 模块与打印卡死验证文档修改，另有 modules.json、test-tools.md、result.json 和 Thingi10K／教材研究改动；均保留。
- 当前步骤：完整云配置差异审查、修复及自动化验证完成；工作区/暂存区模块检查和差异检查通过，本批按用户授权选择性提交。真实表单/云写入及Windows仍待用户验收。
- 工具身份：CodeGraph MCP 未暴露，`~/.local/bin/codegraph` 可运行；开始定位时核对 MainFrame.cpp、Plater.cpp/.hpp、GFDConfig.hpp 的索引 SHA256 与源码一致；本轮改码后索引已过期，后续复核直接读当前源码，未重建索引。图中部分调用关系缺失，不据此认定无调用。
- 验证：提交前当前Mac arm64 Release构建、协议10例89断言、生命周期43检查、CLI --help、中文msgfmt通过。最新身份见 `build/diagnostics/cloud-config-precommit-identity.json`，取代ui-v6证据；真实表单视觉、云端写入和Windows待验。
- 下一步：用户从当前构建执行本文GUI清单并回传日志，尤其验证真实云端与Windows。提交不包含无关改动，不自动推送或向云端写入。

## GFD 环境与域名盘点

当前源码的 GFD 环境只有 qa 和 production，入口为登录弹框及 Preferences 的 GFD 云环境。GFDConfig.cpp 的 resolve_environment 只区分这两套，未定义 GFD 的开发／预发布第三套环境。以下是当前生效配置中的五个固定业务域名，不把源码注释、上游文档链接或用户打印主机地址计入：

| 用途 | 测试 | 正式 |
| --- | --- | --- |
| 登录、公钥、验证码 | qa-datacenter.gongfudou.com | dcenter.kfb-1.com |
| 云配置、耗材参数、设备耗材、OBS 令牌、打印指令 | qa-appgw-hwsh.gongfudou.com（用户本轮明确保留） | print.wisebeginner3d.com |
| 设备查询 | 同测试业务域名 | 3dmodelcenter.wisebeginner3d.com，路径另用 print3dModel |

- 业务 URL 统一由 GFDConfig 拼接；替换 QA_API_BASE_URL 将影响 OBS 令牌、配置 add/edit/list、设备查询及耗材信息、耗材温度 list/detail/updateSliceParam 和 pmc 打印指令。认证服务有独立 AUTH_BASE_URL，不随 API 域名改变。
- 上传与文件分发域名由 OBS 令牌响应的 host、cdn，以及配置下载 URL 决定，不是新增云环境。此前 13:42 Windows 日志实际出现 micro.obs.cn-east-2.myhuaweicloud.com、cdn-h.gongfudou.com；这只是该次请求证据，不是服务端域名的完整固定清单。
- qa-system3d.wisebeginner3d.com 和 qa-3dmodelcenter.wisebeginner3d.com 是用户截图中的测试管理网站入口，当前 Orca GFD 配置并未直接使用这两个站点域名，不构成第三环境。
- 整个 Orca 仓库还保留上游 Bambu 的 DEV／QA／PRE／Product 及版本更新、预设更新等域名；它们属于另一套功能。AppConfig 在缺失 GFD 环境字段的旧配置迁移中，只将旧 iot_environment=QA 映射为 GFD qa，其余映射为 production，不能把上游 DEV／PRE 选项当作 GFD 第三／第四环境。
- 旧 qa-appgw-hwsh 是否停止维护未获证实；当前源码配置盘点和公开前端资源不能证明维护状态。尚未变更域名、会话或发起实际打印。

## 用户补充 system3d 后的接口选择结论

用户新截图的后台为 `qa-system3d.wisebeginner3d.com/#/sliceParams`，网络请求为 `https://qa-print.wisebeginner3d.com/app/print3d/manage/v1/slice-param-config/device-and-slice-type`。进一步只读核对该网站实际发布的 JavaScript：

- [API 定义 783.c29334fb.js](https://qa-system3d.wisebeginner3d.com/static/js/783.c29334fb.js) 使用同一 print3d 前缀，列表 GET `/device-and-slice-type`、新增 POST `/add`、保存 POST `/edit`，另有 GET `/detail`（通过 params 传参）和 GET `/page`。前三项相对路径与当前 Orca 完全一致。
- [页面 9888.b40c2781.js](https://qa-system3d.wisebeginner3d.com/static/js/9888.b40c2781.js) 已按 profiles／fineness 展示并提交 standard、fast、fine；新增和编辑保留顶层标准档文件字段，标准档文件必填。说明 print3d 这套前端契约也已经支持三档。
- 推荐沿用 print3d 接口族，对齐此后台的三档契约。前一轮“当前 Orca 与 pds 后台不同”的对比仍成立，但不能由此推出 Orca 必须迁移到 pds。
- 环境差异必须单独核对：当前 Orca QA_API_BASE_URL 是 `qa-appgw-hwsh.gongfudou.com`，该后台是 `qa-print.wisebeginner3d.com`。源码与前端只证明路径／契约一致，不能证明两个 QA 域名已部署同版本；不要直接全局替换网关而影响登录、设备和打印等接口。
- 本地公开资源证据在 `build/diagnostics/cloud-config-api-review/system3d/`，有 evidence.json 时间及 SHA256；API 文件 SHA256 为 `a32e6b5d34c70facee12b133b3cf3a4aaf66aabc31b81ac22e2999f3f3929cd7`，页面文件为 `baa80d0ac8050b43c062062f2b7f6cecd85487ece5baeb1e88660b157e24efb4`。未调用认证 API 或新增／编辑接口，未使用截图中的 token。

## 改码前的初步证据

- 云导入使用 `GFDCloudImportDialog`：列表上覆盖独立 wxPanel，120ms Pulse，切换加载状态执行 Layout；与已优化的设备选择底部状态行尚未统一。
- 当前 `GFDCloudConfigInfo` 只有一组 config_file_url／config_file_name／default_process_conf；导入来源状态只记录配置 ID、名称、机型和本地预设，未记录质量类型。
- 客户端列表 GET `/app/print3d/manage/v1/slice-param-config/device-and-slice-type`；截图后台请求 GET `/app/pds/manage/v1/slice-param-config/page`。截图 URL 的 sliceType 是 cura，不能把截图 ORCA 页签直接当作该请求筛选条件。
- 上传／保存使用 `/app/print3d/manage/v1/slice-param-config/add` 和 `/edit`，需进一步核对新版请求体和后端兼容性；未向服务端写入数据。

## 已确认的新版协议

证据来自 [QA 后台机型管理前端](https://qa-3dmodelcenter.wisebeginner3d.com/static/js/src_views_optionsMng_machineMng_index_vue.js)。本地只读证据目录为 `build/diagnostics/cloud-config-api-review/`，不入库；`evidence.json` 记录时间和 SHA256，其中 machine.js 为 `47de784a50e40cf9e1cc8b66ada1e32eb42a3287c3fbe1b5f7ca5de3f798477c`。该证据证明前端请求契约，不等于后端写入实测或生产环境已同步上线。

| 流程 | 当前 Orca | 新版后台页面 |
| --- | --- | --- |
| URL 前缀 | `/app/print3d/manage/v1/slice-param-config` | `/app/pds/manage/v1/slice-param-config` |
| 列表 | GET `/device-and-slice-type`，客户端筛选机型与 orca | GET `/page`，传 deviceType、sliceType、pageNum、pageSize |
| 详情 | 当前列表结果直接导入，无对应详情读取 | GET `/detail/{id}`，编辑前读详情 |
| 新增 | POST `/add` | POST `/add` |
| 保存 | POST `/edit` | POST `/update` |

- 新版方案使用 `profiles` 数组；各项含 `fineness`、`configFileUrl`、`configFileName`。枚举 `standard`／`fast`／`fine` 分别对应标准／快速／精细，不能用 `sliceType` 表示质量；`sliceType` 仍是 orca／cura。
- 新版提交仍带顶层 configFileUrl／configFileName，值取标准档，用于旧结构兼容。旧数据缺 profiles 时，页面把顶层文件视为标准档。
- 新版新增及编辑页面均要求标准档文件存在；快速、精细可缺失。它会提交所有有文件的 profiles，而不只发本次改动的一项。
- 新版前端提交未见 defaultProcessConf；当前 Orca 额外发送整个 project_settings_text，并在文件缺失时允许从 default_process_conf 导入。需要后端确认新结构中该字段是否保留、是否分档；不能把标准档的参数回退给快速／精细。
- 未核实 `/print3d` 和 `/pds` 是否由网关转发到同一后端、旧 `/edit` 是否接受 profiles、未提交档位是否保留。不能仅改 URL 或假设旧接口兼容。

## 建议交互与实现边界

1. **加载行**：云导入复用本轮设备选择的底部固定状态行做法。进度条放列表下、取消按钮上方，隐藏时保留布局；状态区统一显示查询／下载解析／失败，避免覆盖列表和重复提示。此处当前是 wxScrolledWindow，不能据静态结构断言必然发生与设备选择完全相同的闪烁。保留 alive、generation、取消、登录重试与环境快照保护。
2. **上传配置**：明确目标方案和档位。新方案按新版规则先建立标准档；已有方案允许把当前配置补充到快速／精细，或覆盖已存在的指定档。不要自动把同一文件复制到三档伪造配置。若产品希望全新方案可单独上传快速／精细，先与后端确认放宽标准档必填规则。
3. **保存配置**：默认保存到导入来源方案及档位，确认框显示“方案名 · 档位”；显式变更目标档时告知覆盖对象。实现时读取详情并保留其他档位和方案备注，不能仅用 ID 与顶层文件更新整个方案。若后端提供单档更新接口应优先使用；整组更新还需明确并发修改处理方式。
4. **云端导入**：每个方案保持一个分组，标准／快速／精细分别显示标签、文件和导入动作；缺档显示“未上传”并禁用。导入某档后绑定 `方案 ID + fineness`，本地预设显示名也应区分档位，避免同名覆盖与保存串档。
5. **来源持久化**：GFDCloudConfigInfo、GFDCloudImportState 及 gfd_cloud_process_records 都需要保存档位。老记录可按兼容约定迁移为 standard，未知值不可默认为标准后写入。新增返回 ID 缺失时的列表回查也需按档位匹配文件，不能只查顶层 configFileUrl。
6. **实现入口**：MainFrame.cpp 的 GFDUploadConfigDialog、GFDCloudImportDialog、按钮绑定；Plater.hpp/.cpp 的解析、导入、上传及来源记录；GFDConfig 的 API 地址；中英文翻译。GUI 按钮目前走异步实现，文件中还保留同步封装，应共用协议转换逻辑避免两套行为漂移。

GUI／CLI 边界：在 src/OrcaSlicer.cpp 未找到对应云方案上传／保存／导入命令；本次是 GUI 云方案管理及服务协议适配，CLI 无相应业务入口。共享 3MF 参数导出／解析必须维持现有语义；不更改切片算法或按质量标签自动推导层高、速度。

## 验证范围（执行结果见下方）

- 解析：旧单文件、新 profiles、三档齐全、缺快速／精细、未知档位；确认兼容标准档且不会将缺失档误导入。
- 保存：依次修改三档，验证只替换目标档、其余文件与备注保留；重启后恢复档位；切换机型、预设、环境或账号时防止串绑。
- 网络：错误、超时、登录恢复、取消／关闭、旧请求晚返回；保持原有非幂等写请求不因普通网络失败自动重放。
- GUI：Mac／Windows 的首次加载、刷新、下载、缩放、主题与 DPI；无列表覆盖或闪烁、关闭后无残留回调。图 1 静态截图本身不能作为上述场景通过证据。
- 协议：先用只读列表／详情确认实包；经用户确认编码及明确测试方案后，才对指定测试方案做新增／更新联调。

## 实施与验证流水

- 第一批：新增纯 JSON 的 GFDCloudConfig.hpp；兼容旧标准档，保留未知档位与非目标档的元数据，拒绝重复／坏 profiles，顶层文件始终对应标准档。
- 第二批：上传可新建标准档或选当前机型已有方案指定档位；保存继承来源档位，详情读取失败不写配置，写后回读核对所有档位 URL。未切换域名、add/edit 路由未变，仅增加同前缀 detail GET。
- 第三批：云端导入一方案三档，缺档禁用；固定底部状态行，取消覆盖面板及状态切换全窗 Layout。来源记录持久化档位与环境／账号，新导入工艺名带稳定英文档位后缀，避免切换语言造成重复预设。
- 旧记录无 fineness 仍按 standard；旧记录无账号／环境沿用历史当前会话语义，新记录检查环境／账号。后端无版本字段或条件更新契约，仍不能保证两个客户端同时编辑时的原子性；当前实现采用写前读最新、写后回读检测，不宣称解决服务端并发覆盖。
- 构建首次 CMake 生成失败：测试误用 nlohmann_json::nlohmann_json，仓库实际 target 为 nlohmann_json；已修正，不修改第三方。
- 中文目录 msgfmt --check --check-format 通过；本地 mo 已更新。最终构建及测试结果见下方。未使用截图中的认证信息，未向云端写入方案。

## GUI 验证交接（本批待用户操作）

- 本地基准：master / d76038c3e4 加本记录所述未提交差异，Mac arm64 Release；应用最终构建成功，CLI --help 通过，二进制身份见末节。CLI 不提供云方案管理命令；协议行为由独立测试覆盖，共享3MF导出方式未改。
- 输入：仓库 `tests/data/model_repair_acceptance/00-healthy-cube.stl`；在准备页选择 EP3 Plus 0.4 nozzle、0.20mm Standard @EP3 Plus 及对应 PLA，使用现有 QA 登录，简中亮色开始。使用独立新测试方案 `codex-quality-20261009-自定义后缀`，不要覆盖现有业务方案。
- 启动前正常关闭当前正在运行的旧程序，保留或保存自己的工程。应用路径：本次构建目标实际输出 `build/arm64/src/Release/OrcaSlicer.app`，旧 `build/arm64/OrcaSlicer/OrcaSlicer.app` 打包副本暂未更新。下列命令启动的是本轮已验证构建。

```sh
mkdir -p planning/debug
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer 2>&1 | tee planning/debug/orca-cloud-quality-mac.log
```

Windows 同步本批改动并自行构建后使用既有入口（当前未提交，尚不能仅 git pull 获取）：

```powershell
./build/OrcaSlicer/orca-slicer.exe 2>&1 | Tee-Object -FilePath planning/debug/orca-cloud-quality-windows.log
```

| 场景 | 操作 | 预期 | 状态 |
| --- | --- | --- | --- |
| 加载布局 | 打开云端导入，刷新，加载中切换其他应用再返回；缩放窗口 | 底部显示状态／细进度条，列表无覆盖；取消可用；完成后控件恢复 | Mac／Windows 待验 |
| 标准档新增 | 上传配置→新建方案，输入独立测试名和备注后确认 | 新方案类型固定标准；查询后标准档有文件，快速／精细显示未上传 | 云端待验 |
| 补快速档 | 将层高改成合法的0.24mm并在本地另存为独立工艺（先保存所有未保存的机器／耗材／工艺修改）；上传配置→选择云端方案→同一测试方案快速档→确认 | 仅快速档新增；标准档文件／备注不变 | 云端待验 |
| 补精细档 | 将层高改成合法的0.12mm并在本地另存为另一独立工艺；选择同一测试方案精细档上传 | 三档分别存在；标准／快速的文件不变 | 云端待验 |
| 分档导入 | 分别导入标准、快速、精细，按既有机器／耗材替换确认选择所需项 | 工艺名后缀对应档位，各自参数为先前上传值；缺档按钮不可点击 | GUI／云端待验 |
| 分档保存 | 导入精细档后修改一个工艺参数→保存配置 | 确认框显示方案名及精细；重新导入核对更新值，标准／快速不变 | GUI／云端待验 |
| 来源恢复 | 关闭并重开应用，选择刚才的精细工艺后修改→保存 | 仍识别精细档；旧来源无档位按标准兼容 | GUI／云端待验 |
| 中断与错误 | 加载时取消／右上角关闭；断网刷新并恢复网络；下载中关闭 | 无残留模态／回调异常；错误可见且可重试；结果未确认时不显示保存成功 | Mac／Windows 待验 |
| 中英文及布局 | 切英文、深色，Windows 125%／150% DPI，重复打开两种配置窗口 | 标签完整、确认按钮可见；三档对齐，长文件名省略但可查看提示 | 待验 |

回传对应日志及时间段、上传／保存确认框和三档列表截图；若接口未返回 profiles 或 detail 不支持，记录具体错误，不通过替换域名绕过用户本轮约束。并发原子性须后端支持，本次手测不要让两个客户端同时修改同一测试方案。

- 自动化实测：`gfd_cloud_config_tests '[GFDCloudConfig]'` 7 个用例、50 条断言全部通过，退出码 0；日志 `build/diagnostics/cloud-config-tests.log`。此目标不连接云服务。
- 应用构建因缓存更新需重编译较多依赖，确认本机 64 GiB／16 逻辑核后，停止本任务自己的 -j2 构建（退出码130），保留已完成对象；独立协议测试完成后以 -j6 继续应用目标。不是编译失败，也未停止用户进程。

- 第一轮应用构建成功，退出码0；末段补充状态文字悬停提示及头文件排版后，最终增量构建成功（退出码0）。

- 最终复核将三档按钮回调改为捕获行索引和档位，点击时读取当前方案，避免每个按钮长期复制完整方案JSON。该修改在最终构建编译MainFrame之前写入；随后再次构建仅执行已有 encoding-check，没有待编译／链接目标，退出码0。

## 最终本地验证（2026-10-09）

以下为第一批验证，用户截图反馈后的修正另见末节；不要以此处二进制身份证明后续源码。

- 配置：当前仓库 master / d76038c3e4 加本轮工作区差异；Ninja Multi-Config、arm64、Release、SLIC3R_GUI_TESTS=ON。未构建 Windows，也未更新旧打包副本。
- 应用及测试构建：`/Applications/CMake.app/Contents/bin/cmake --build build/arm64 --config Release --target OrcaSlicer gfd_cloud_config_tests --parallel 6`，退出码0；再次执行仅运行既有编码检查。日志 `build/diagnostics/cloud-config-final-build.log`、`cloud-config-final-noop.log`。编译存在原有 wxWidgets 重载隐藏等告警，未出现构建错误。
- 协议最终复跑：7例50断言通过，退出码0；`build/diagnostics/cloud-config-tests-final.log`。覆盖三档更新隔离、保留未知档位、旧格式兼容、损坏／重复档位、标准档必填及服务端回读不一致。
- 当前应用 `--help`：退出码0，版本仍为2.3.2.2-Release；`build/diagnostics/cloud-config-help.log`。此检查没有启动GUI，不能代替真实云端业务。
- 应用 SHA256：`b9846f1748d5ae6720875221cc2cc1e8d73082cbc4e701d0e4628d41b4ea7644`；测试 SHA256：`b09f288fd7c5e05744af5711d826fbd42b8d9915172b0744393ce789ee050fe1`。逐文件源码、测试、PO/MO身份存于本地 `build/diagnostics/cloud-config-identity.json`，日志和身份文件不入库。
- 最终待验：Mac／Windows 的布局、主题、DPI、取消及云端三档联调；当前网关是否部署新版 profiles/detail 契约须实际服务响应确认。未执行真实云端新增、编辑、打印，未提交。
- 文档及差异检查：`python3 scripts/check_ai_module_docs.py --all` 通过（35模块、12882归属文件、0问题）；`git diff --check` 通过。最终源码／测试／PO/MO／二进制哈希与身份记录仍一致；固定认证／业务域名及既有add/edit/list路径逐项比对HEAD未变。未暂存，未提交。

## 用户截图反馈后的第二批修正

- 用户明确要求加载条居中与打印窗口一致；云导入三档内容完整展示；选择上传目标每方案一个选择、默认标准；保存支持快速／精细，旧数据默认标准。域名继续保持不变。
- 原因：水平状态行的文字占满剩余空间，Gauge被推到右边；row_panel仍显式使用44DIP单行高度，覆盖了wx根据三档内容计算的有效最小高度，内容因此超出父窗口被裁切。表头和正文的文件／操作列最小宽度也不一致。
- 已改：状态文字与居中Gauge分行、隐藏仍预留空间；根据真实sizer最小高度确定整行高度，表头／正文统一文件列230DIP、操作列108DIP；每个子行按字体最小高度对齐。选择模式每方案一个居中按钮并返回standard，上传窗口仍可选择快速／精细；标题改成选择云端方案。
- 保存弹框新增配置类型下拉框，默认来源档位、允许改选目标档位，仍在确认后复核来源及环境。旧来源无fineness、null或空串默认standard，明确未知类型拒绝，避免误写标准；成功后的本地来源记录使用实际保存档位。参数值不随标签自动变化，本地工艺名称不因另存到其他档位自动重命名。
- 最小构建命令与前批相同，日志`build/diagnostics/cloud-config-ui-v2-build.log`，退出码0；重复构建`cloud-config-ui-v2-noop.log`仅执行编码检查、无需编译／链接，退出码0。中文msgfmt检查及本地mo更新通过。首次追加文档的Python命令因stdin编码错误未执行，改用apply_patch完成，没有影响源码。
- 第二批协议测试新增旧来源类型到保存payload的覆盖，实际8例64断言通过，退出码0；日志`cloud-config-ui-v2-tests.log`。曾在测试目标尚未重新链接时运行到旧程序（7例50断言），该输出另存`cloud-config-ui-v2-tests-prelink-old.log`且不计入本批证据；确认[150/157]重新链接后复跑新8例。
- 第二批应用`--help`退出码0，公开版本仍为2.3.2.2-Release；日志`cloud-config-ui-v2-help.log`。应用SHA256=`b8febccc6b4db3528b457c576260c3791cc7f94e18e6ab403d22aba551d5ffeb`，测试SHA256=`d0c9a1391fc271198bc731ad59a7119f70577e2a3ecea41de890709930d8f41a`；逐文件身份见本地`build/diagnostics/cloud-config-ui-v2-identity.json`。
- 新程序仍从`build/arm64/src/Release/OrcaSlicer.app`启动；用户运行中的`build/arm64/OrcaSlicer/OrcaSlicer.app`副本未关闭或替换。复测前正常退出旧程序，再使用前文新构建启动命令。本批尚未验证真实界面，不把编译通过写成裁切／DPI视觉实测通过。
- 第二批文档检查通过（35模块、0问题），`git diff --check`通过；源码／二进制／PO与MO哈希均与本批身份记录一致，域名和既有API路径未变。未暂存或提交，保留本轮之外的工作区改动。

### 第二批GUI复测清单

使用前文同一输入和独立测试云方案，启动本批构建；首批截图证明存在视觉缺陷，不能视为第二批GUI通过。

| 场景 | 操作与预期 | 状态 |
| --- | --- | --- |
| 加载位置 | 云端导入→刷新，进度条在列表下方水平居中；显示／隐藏不改变列表大小 | Mac／Windows待验 |
| 三档布局 | 完整看到标准、快速、精细三行；每行操作与文件对齐，无上下方案覆盖；滚动至首末项并调整窗口尺寸 | Mac／Windows待验 |
| 选择目标 | 上传配置→选择云端方案，每个方案只有一个选择按钮，返回后配置类型为标准；可在上传窗口改快速／精细 | 待验 |
| 保存类型 | 修改已关联云端的工艺后点保存配置；弹框显示方案和配置类型。新来源默认实际档位，旧来源默认标准；取消不发送保存 | 待验 |
| 改选保存 | 在独立测试方案中选择快速或精细保存，重新导入核对实际参数与其他档位保持；重启后本地来源仍为已保存档位 | 云端待验 |
| 显示适配 | 中英文、深色及Windows125%／150%DPI，三行完整、按钮／下拉框清晰；长文件名截断后悬停可读 | 待验 |

## 第三批：固定表头、紧凑底栏与可编辑上传表单

- 基于用户第二批截图继续调整，不推翻三档协议和域名约束。取消／刷新绿色边框来自Button::SetStyle的Focused边框；表头当前在滚动内容内；底部留白由状态、进度、按钮三行与多个间隔叠加。
- 将表头移到滚动区域外并跟随滚动条实际宽度对齐列；类型改成带细边框的非交互标签；状态、居中进度与取消整合为一行，隐藏进度不改变列表高度。
- 上传的两个入口改成等宽、有当前状态的按钮；已有档位显示将覆盖，缺档显示将新增。选择已有方案后名称和备注允许编辑，提示其为三档共用字段；仅把用户实际修改的字段合并到写前最新详情，未改字段保持最新值，写后验证元数据及各档文件。
- GUI／CLI范围不变：云方案管理仅GUI入口；纯协议测试覆盖元数据部分更新与回读，未对真实云端写入。本批手测待用户执行。
- 已实现上述改动，中文msgfmt检查通过、本地mo已更新。协议目标先独立构建并确认重新链接，再运行测试：9例80断言通过，日志`build/diagnostics/cloud-config-ui-v3-tests-build.log`、`cloud-config-ui-v3-tests.log`，退出码均为0。
- Mac arm64 Release应用最小构建通过，日志`build/diagnostics/cloud-config-ui-v3-build.log`，退出码0；重复构建`cloud-config-ui-v3-noop.log`仅编码检查，CLI --help日志`cloud-config-ui-v3-help.log`，退出码均0。编译保留已有wx/TBB告警，没有构建错误。
- 本批应用SHA256=`438c8e3c1184571a386f2fe747f557067bfb6e7b6e22687dfb14c9b4a6a28a6f`，测试SHA256=`24de2a0a3acea1602241f336089a867830206103cb262ada0e020f4a0c5740e9`，逐文件身份在本地`build/diagnostics/cloud-config-ui-v3-identity.json`。未关闭／替换用户运行中的旧打包副本；本批输出仍为`build/arm64/src/Release/OrcaSlicer.app`，前文启动命令适用。
- 第三批最终文档检查通过（35模块、0问题），`git diff --check`通过；HEAD、源码／二进制／PO/MO身份一致，固定域名及既有接口路径未变。未暂存或提交，保留无关工作区改动。

### 第三批复测

输入与独立测试方案沿用前文，启动本批新构建后按序执行。第二批截图能确认三档文字已完整显示，但不能证明本批样式、滚动及真实保存通过。

1. 云端导入：滚动到底部，表头保持固定；调整窗口大小、刷新空列表／长列表时，表头与数据列仍对齐。
2. 标准／快速／精细显示细边框标签；缺档操作仍禁用；Windows125%／150%DPI和英文检查标签不截字、文件列不错位。
3. 取消／刷新点击或Tab聚焦时以背景提示，无绿色边框；Tab和Enter仍可操作。底栏只占一行，加载条居中，加载前后列表大小不跳。
4. 上传配置：两个入口等宽，当前模式明确；新建输入名称和备注后选择已有方案，再切回新建，原先新建草稿恢复。选择已有方案后名称、备注可输入。
5. 独立测试方案已有标准档：选标准时提示覆盖，确认后仅替换标准文件，其他档文件保持；选缺失快速／精细时提示新增。
6. 修改方案名称但不编辑备注，上传后名称更新且备注保持；只清空备注后上传，名称和其他类型文件保持。名称／备注影响整个方案，使用测试方案验证，不覆盖业务方案。
7. 保存窗口三档选择与旧来源默认标准继续回归；取消不发送写请求，真实保存后回读确认。Mac／Windows视觉及云端联调均待验证。

## 第四批：中性焦点、完整备注与上传入口语义

- 用户第三批截图可见表头固定结构、带框类型标签和紧凑底栏；同时反馈刷新仍有淡绿背景、备注省略、上传两按钮过宽且“选择云端方案”含义不清。截图仅作为这些显示现象的证据，不证明云端写入或各平台完整通过。
- 普通按钮Focused/Pressed使用已有灰色主题映射，保留Tab焦点反馈；绿色只表达当前上传模式。备注移除省略号，按实际列宽从原文重新换行，支持中文、空格及连续字符串；行高不足时扩展。尺寸事件合并后延迟排版，窗口关闭检查存活标识；刷新前清除旧控件记录，避免回调访问销毁行。
- 上传模式按钮改为内容宽度、30DIP高度和12号字体，取消整行拉伸；第二项改“上传到已有方案…”，说明为已有方案补充其他类型或替换已有类型。选择器标题同步明确上传目的。选择后的默认标准、三档选择、元数据编辑和二次保存确认保持。
- 本批仅MainFrame GUI及zh_CN文案变化；CLI无此管理UI，共享协议、Plater上传链、GFD域名、测试源码和测试程序逐文件核对与第三批身份一致，因此复用9例80断言证据，不重复运行未改的协议测试。
- 中文msgfmt --check --check-format与git diff --check通过；本地MO已更新。最小构建通过（退出码0）：`/Applications/CMake.app/Contents/bin/cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 6`，日志`build/diagnostics/cloud-config-ui-v4-build.log`。构建目录仍为当前仓库arm64／Release，无其他构建进程；用户运行的打包副本不关闭或替换。

### 第四批GUI复测清单

使用前文输入、EP3Plus现有云列表及独立测试方案。仅观察布局无需上传；需要检查上传行为时仍使用独立测试方案。源码当前未提交，Windows须同步本轮工作区并编译。

1. 刷新和取消：鼠标悬停、点击、Tab移入／移出焦点，背景仅灰度变化，不出现淡绿色；Tab／Enter仍可操作。切深色再检查可辨识。
2. 备注：查看截图中的长备注方案，完整分行，无省略号；缩小、放大窗口，再刷新，换行随列宽更新；更长备注时整行高度扩展且不覆盖下一行，表头与各列保持对齐。Mac和Windows125%／150%DPI待验。
3. 上传配置：两个入口按文字宽度紧凑排列，第二项为“上传到已有方案…”，下方说明补充／替换类型。切英文检查文案完整；选已有方案后回传标准，可改快速／精细，提示新增或替换对应类型。
4. 关闭／刷新恢复：加载中取消或关闭，再打开；调整窗口后立即关闭再打开，不应残留布局回调或空白／卡住。已有方案窗口取消不改变上传表单已选目标，新建草稿仍可恢复。

待用户回传本批界面截图／日志与操作时段；本批尚未进行GUI实操或真实云端写入。

- 第四批Mac构建与CLI --help退出码均0，公开版本保持2.3.2.2-Release。仅MainFrame重新编译并链接，已有wx重载隐藏/未使用字段等告警未引入构建失败。逐文件身份见`build/diagnostics/cloud-config-ui-v4-identity.json`，应用SHA256=`95cbc049871403207cc2772fd38f33e80a72e52af02fbf57de29e8aee6f2d522`。新应用仍从`build/arm64/src/Release/OrcaSlicer.app`启动，前文日志命令适用；旧打包副本未替换，GUI与真实云端仍待验。

- 第四批最终文档检查通过（35模块、0问题），`git diff --check`通过；源码／资源／二进制与身份记录一致。未暂存、未提交，保留其他工作区改动。


## 现场补充：跨屏拖动后弹窗不可见（本批UI恢复，不是代码修复）

- 用户报告把对话框移出去后看不到、主界面无法操作。现场读取到测试构建进程50300及仍存在的“云端导入”对话框；窗口菜单提供另一显示器2490W1。未结束进程或修改工程。
- 尝试窗口菜单Center、Raise及Move to 2490W1后，用户确认能看到弹窗，但拖回Orca时感觉层级异常／半透明。截图可确认弹窗内容仍可绘制，某次标题栏呈非活动灰色；不据此认定整个窗口alpha已改变。自动坐标点击被多屏windowNotFoundAtPosition阻断，Escape／Cmd+W也未观察到关闭，不能声称已自动取消。
- 随后用户明确反馈“现在正常了”。恢复的具体用户动作未提供，因此仅记录恢复结果，不把某次菜单操作判为唯一恢复原因。
- 源码检查：MainFrame将自身作为云导入父窗口并调用ShowModal；当前wx Cocoa实现显示dialog时会作为NSWindowAbove子窗口加入父窗口，业务代码未设置弹窗半透明。没有证据把该问题直接归因于父窗口缺失。双屏位置、macOS桌面／全屏空间及窗口激活／层级异常仍是待区分因素；未记录故障时坐标、空间及原生窗口层级，不能锁定具体分支。
- 主窗口等待模态弹窗这一机制已确认；本次不修改代码，不宣称防止跨屏移丢的修复完成。后续若复现，优先保留包含主窗口和弹窗的整体截图，并区分拖动中／松开后、全屏／普通窗口、所在显示器。


## 第五批：移除按钮省略号与上传重复确认

- 用户截图中的省略号来自按钮文案，不是控件截断；中英文改为“上传到已有方案”／“Upload to existing scheme”。
- 删除GFDUploadConfigDialog::on_confirm在已有方案下额外ShowModal的“确认保存”问答。保留名称非空校验、表单中的新增／覆盖提示和名称备注共用说明；点击表单“确定”后继续原上传链，表单“关闭”仍取消。没有改“保存配置”的档位选择窗口、登录／异常／成功提示、协议回读及覆盖隔离。
- 本批GUI专属交互，CLI无该上传表单；共享协议及三档校验未改，不为文案和删除重复弹框新增实现镜像测试。后续核对与第三批9例80断言的源码／测试程序身份。
- 中文msgfmt及git diff --check已通过；当前仓库arm64／Release／Ninja Multi-Config，无其他构建进程。最小构建通过（退出码0）：`/Applications/CMake.app/Contents/bin/cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 6`，日志`build/diagnostics/cloud-config-ui-v5-build.log`。

### 第五批GUI复测清单

启动本批新构建，沿用前文输入与独立测试云方案，Mac／Windows及英文界面待验：

1. 上传配置入口按钮完整显示“上传到已有方案”，末尾没有省略号；英文同样无省略号。
2. 选已有独立测试方案，切标准／快速／精细，表单仍分别提示替换已有类型或新增缺失类型，并说明名称备注共用。点“确定”后不再弹图中的“确认保存／是／否”，直接进入原上传流程；上传结束仍按实际结果显示成功或错误。
3. 名称为空时点“确定”仍停留表单并提示填写；点“关闭”直接取消，不发送上传。新建方案仍默认标准。
4. 实际完成一次替换或补档后刷新独立测试方案，核对目标类型及其他类型保持；本批没有执行真实云端写入，此项待用户验证。

- 第五批最终结果：Mac应用构建、CLI --help、msgfmt、git diff --check均退出码0；文档检查35模块、0问题。已有wx告警不影响构建。应用SHA256=`4f181cb6a793f60a4ba9e67f91d8d502503089cb8b2bdd4d4df4539dfac5ecdb`，逐文件身份记录`build/diagnostics/cloud-config-ui-v5-identity.json`；构建与启动日志同前缀。新输出仍为`build/arm64/src/Release/OrcaSlicer.app`，前文启动命令适用。未关闭或替换用户运行的打包副本，未执行云端写入，未暂存或提交。


## 第六批：保存／上传进度与导入状态收尾（进行中）

- 保存框继承消息弹框导致图标占位和内容过挤，改为与上传一致的普通表单；在原表单内显示准备、上传文件、保存方案、确认结果阶段。文件上传使用真实字节百分比，未知时长阶段只显示活动动画，不伪造整体百分比。
- 两个表单共用提交生命周期：开始后禁用重复提交／修改，异步完成停止进度并退出原模态层，返回调用者后才显示结果。成功内容包含服务端确认的方案名称和实际类型；错误不报成功。生命周期和晚到回调需验证，不改变HTTP写入语义。
- 图2已定位：MainFrame在apply_prepared_cloud_config返回后才set_loading_state(false)，但Plater在该函数内部显示成功提示。计划在下载完成、进入用户参数确认前停掉动画并清空下载状态，保留忙碌防重入到处理返回。
- GUI入口为MainFrame上传、Plater保存选择；共享上传核心仍为start_gfd_config_upload_async。CLI没有云管理表单；三档写入／回读协议及域名不变。

### 第六批实现与验证进行中

保存表单去掉消息框大图标，与上传表单共用 `GFDCloudConfigDialog`：提交锁定输入、预留底部状态/居中进度，阶段未知时动画、上传HTTP字节进度时显示百分比；完成后先EndModal，再由外层显示带服务器回读方案名/类型的成功提示。未增加二次确认。导入应用前停止加载计时器/隐藏Gauge/清空状态，但保持操作锁定直到应用返回。

新增独立wx生命周期目标 `gfd_cloud_config_dialog_tests`：异步完成、重复点击、忙时关闭/取消、失败/异常/取消结果、释放父窗口、迟到回调；不加载切片业务或连接云端。已登记模块归属。中文msgfmt已通过。

构建前确认 `master/d76038c`、arm64、Release、Ninja Multi-Config、SLIC3R_GUI_TESTS=ON 和工作目录一致，无并行编译。用户仍运行旧路径 `build/arm64/OrcaSlicer/OrcaSlicer.app`，不覆盖或关闭。启动命令：`cmake --build build/arm64 --config Release --target OrcaSlicer gfd_cloud_config_dialog_tests --parallel 6`；日志 `build/diagnostics/cloud-config-ui-v6-build.log`，此条记录时运行中。

生命周期初测：36检查中1项失败——测试错误地要求 Cocoa 的原生 ShowModal 必须改变父窗口 wx IsEnabled 标记。已核对当前 wx `src/osx/dialog_osx.cpp`：使用 OSXBeginModalDialog/native modal loop，并不对父窗口调用 Enable(false)。调整为 Windows 检查禁用标记、Cocoa 检查模态会话，仍跨平台检查结束后表单隐藏、循环返回、父窗口可用。实现另补充从不定进度切换到上传0%时必须调用SetValue，避免数值相同而未退出原生动画。待本轮构建结束后增量重建辅助目标并复测。

### 第六批最终自动化结果

- 主构建和最后8步增量构建退出0，分别见 `build/diagnostics/cloud-config-ui-v6-build.log`、`cloud-config-ui-v6-build-final.log`。已有编译警告未在此轮扩展处理。
- 独立程序 `build/arm64/tests/gui/Release/gfd_cloud_config_dialog_tests.app/Contents/MacOS/gfd_cloud_config_dialog_tests`：36检查、0失败、退出0，见 `cloud-config-ui-v6-lifecycle-final.log`。真实wx事件循环覆盖原表单先退栈再交还结果；没有连接云端或启动用户Orca配置。
- 当前应用 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --help` 退出0，见 `cloud-config-ui-v6-cli-help.log`。CLI无云方案保存/上传管理命令；本次未改共享3MF导出/有效配置/切片规则。
- PO msgfmt检查/生成MO通过，新应用Resources指向仓库resources；源码/程序/MO身份见 `cloud-config-ui-v6-identity.json`。旧路径运行中的应用未替换、未关闭。
- 纯JSON协议头、测试源、CMake和协议测试程序均与ui-v5 manifest一致；复用ui-v3的9例80断言，本轮不重复计数。模块检查35模块0问题，`git diff --check`退出0。未暂存、提交或推送。

### 第六批GUI交接

构建身份：本仓库master/d76038c加当前未提交改动，Mac arm64 Release，ui-v6 manifest。保存当前工程并自行退出旧Orca，然后启动本次程序；不要同时启动两个Orca来判断弹框归属。

```sh
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer 2>&1 | tee build/diagnostics/cloud-config-ui-v6-user.log
```

输入：使用可修改的测试云方案/机型和已导入的当前工艺；保存测试会写入所选档位，新增测试用独立方案名。域名保持现有QA/正式选择。本轮AI未执行云端写入。

| 场景 | 操作 | 预期结果 | 自动化/CLI证据 | GUI状态 |
| --- | --- | --- | --- | --- |
| 保存布局 | 云端导入测试方案后点保存配置，查看标准/快速/精细 | 无大图标；方案名、类型、说明及按钮有间距；默认来源档位 | 应用构建通过；类型协议旧证据身份一致 | 待用户 |
| 保存进度/成功 | 选目标类型后点保存 | 表单保留且不可重复提交；准备/上传/保存/核对状态；上传文件阶段显示百分比；先关处理表单再显示含方案名/档位的成功框；确认后主窗口可用 | 36项生命周期通过；实际云端由用户验证 | 待用户 |
| 上传新方案/已有方案 | 新建标准配置；再选已有测试方案补充快速或精细 | 两条入口都有同样进度和成功结果；没有新增是/否确认；未选类型的文件保留 | 纯JSON协议9例80断言旧证据；本轮生命周期通过 | 待用户 |
| 导入成功底栏 | 点击某个已上传档位“设置此参数”，完成替换选择 | 进入参数确认/成功提示前底部加载条已隐藏，状态不再停留“正在准备”；确认后可继续操作 | 源码复核和构建，实际导入GUI待验 | 待用户 |
| 取消/失败 | 未提交时关闭；在可控断网测试下提交并等待失败 | 未提交不发送；忙时阻止重复操作/关闭；失败不出现成功提示，结束后可重新进入；结果不明时按提示先查列表 | 测试覆盖启动失败/异常/服务端失败/取消结果，未模拟实际网络 | 待用户 |
| 切换窗口/显示 | 保存或上传期间切到其他App再返回；中英文/深色/Windows缩放复看 | 处理弹窗可见，成功提示确认后主窗口能操作；状态/按钮无遮挡 | Mac辅助生命周期通过，不能替代原机/多屏 | 待用户 |

回传本次运行日志、保存/上传方案名与类型、出现问题的时间和截图。区分“日志显示业务结束”与“手动确认界面可操作”；未验证项仍保留待验。

### 提交前审查进行中

用户授权检查通过后提交。审查范围为自d76038c以来云方案三档、列表/上传/保存UI、进度和成功提示相关全部未提交差异。保留并排除result.json、Thingi10K工具/研究、旧打印现场补充及混合文档中的无关段落；工作区与暂存区分别检查模块指纹。

审查发现并修正：云配置初次登录恢复及后台401重登后必须核对原环境/账号，再进入导出或重放请求；仅未选已有方案且原账号为空的新建流程允许首次登录建立身份。避免旧ID跨环境/账号继续更新。登录和二次验证父窗口改为当前保存/上传表单。新增会话切换与嵌套登录生命周期回归；开始最小重建当前源码，日志cloud-config-precommit-build.log。

审查回归第一项完成：新构建独立生命周期程序43检查0失败，日志 `build/diagnostics/cloud-config-precommit-lifecycle.log`。新增嵌套登录父子关系、内层先退出再恢复上传与外层收尾；实际登录/验证码UI及Windows仍待验。应用与协议目标本次重建仍运行中。

### 提交前审查结果与补充交接

已复核完整三档解析/保留与旧数据兼容；移除同步入口后的调用；最新详情合并/回读确认；文件上传进度与后台阶段；忙时禁止重复提交；模态退栈后显示结果；临时文件/迟到回调清理；名称备注修改范围和中英文占位符。

修复上述重登目标边界和父窗口归属，并补齐Success标题中文。修复后未发现阻塞本次提交的问题。域名、版本号、3MF导出和切片算法不改；GUI云方案管理无同义CLI入口。无关研究、下载脚本、result.json与旧打印调查补充不入本提交；混合模块清单/测试文档按暂存快照单独复核。

当前验证：`cloud-config-precommit-build.log`构建退出0；`cloud-config-precommit-protocol.log`协议10例89断言通过；`cloud-config-precommit-lifecycle.log`独立wx 43检查0失败；`cloud-config-precommit-cli-help.log`启动退出0；msgfmt检查/本地MO生成通过。源码/二进制/资源身份见 `build/diagnostics/cloud-config-precommit-identity.json`。实际云端写入及Windows仍待用户，自动化不替代这些场景。

追加GUI场景：在测试账号登录过期时进入保存/上传，登录窗口应在当前表单上；原账号/环境恢复后可以继续，换账号或环境后应停止旧任务并要求重选目标；无已有方案目标的首次新建允许在原环境登录。后台401触发重登也遵守相同规则。此场景尚未实连服务验证；自动化覆盖会话判定和原生嵌套模态收尾。

选择性暂存24个相关文件；模块清单/测试工具正文仅纳入云配置部分，分别计算工作区和暂存区指纹。`--all`及`--staged`均35模块0问题，`git diff --cached --check`通过；无需修改或提交无关研究工具。未安装hook，不推送。
