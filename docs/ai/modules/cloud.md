# 账号、云端导入与模型服务

<!-- module-review
{
  "id": "cloud",
  "source_fingerprint": "sha256:5f7a8e7e1c6eab0cbbea8855131cc3f7100f36e87338a4f6edf4d8abd9f494b2",
  "reason": "提交前确认GFD打印的3MF路径原本已传明确盘号，共享导出只校验目标盘，失败不进入上传；纯G-code调度及完整云配置工程、登录/后端协议不变。传统send_gcode补当前盘解析，未执行云端或设备发送，不将格式测试当作云回归。"
}
-->

## 职责与边界

本模块负责账号、云端导入与模型服务。归属以[模块清单](../modules.json)的 `cloud` 为准。

## 入口与调用

- [GFDAuthManager.cpp](../../../src/slic3r/GUI/GFDAuthManager.cpp)：会话验证及带认证请求。
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

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支保留工艺/耗材云端导入和上传入口及相关替换/另存流程；要核对 GFD 按钮配置和当前机型绑定，不能把用户版去入口的行为当成此分支。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[设备、AMS 与打印任务监控](devices.md)、[网络传输与打印主机](network-hosts.md)、[参数面板与对象覆盖](gui-parameters.md)。
