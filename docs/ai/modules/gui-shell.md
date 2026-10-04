# GUI 生命周期与主工作台

<!-- module-review
{
  "id": "gui-shell",
  "source_fingerprint": "sha256:bdd134fa643d0b0e7a9e5bcb48dbfaaf1939bd70d67a117c43556a301388e300",
  "reason": "按 2026-10-04 用户约定补齐 GUI／CLI 同步检查、必要构建与测试分工、GUI 验证交接；只改维护规则，源码指纹及既有测试证据未变。"
}
-->

## 职责与边界

本模块负责GUI 生命周期与主工作台。归属以[模块清单](../modules.json)的 `gui-shell` 为准。

## 入口与调用

- [GUI_App.cpp](../../../src/slic3r/GUI/GUI_App.cpp)：on_init_inner/post_init；应用、配置、窗口与网络启动。
- [MainFrame.cpp](../../../src/slic3r/GUI/MainFrame.cpp)：主窗口页面、菜单与工作台生命周期。
- [Plater.cpp](../../../src/slic3r/GUI/Plater.cpp)：模型、侧栏、盘列表、切片和保存的 GUI 集成点。

## 核心流程

`GUI_App::on_init_inner()` 初始化配置、资源、窗口及网络基础设施，`post_init()` 继续加载。MainFrame 管理准备／预览／设备／项目页面，Plater 持有模型、侧栏、盘列表和后台切片状态；按钮由当前模型及任务状态决定可用性。

## 配置与约束

AppConfig 保存应用偏好，PresetBundle 保存当前工艺／设备／耗材选择；两者不可互相替代。设备品牌、语言、单实例及文件关联会改变启动分支。Plater.cpp 还是多个业务的集成文件，改动必须检查下游模块。

## 失败与取消

启动失败、历史工程失效、预设警告和本次切片失败应分别记录。窗口销毁后不能继续使用异步回调中的裸 UI 指针；后台异常应通过主线程通知，不在工作线程操作控件。

## 修改与验证

改菜单、页面切换或启动时，验证首次启动、已有配置启动、打开项目和关闭取消；按[交接规范](../gui-validation.md) 输出并保存本次实际输入、操作顺序、预期和日志路径。两端都有的业务同时对照 CLI；纯窗口交互可说明 CLI 不适用。

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[参数面板与对象覆盖](gui-parameters.md)、[场景、对象与交互编辑](scene-editing.md)、[后台切片、任务和取消](jobs.md)、[工程保存、撤销和恢复](project-state.md)、[账号、云端导入与模型服务](cloud.md)。
