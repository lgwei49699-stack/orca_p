# 配置向导与预设创建

<!-- module-review
{
  "id": "guide",
  "source_fingerprint": "sha256:58817685f767d32ac5c2cc0f8d1f66c626c5a2285583f7ea545dca85ae0fa67a",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责配置向导与预设创建。归属以[模块清单](../modules.json)的 `guide` 为准。

## 入口与调用

- [WebGuideDialog.cpp](../../../src/slic3r/GUI/WebGuideDialog.cpp)：GuideFrame 加载品牌 JSON 并 apply_config。
- [ConfigWizard.cpp](../../../src/slic3r/GUI/ConfigWizard.cpp)：`BundleMap::load` 与 `PrinterPicker` 组织原生配置向导的品牌／机型选择；与网页向导分开追踪。
- [CreatePresetsDialog.cpp](../../../src/slic3r/GUI/CreatePresetsDialog.cpp)：机器／耗材等预设创建对话框，负责名称、来源和可兼容目标的选择与保存。
- [SavePresetDialog.cpp](../../../src/slic3r/GUI/SavePresetDialog.cpp)：`SavePresetDialog::Item` 校验另存名称并接受选择，保存前处理冲突与有效性。
- [GuideMachineMetadata.hpp](../../../src/libslic3r/GuideMachineMetadata.hpp)：品牌目录内递归机器继承和喷嘴校验。

## 核心流程

GuideFrame 读取品牌目录，组织机型、喷嘴、耗材 JSON 供向导网页显示；apply_config 将选择落到 AppConfig/PresetBundle。ConfigWizard 与创建／保存预设对话框仍是不同入口，不能以当前选中打印机推算目录里所有机器。

## 配置与约束

向导的显示名称、machine 的 printer_model/nozzle_diameter、model_id 与设备类型各有用途。系统目录与资源目录的优先级需保持，隐藏某个机型不等于删除旧用户已启用配置。

## 失败与取消

坏机器、继承循环、数值解析失败需要记录具体项并隔离；是否支持完整隔离由本分支实现决定。保存／取消向导时要核对旧选择是否被意外清除。

## 修改与验证

验证直接值、继承、多级覆盖、坏项及 EP3 Plus／EPONE Pro／Plus；源码测试不等于向导 GUI 已验证，需隔离配置目录检查首次及升级启动。

现有测试/工具范围：`tests/libslic3r/test_epone_presets.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支使用独立 GuideMachineMetadataResolver，在当前品牌机器清单递归解析父项，子项覆盖；检测缺父项/循环、喷嘴必须有限正数，保留字符串 1.0 等有效拼写；坏机器单项跳过。它不读取当前选中的机器。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[打印机、耗材与工艺资源](resource-profiles.md)、[内嵌网页与原生消息桥](web-assets.md)、[GUI 生命周期与主工作台](gui-shell.md)。
