# 内嵌网页与原生消息桥

<!-- module-review
{
  "id": "web-assets",
  "source_fingerprint": "sha256:8d800ec25994ff5223e63659ace1068c24b200e17ec0529c1e4331d43b682634",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
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

网页语言、资源相对路径、网络 URL 与原生会话有不同来源。网页提交的机型选择需由原生校验；不可把前端显示状态直接当已保存配置。

## 失败与取消

WebView 初始化失败、消息 JSON 错误、页面卸载后的回调和认证失败需区分。不要把后端凭据硬编码到静态 JS 或文档中。

## 修改与验证

改消息协议先核对原生接收分支和网页发送字段，再测向导保存、取消、登录与离线页面。原生单元测试无法证明页面渲染和桥接时序。

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[配置向导与预设创建](guide.md)、[账号、云端导入与模型服务](cloud.md)、[GUI 生命周期与主工作台](gui-shell.md)、[中英文与翻译资源](localization.md)。
