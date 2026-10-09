# 账号、云端导入与模型服务

<!-- module-review
{
  "id": "cloud",
  "source_fingerprint": "sha256:945f7545403f76ae28a112f746dd054b0a9b2a6f690ae963db6e2cfc0e47578c",
  "reason": "设备选择加载改为底部固定状态行，移除列表覆盖面板、重复文字、进度回调全窗布局及同步重绘；保留不确定动画、错误恢复与模态退出，CLI无对应控件。Mac最小构建通过；12:57用户日志连续两次下发及模态恢复正常。既有模态测试身份未变，仅复用其范围；Windows加载视觉仍待手测。"
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

## 配置与约束

认证 token、已保存登录信息、环境和用户本地预设分属不同数据。云端 preset ID 与本地名称／继承不能简单互换；导入时必须绑定当前目标机型并保留用户可审阅的替换流程。

## 失败与取消

认证失败可能触发重试或重新登录，普通请求失败不能无限重登。取消／关闭对话框后的完成回调应先检查存活状态，日志不能带敏感凭据。

## 修改与验证

改认证／导入时测试失效会话、坏响应、取消、同名预设和机型不兼容；云服务不可用时仍能编辑本地工程。用户版已关闭的同步入口不能在文档中写成存在。

现有测试/工具范围：`gfd_modal_dialog_tests` 用真实 wx 事件循环覆盖正常内外层依次关闭、提前隐藏、嵌套期间结束、取消、早期结束和重复打开；配置 `SLIC3R_GUI_TESTS=ON` 后启用，要求交互桌面。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

下发成功后界面点不动的证据见 [日志与 Windows 现场调查](../validation/2026-10-08-print-ui-freeze-log-analysis.md)。10 月 9 日同事现场已证实主窗口处于原生 disabled、打印机选择窗口 hidden 但仍存活，成功提示已无窗口；重点核对设备选择的模态退出与主窗口恢复。对应 PID 19568 日志已补齐：08:56:44 finalize 返回后，主线程仍持续处理周期消息至 10:07；09:10 的 inactive 是 15 分钟活动超时，不是切换应用的证据。10:21 同进程转储进一步确认设备选择 ShowModal 未返回、shouldExit=false、windowDisabler 未释放，主窗口 wx enabled=false；隐藏模态循环残留的机制已确认。具体先行事件／退出提前返回分支仍未确认。现已修改 Windows 设备选择模态收尾：所属循环使用 ScheduleExit，已隐藏或暂非活动循环时也能记住退出请求；窗口禁用作用域在该循环真正退栈后释放，并在隐藏前恢复窗口状态。重复结束保留首次结果，初始化中结束不进入事件分发；选择框开始结束后不接受再次确认。新增请求结束／返回日志，不改变服务协议、G-code/3MF 导出或失败重试，CLI 无对应模态交互。验证范围及 Windows 待验项见同一记录，Mac 公共循环测试不能替代 Windows 焦点和窗口切换验收。用户 10 月 9 日 11:28–11:36 实际运行日志已确认一次取消及一次成功下发的 modal returned，均为 shown=false、modal=false、parent_enabled=true；没有长期隐藏模态残留，应用仍运行时尚无主窗口退出证据。公开版本更新为 2.3.2.2-Release 后提交；后续 Windows 虚拟机日志确认一次成功下发后正常返回，仍不能替代同事原机偶现场景与完整 Windows 验收。

设备加载和打印进度使用列表下方独立的状态文字／进度条行；预留进度条隐藏时的空间，文字不自动调整窗口尺寸，长文本末尾省略。进度回调只在文字、颜色或确定进度值变化时更新控件，不再对整个对话框 Layout 或强制 Refresh/Update，也不重复设置 Gauge 范围。查询和未知进度保留 120ms 动画，3MF 导出回调仍可推进 Gauge，因为导出等待只泵 UI／输入事件。相同加载状态直接返回；错误时恢复控件和隐藏进度条，完整错误仍由原有错误对话框展示。Windows 闪烁的实际改善需按[加载闪烁优化记录](../validation/2026-10-08-print-ui-freeze-log-analysis.md#加载闪烁优化实施)手测；不把构建或模态回归当作绘制验收。

## 分支现状

本分支保留工艺/耗材云端导入和上传入口及相关替换/另存流程；要核对 GFD 按钮配置和当前机型绑定，不能把用户版去入口的行为当成此分支。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[设备、AMS 与打印任务监控](devices.md)、[网络传输与打印主机](network-hosts.md)、[参数面板与对象覆盖](gui-parameters.md)。
