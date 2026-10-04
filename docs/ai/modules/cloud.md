# 账号、云端导入与模型服务

<!-- module-review
{
  "id": "cloud",
  "source_fingerprint": "sha256:cc07a5ab49638e7646e19e9c8cef2a34bd026823795791cfd0ceeba42ecb9323",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
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
