# 账号、云端导入与模型服务

<!-- module-review
{
  "id": "cloud",
  "source_fingerprint": "sha256:be283efd3b4d0a48f0d3db033cd4476ca52653c8c3eea7bd9ce8975386bd7e98",
  "reason": "Advancer3 调平默认开启，保留主动关闭及其他机型协议；区分映射与命令发送前取消、失败及结果未知；共用关闭守卫防止主窗口销毁栈上模态。更新正文与日志分析、测试及 GUI 交接。"
}
-->

## 职责与边界

本模块负责账号、云端导入与模型服务。归属以[模块清单](../modules.json)的 `cloud` 为准。

## 入口与调用

- [GFDAuthManager.cpp](../../../src/slic3r/GUI/GFDAuthManager.cpp)：会话验证及带认证请求。
- [GFDModalDialog.cpp](../../../src/slic3r/GUI/GFDModalDialog.cpp)：Windows 设备选择专用模态循环与窗口禁用作用域；非 Windows 设备选择继续使用原生 wxDialog。
- [GFDDeviceSelectionDialog.cpp](../../../src/slic3r/GUI/GFDDeviceSelectionDialog.cpp)：`GFDDeviceSelectionDialog` 组织设备与任务选择；设备能力及参数同步入口受分支约束。
- [ModelMall.cpp](../../../src/slic3r/GUI/ModelMall.cpp)：`ModelMallDialog` 承载模型商城网页，导航与 `OnScriptMessage` 处理网页消息。
- [Downloader.cpp](../../../src/slic3r/GUI/Downloader.cpp)：`Downloader::start_download` 启动下载；`Download` 提供暂停、恢复和取消，完成事件交回界面。
- [GFDConfig.cpp](../../../src/slic3r/Utils/GFDConfig.cpp)：环境、认证配置和设备按钮能力。

## 核心流程

GFDAuthManager 管理会话并包装需要认证的请求；GFDConfig 提供环境、地址和设备按钮配置。下载器／ModelMall 获取模型，设备选择和耗材映射对话框组织云任务；参数导入和上传的可用入口随版本分支不同。

GFD 当前盘打印的 `start_gfd_print_export` 在 3MF 模式向共享 `export_3mf` 传明确零基盘号和 `WithGcode + SkipModel`，不经过传统 `send_gcode_legacy`。新切片包校验仅要求目标盘有效且 G-code 非空，其他盘未切片不应拦截单盘；导出失败不会进入上传。纯 G-code 调度、已导入切片文件直传、完整云配置工程导出及后端接口没有随此批修改。共享文件回归不等于真实云端/设备下发已验证。

仅 `deviceType=Advancer3` 的普通 G-code 下发会先请求 PMC `/app/pmc/api/v1/deviceCmd/deviceStatus?deviceId=<MAC>`，从 `matlStationInfo.slotInfos` 读取 1～4 号已装载槽位及真实材料／颜色。共享映射窗口在这一分支显示“打印前调平”（默认开启，可在本次下发取消勾选），按当前盘实际使用的零基 T 编号逐项映射；查询失败、空槽列表或不完整数据均阻止上传，不使用通用耗材接口的空列表跳过确认。新协议解析／校验／请求构造位于 [GFDPrintOptions.hpp](../../../src/slic3r/Utils/GFDPrintOptions.hpp)。

Advancer3 请求使用 MAC 作为 `deviceId`，发送 `levelingBeforePrint`、`ms[{slot,tNum,mt,sRgb,tRgb}]`、不含扩展名的 `fileName` 和 `source=slicer`；实体颜色为 sRgb，切片颜色为 tRgb，保留稀疏 T 编号和多个 T 映射同槽。此确认仅支持单台 Advancer3 的 G-code；其他机型（含 AD5X）的普通 G-code／3MF 入口和 `consumable` 数组协议保持原逻辑。重登重试继续使用同一请求 ID／选项；在确认框取消不启动上传，完成回调仍检查窗口存活与 submission ID。CLI 没有此云端打印命令，因此仅 GUI 接入；CLI 切片结果和 G-code 生成规则未修改。

## 配置与约束

认证 token、已保存登录信息、环境和用户本地预设分属不同数据。云端 preset ID 与本地名称／继承不能简单互换；导入时必须绑定当前目标机型并保留用户可审阅的替换流程。

打印设备列表在 QA／正式环境均使用 modelcenter 的 `/app/print3dModel/manage/v1/md/query`，由 `device_query_url` 单独选择地址。不得改回通用业务网关的 `/app/print3d/manage/v1/md/query`：二者返回不同设备集合，仅将旧网关域名换成 qa-print 也无法补齐设备。认证、云方案、动态耗材、耗材槽位、上传授权及打印命令继续使用各自原有地址；本次只读对照和目标设备字段检查见[Advancer3 记录](../validation/2026-10-09-adv3-custom-feasibility.md)。CLI 无此云设备选择功能。

