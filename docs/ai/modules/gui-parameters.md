# 参数面板与对象覆盖

<!-- module-review
{
  "id": "gui-parameters",
  "source_fingerprint": "sha256:260c122d384f64d5c72c63b709a54c0f10c29304ec371067b9b5edaf5067ba79",
  "reason": "合并云预设按机型隔离及三档旧标准名称兼容；共享Preset接口追加可选目录，默认调用和GUI参数联动不变。保留CLI默认关闭的repair_evidence/close_boundaries选项，它们不属于工艺键或对象覆盖，不新增Tab控件。合并构建/测试及GUI待验边界见主记录。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。"
}
-->

## 职责与边界

本模块负责参数面板与对象覆盖。归属以[模块清单](../modules.json)的 `gui-parameters` 为准。

## 入口与调用

- [Tab.cpp](../../../src/slic3r/GUI/Tab.cpp)：`Tab` 及机器／耗材／工艺子类构建参数页，管理预设切换、脏状态及修改回调。
- [ParamsPanel.cpp](../../../src/slic3r/GUI/ParamsPanel.cpp)：`ParamsPanel` 组织全局／对象参数面板与布局、搜索高亮和选择状态。
- [GUI_ObjectSettings.cpp](../../../src/slic3r/GUI/GUI_ObjectSettings.cpp)：`ObjectSettings::update_settings_list` 从当前选择刷新对象覆盖项；先核对对象／体积索引。
- [ConfigManipulation.cpp](../../../src/slic3r/GUI/ConfigManipulation.cpp)：`ConfigManipulation::apply` 应用参数联动，温度等专用检查和 toggle 接口控制有效字段。

## 核心流程

Tab 构造参数页及预设编辑状态，OptionsGroup／Field 处理值和显示单位；ParamsPanel 在全局与对象范围切换，GUI_ObjectSettings／ObjectTable 把覆盖写回模型。ConfigManipulation 根据组合规则调整可见／可用状态并提示不合法组合。

## 配置与约束

显示默认值、父预设继承值和对象显式覆盖必须区分；0 只有在具体选项定义允许时才有继承含义。内部单位及序列化键由 PrintConfig 决定，不能用控件文案推测。修改一项后需同步脏标记及后台失效；同时检查 CLI 反序列化后的相同有效值与处理规则，不能只让 GUI 控件正确。

主界面耗材下拉框 `PlaterPresetComboBox::get_preset_name` 默认可显示 alias；EP7、EPONE 系列及 Advancer3 保留完整预设名，使 `@机型`／喷嘴标识可见。耗材编辑窗口本身已使用完整名；向导与耗材管理网页的显示逻辑另见[向导模块](guide.md)。此规则只改变 GUI 标签，不改预设身份、兼容性或 CLI 参数。

## 失败与取消

Tab 的脏状态页签刷新通过自身 `CallAfter` 排队，销毁 Tab 时一并清理未执行事件；执行前检查正在销毁状态及页签控件。不可改回携带裸 this 的应用全局队列，parent 非空不是对象仍存活的证明。此为 GUI 生命周期逻辑，CLI 无页签回调；动态参数有限切换回归及原偶发现场限制见[验证记录](../validation/2026-10-09-adv3-custom-feasibility.md)。

选中对象被删除或参数页重建时，事件可能晚到；先验证对象和页状态。批量更新有防递归处理，不应在 change 回调里反复重新选择预设。

## 修改与验证

改控件或文案需中英文、恢复默认、对象覆盖、保存／重开验证。墙长“倍”是相对于路径宽度的系数，GUI 文案修正不等于 CLI 参数数值迁移；分支差异见墙模块。具体参数入口、设置值与保存重开步骤按[GUI 交接规范](../gui-validation.md) 写到本轮验证 MD。

现有测试/工具范围：`tests/libslic3r/test_config.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

云端标准档导入优先匹配带 `[standard]` 的名称，再兼容旧无后缀名称；快速／精细独立匹配各自档位。兼容命中的旧标准工艺只有成功从云端导入／替换后才在列表显示 `[standard]`，内部名称、文件及云来源键保持原样，已有后缀不重复添加。仅上传形成关联不增加显示标记；准备页与参数页按条目索引映射真实预设名，更新修改标记也使用真实名称，避免显示重名时选错工艺。标签不会自动更改层高、速度等参数，实际配置来自所选档的3MF或其内嵌参数。导入后沿用原业务恢复先前的工艺选择；用户手动切到实际导入工艺并修改时，脏状态按其云来源匹配。机器／耗材替换及另存确认仍走既有流程，详见[云模块](cloud.md)。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[配置向导与预设创建](guide.md)、[FFF 切片步骤与层状态](slicing.md)、[周长墙与 Arachne 可变线宽](walls.md)。
