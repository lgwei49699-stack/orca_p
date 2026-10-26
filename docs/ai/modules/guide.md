# 配置向导与预设创建

<!-- module-review
{
  "id": "guide",
  "source_fingerprint": "sha256:1054231dfd4d86a0a77e409d9f1a83f4f5e098c48edba65451253ae2e38712fc",
  "reason": "共享 test_epone_presets 新增云工艺值恢复回归，向导身份、机型继承解析和预设创建入口未修改，无需改正文。"
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

网页向导的常规 info 日志只记录品牌数量、最终 model/machine/filament/process 数量和选择/保存/取消等摘要；逐耗材路径与正常继承查找为 debug，trace 的文件/消息只记录大小及命令，不输出完整配置正文或完整收发脚本。不为写日志额外序列化整份配置，网页仍接收原有完整数据。解析及坏项 error 保留；不修改全局日志等级，不影响模型修复的 info 诊断。

## 配置与约束

向导的显示名称、machine 的 printer_model/nozzle_diameter、model_id 与设备类型各有用途。系统目录与资源目录的优先级需保持，隐藏某个机型不等于删除旧用户已启用配置。

## 失败与取消

坏机器、继承循环、数值解析失败需要记录具体项并隔离；是否支持完整隔离由本分支实现决定。保存／取消向导时要核对旧选择是否被意外清除。

## 修改与验证

验证直接值、继承、多级覆盖、坏项及 EP3 Plus／EPONE Pro／Plus；源码测试不等于向导 GUI 已验证，需隔离配置目录检查首次及升级启动。

现有测试/工具范围：`tests/libslic3r/test_epone_presets.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

日志降噪只涉及 GUI 向导，不涉及 CLI 或共享配置加载规则。除必要应用构建和 `[Preset][Guide]` 专项外，手测应打开打印机向导确认列表、搜索、取消/确认及日志摘要；回归结果见[当前执行记录的向导日志章节](../validation/2026-10-04-smart-print-engine.md)。静态“非日志业务语句未变”核对不能替代网页交互验收。

2026-10-05 已使用新打包的 macOS arm64 应用完成上述最小 GUI 场景，选择配置的规范化摘要哈希前后一致，完整 JSON 日志不再出现；详细程序身份、日志及范围见同一记录。此结果不覆盖首次安装、其他平台或所有坏预设路径。

## 分支现状

本分支使用独立 GuideMachineMetadataResolver，在当前品牌机器清单递归解析父项，子项覆盖；检测缺父项/循环、喷嘴必须有限正数，保留字符串 1.0 等有效拼写；坏机器单项跳过。它不读取当前选中的机器。

Advancer3 在 Custom 目录声明 0.25／0.4／0.6／0.8 mm 喷嘴，使用本品牌内完整参数，无跨品牌机器继承。`tests/libslic3r/test_advancer3_presets.cpp` 检查目录解析和床／封面资源；首次添加、旧配置升级及重启后选择保持见[GUI 交接](../validation/2026-10-09-adv3-custom-feasibility.md)。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[打印机、耗材与工艺资源](resource-profiles.md)、[内嵌网页与原生消息桥](web-assets.md)、[GUI 生命周期与主工作台](gui-shell.md)。

首次向导（22）与耗材管理（23）共用完整预设名称列表，保留 @Advancer3 等机型身份并精确保存选择；不再按同名 Generic 简称跨机型合并。`node --test tests/web/guide_filaments.test.cjs` 覆盖过滤、默认值、选择保持及两个入口的桥接数据。