## 失败与取消

打印设置取消单独返回中性“已取消打印下发”状态，恢复选择与确认按钮；导出／查询／上传在命令发出前取消也走此状态，不再显示下发失败或错误弹窗。已经发送命令但响应不明／已有部分成功仍按原逻辑提示并关闭选择框，避免重复打印。主窗口退出守卫在子模态仍运行时否决普通退出，先完成弹窗退栈再销毁父窗口，见 [GUI 生命周期](gui-shell.md)。

动态耗材参数窗口复用独立的 TabFilament；切换机型／耗材会重新加载当前预设，脏状态延迟刷新归属 Tab 自身。关闭对话框时除取消网络请求，也必须避免界面延迟回调引用已销毁页签。Advancer3 保持自身云端类型；响应中没有对应动态参数时使用当前本地参数，不借用 AD5X 的云端数据。HTTP 200 只表示请求完成，不保证后续 UI 回调安全，调查和验证边界见[Advancer3 记录](../validation/2026-10-09-adv3-custom-feasibility.md)。

认证失败可能触发重试或重新登录，普通请求失败不能无限重登。取消／关闭对话框后的完成回调应先检查存活状态，日志不能带敏感凭据。

## 修改与验证

改认证／导入时测试失效会话、坏响应、取消、同名预设和机型不兼容；云服务不可用时仍能编辑本地工程。用户版已关闭的同步入口不能在文档中写成存在。

现有测试/工具范围：`gfd_modal_dialog_tests` 用真实 wx 事件循环覆盖正常内外层依次关闭、提前隐藏、嵌套期间结束、取消、早期结束和重复打开；配置 `SLIC3R_GUI_TESTS=ON` 后启用，要求交互桌面。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

下发成功后界面点不动的证据见 [日志与 Windows 现场调查](../validation/2026-10-08-print-ui-freeze-log-analysis.md)。10 月 9 日同事现场已证实主窗口处于原生 disabled、打印机选择窗口 hidden 但仍存活，成功提示已无窗口；重点核对设备选择的模态退出与主窗口恢复。对应 PID 19568 日志已补齐：08:56:44 finalize 返回后，主线程仍持续处理周期消息至 10:07；09:10 的 inactive 是 15 分钟活动超时，不是切换应用的证据。10:21 同进程转储进一步确认设备选择 ShowModal 未返回、shouldExit=false、windowDisabler 未释放，主窗口 wx enabled=false；隐藏模态循环残留的机制已确认。具体先行事件／退出提前返回分支仍未确认。现已修改 Windows 设备选择模态收尾：所属循环使用 ScheduleExit，已隐藏或暂非活动循环时也能记住退出请求；窗口禁用作用域在该循环真正退栈后释放，并在隐藏前恢复窗口状态。重复结束保留首次结果，初始化中结束不进入事件分发；选择框开始结束后不接受再次确认。新增请求结束／返回日志，不改变服务协议、G-code/3MF 导出或失败重试，CLI 无对应模态交互。验证范围及 Windows 待验项见同一记录，Mac 公共循环测试不能替代 Windows 焦点和窗口切换验收。用户 10 月 9 日 11:28–11:36 实际运行日志已确认一次取消及一次成功下发的 modal returned，均为 shown=false、modal=false、parent_enabled=true；没有长期隐藏模态残留，应用仍运行时尚无主窗口退出证据。公开版本更新为 2.3.2.2-Release 后提交；后续 Windows 虚拟机日志确认一次成功下发后正常返回，仍不能替代同事原机偶现场景与完整 Windows 验收。

