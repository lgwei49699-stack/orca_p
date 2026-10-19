# 内嵌网页与原生消息桥

<!-- module-review
{
  "id": "web-assets",
  "source_fingerprint": "sha256:bc24f00f3a67a55fe22c0846c11c920ea8d39e6a985dcd7cdadbfad2d90939ff",
  "reason": "22 与23耗材列表共用新 helper，完整显示和提交预设名，按机型及喷嘴精确过滤，保留选择状态及兼容旧短名默认值；12个Node用例通过，实际耗材窗口确认Advancer3普通及0.25专用标识。"
}
-->

## 职责与边界

本模块负责内嵌网页与原生消息桥。归属以[模块清单](../modules.json)的 `web-assets` 为准。

## 入口与调用

- [index.html](../../../resources/web/guide/index.html)：网页向导容器，以 iframe 装载步骤页，页面脚本经原生消息桥同步选择。
- [WebViewDialog.cpp](../../../src/slic3r/GUI/WebViewDialog.cpp)：`WebViewPanel` 管理内嵌网页的加载／刷新／导航；具体业务消息由对应对话框处理。

## 核心流程

resources/web 下 guide、homepage、login、model、orca 等页面在内嵌 WebView 展示，通过 JS 消息与原生 GuideFrame/WebViewDialog/模型页面交互；include 提供共用前端依赖。页面和原生 command 字段需配套修改。

## 配置与约束

向导页面 22 和耗材管理页面 23 共用 `guide/js/filament_selection.js` 构造系统材料列表。完整预设名保留 @ 后的机型和喷嘴标识，模型匹配使用完整 `[model++nozzle]`；初始选中状态在创建实际控件时设置，保存只发送已勾选的精确 key，不按简称扩展其他机型。首次向导默认材料兼容旧短名和新完整名称。耗材制造商仍来自原 vendor，不用打印机名称伪造厂商。

网页语言、资源相对路径、网络 URL 与原生会话有不同来源。网页提交的机型选择需由原生校验；不可把前端显示状态直接当已保存配置。

## 失败与取消

WebView 初始化失败、消息 JSON 错误、页面卸载后的回调和认证失败需区分。不要把后端凭据硬编码到静态 JS 或文档中。

## 修改与验证

改消息协议先核对原生接收分支和网页发送字段，再测向导保存、取消、登录与离线页面。原生单元测试无法证明页面渲染和桥接时序。

现有测试/工具范围：`node --test tests/web/guide_filaments.test.cjs` 验证两个耗材页面的预设身份、筛选、默认值及提交；GUI 样式、实际桥接仍按本节场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[配置向导与预设创建](guide.md)、[账号、云端导入与模型服务](cloud.md)、[GUI 生命周期与主工作台](gui-shell.md)、[中英文与翻译资源](localization.md)。