设备加载和打印进度使用列表下方独立的状态文字／进度条行；预留进度条隐藏时的空间，文字不自动调整窗口尺寸，长文本末尾省略。进度回调只在文字、颜色或确定进度值变化时更新控件，不再对整个对话框 Layout 或强制 Refresh/Update，也不重复设置 Gauge 范围。查询和未知进度保留 120ms 动画，3MF 导出回调仍可推进 Gauge，因为导出等待只泵 UI／输入事件。相同加载状态直接返回；错误时恢复控件和隐藏进度条，完整错误仍由原有错误对话框展示。13:42 Windows 虚拟机新构建日志确认普通 G-code 下发后 modal returned、parent_enabled=true，用户明确确认进度／列表已不闪烁、操作正常；本轮普通下发和加载视觉通过。其余取消／失败／3MF／主题及 DPI 场景仍见[加载闪烁优化记录](../validation/2026-10-08-print-ui-freeze-log-analysis.md#加载闪烁优化实施)，不将一次手测扩写为所有环境验收。

## 分支现状

本分支保留工艺/耗材云端导入和上传入口及相关替换/另存流程；要核对 GFD 按钮配置和当前机型绑定，不能把用户版去入口的行为当成此分支。

云配置使用 `GFDCloudConfigInfo::scheme` 保留方案及 `profiles`，通过 `fineness=standard/fast/fine` 选择实际文件。旧顶层文件兼容标准档；重复／坏档位拒绝，未知档位保留但不供用户选择。纯协议逻辑集中在 [GFDCloudConfig.hpp](../../../src/slic3r/Utils/GFDCloudConfig.hpp)，不把档位标签自动换算成层高或速度。

上传可新建标准档，或为当前机型已有方案补充／覆盖指定档位；两个入口以内容宽度紧凑排列并显示当前模式，“上传到已有方案”明确用于补充／替换类型，选择已有方案默认标准。已有档提示覆盖，缺档提示新增；名称与备注可编辑，界面提示其为整个方案共用。上传表单完成校验后点“确定”即进入上传流程，不再追加“确认保存”的是／否弹框。只提交用户实际改动的元数据字段，空备注可用于清除，未改字段保留写前最新详情值。保存弹框显示方案和配置类型下拉框，默认导入档位，也可选择另一个目标档。保存前通过同域名 `/slice-param-config/detail?id=...` GET 读取最新方案，校验 ID／机型／orca 类型；只替换目标档，顶层文件始终指向标准档。保存后回读核对各档文件及明确修改的名称／备注，后端忽略更新或详情失败不误报成功，不自动重放结果不明的写请求。无后端条件更新契约时不能保证并发原子性。未修改 QA／正式域名和既有 add/edit 路径。

导入列表按方案列出三档，缺档不能导入；整项高度按三档内容及备注换行高度计算；备注按实际列宽从原文换行，缩放后恢复正确行数，不使用省略号。表头固定在滚动内容外，按滚动条实际宽度补齐右侧间隔，表头／正文同列宽；类型使用非交互的彩色细边框标签。上传目标选择器每方案只有一个选择按钮并默认返回标准，档位在上传窗口选择。底栏将状态、水平居中的 Gauge、取消放在同一行，左右等宽、隐藏 Gauge 仍占位，缩小留白且加载不改变列表高度；取消／刷新使用灰色背景变化表达焦点，无绿色焦点边框或淡绿底色。保留取消、代次及会话防过期检查。导入工艺名带稳定 `[standard]`／`[fast]`／`[fine]` 后缀，来源记录保留实际保存档位和新记录的环境／账号；旧记录无档位、null或空串按 standard 兼容，明确未知值拒绝。旧记录缺环境／账号仍沿用历史当前会话语义。保存到其他档位不自动重命名当前本地工艺。经全仓调用核对后移除未使用的同步上传／保存封装，GUI统一使用异步链路；CLI没有相应云方案管理命令，共享3MF导出与切片算法不变。

保存窗口使用普通表单布局，无消息框大图标；保存和上传共用 `GFDCloudConfigDialog`，提交后保留窗口并锁定表单，防止重复提交和忙时关闭/取消。底部预留状态行与居中Gauge，不随进度反复布局。准备/授权/保存/回读阶段使用不定进度，文件上传使用HTTP已传字节百分比；文件100%不代表保存完成。后台阶段通过 `Job::Ctl::call_on_main_thread` 投递，存活标记和忙状态丢弃迟到回调。回读成功后携带服务器方案名与实际档位，先结束原表单模态循环，再显示成功提示；失败不显示成功。导入文件准备完成、进入参数确认/应用前停止底部动画和清空状态，但仍锁定列表直到应用返回，避免成功框背后残留“准备中”。独立 `gfd_cloud_config_dialog_tests` 验证上述提交/模态生命周期；不覆盖真实网络、实际表单视觉或Windows原机。

提交前审查补齐重登边界：初次登录恢复及后台401刷新后，先在GUI线程核对原环境/账号，再进入导出或重放业务请求。只有原账号为空且未选已有方案的新建流程允许登录建立首次身份；已有方案不能携带旧ID换账号/环境继续保存。登录弹窗以当前保存/上传表单为父窗口，结果仍在原表单退栈后显示。

独立 `gfd_cloud_config_tests` 已有三档隔离、旧数据兼容、损坏结构和保存回读回归；测试结果、当前构建及 GUI 待验清单见 [三档改造记录](../validation/2026-10-09-cloud-config-quality-analysis.md)。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[设备、AMS 与打印任务监控](devices.md)、[网络传输与打印主机](network-hosts.md)、[参数面板与对象覆盖](gui-parameters.md)。
