# 配置定义、预设继承与资源更新

<!-- module-review
{
  "id": "presets",
  "source_fingerprint": "sha256:b0f214684e130416625de3e3b796ef0e1aed76c07e1ff47abaa085acc972fb35",
  "reason": "合并云导入预设的gfd-cloud机器子目录、保存/重载与三档旧名称兼容，默认save_current_preset调用保持原公共目录。保留CLI专用默认关闭的repair_evidence/close_boundaries选项，不把其纳入机器/耗材/工艺配置。正文补存储边界，合并后验证见主记录。 10-17 仅CLI misc新增默认关闭model_stage_report及默认空model_reference，不属于工艺/机器/耗材参数，未新增GUI面板或配置覆盖。 10-18 CLIMiscConfigDef仅新增默认false的slice_analysis_report，不加入有效打印参数、预设持久化或GUI面板；既有默认值和覆盖规则未改。 2026-10-22仅CLIMisc新增optimize_model_detail_paths=false，不加入PrintConfig/PrintObjectConfig、预设和工程持久化；GUI按用户要求默认关闭。原云预设、资源默认值及覆盖规则保留，正文补运行时边界。 2026-10-22仅CLIMisc新增variable_layer_height_min/max默认0，未改PrintConfig/PrintObjectConfig有效配置、资源或持久化；正文补显式计划边界。 2026-10-23仅在CLITransform注册默认空的model_layer_height_plan及英文/简中帮助，非普通打印配置，不改预设继承/默认值或GUI面板；CLI契约与回归见进展记录。"
}
-->

## 职责与边界

本模块负责配置定义、预设继承与资源更新。归属以[模块清单](../modules.json)的 `presets` 为准。

## 入口与调用

- [PrintConfig.cpp](../../../src/libslic3r/PrintConfig.cpp)：配置定义、默认值、单位和提示的集中入口。
- [Config.cpp](../../../src/libslic3r/Config.cpp)：`ConfigBase::load*`／`set_deserialize` 解析参数、处理替换规则；`apply_only` 合并指定键。
- [Preset.cpp](../../../src/libslic3r/Preset.cpp)：`PresetCollection` 管理加载、选择和继承；`update_map_system_profile_renamed` 建立旧名称迁移映射并报告重复。
- [PresetBundle.cpp](../../../src/libslic3r/PresetBundle.cpp)：load_presets/load_selections/full_config；预设与有效配置。
- [AppConfig.cpp](../../../src/libslic3r/AppConfig.cpp)：`AppConfig::load`／`save` 持久化应用偏好及选择；save 要求主线程，先写带 PID 的临时文件。
- [PresetUpdater.cpp](../../../src/slic3r/Utils/PresetUpdater.cpp)：`PresetUpdater::sync` 获取更新；`config_update` 安装资源并经 reload_configs_update_gui 重新加载。

## 核心流程

`PresetBundle::load_presets` 先加载系统品牌 JSON，再按 AppConfig 的 preset_folder 加载用户预设；随后更新多材料选择、兼容状态，并由 `load_selections` 恢复选择。工程嵌入预设另经 PresetCollection 的对应入口进入，不能把向导列表当作全部运行时预设。

云端导入的用户工艺和耗材按机器存放在各自预设目录的 `gfd-cloud/<机器名>/` 下。`PresetCollection::load_presets` 会在加载普通用户预设后读取这些子目录；`save_user_presets` 保留已存在的机器目录，避免下次保存时把文件移回公共目录。内存中的预设名称仍全局唯一，磁盘目录隔离不能代替名称和兼容机器检查。

`full_config` 按 FFF／SLA 分派。FFF 先叠加默认配置、当前工艺、默认耗材、当前机器和 project_config，再按单／多耗材分支填入耗材值。对象／体积／层范围的覆盖继续由切片应用配置阶段处理；排查时记录最终有效值，而不只看 JSON 父项。

`PresetUpdater::sync` 获取更新，`config_update` 处理安装及通知；重新加载前检查未保存预设。AppConfig 保存应用偏好／启用选择，PrintConfig 描述打印参数，两者不是同一份配置。

## 配置与约束

`variable_layer_height_min/max`仅属于CLIMisc，默认0，不是机型/耗材能力标定、不写入工艺/机器/项目配置。显式非零时与adaptive切片配对，原机型上下界仍约束生成器；GUI不新增控件或调用。普通CLI、省略参数的旧adaptive调用与资源默认值不变，实际验收见[质量策略记录](../validation/2026-10-22-bounded-quality-policy.md)。

`optimize_model_detail_paths`仅加入CLIMiscConfigDef，默认false，不进入PrintConfig/PrintObjectConfig、工艺或机料预设、对象覆盖及工程持久化。CLI显式开启后写入本次Print运行时状态；GUI按用户要求不接入，始终默认关闭。它独立于`slice_analysis_report`，需要切片动作且开启时拒绝无模式身份的切片缓存导入/导出；旧调用不受该限制。没有修改既有配置默认值或资源预设。

`slice_analysis_report` 只加入 CLIMiscConfigDef，默认 false，不进入打印机/耗材/工艺配置、持久预设或 GUI 参数面板。它控制只读报告输出，不更改原生优化功能的默认值或有效参数覆盖顺序。见[实施记录](../validation/2026-10-18-slice-analysis.md)。

`model_stage_report`（默认false）及`model_reference`（默认空）仅在CLI misc定义中供业务报告使用；不属于打印机、耗材或工艺参数，不加入GUI面板/持久预设，也不更改修复或切片默认值。后端阈值与流程版本由PDS保存，Orca不新增业务选择预设。

父项提供基础、子项显式值覆盖；机器／耗材／工艺及模型覆盖由不同阶段合成。renamed_from 是旧名称迁移别名，不是当前默认耗材选择。资源版本变化也不等于已覆盖用户本地参数。

## 失败与取消

Config 的反序列化与兼容替换规则决定未知／旧值如何处理；系统与用户配置可能采用不同替换策略。`update_map_system_profile_renamed` 对重复旧名称映射记录错误；它不直接改变当前耗材的打印数值。

PresetUpdater 在重载前经 check_and_save_current_preset_changes 处理未保存修改，用户取消后不继续重载。AppConfig::save 要求主线程并先写带 PID 的临时文件。缺父项、循环或坏 JSON 的隔离范围要回到实际加载入口；[向导](guide.md) 的单项跳过能力不能推断为所有加载路径共有。

## 修改与验证

改键/默认值需测试序列化、合法 0、继承、旧工程兼容及资源升级；修改资源包元信息时审查 version 与实际差异，不能顺便改打印参数。

现有测试/工具范围：`tests/libslic3r/test_config.cpp`；`tests/libslic3r/test_epone_presets.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

Advancer3 资源通过共享 PresetBundle 验证：仅安装 Custom 与 OrcaFilamentLibrary 时即可加载，不要求 Flashforge；与 Flashforge 同时安装时也验证无重名错误。测试覆盖全部四规格，比较解析后的机器／工艺／耗材全部打印参数，只排除身份、默认预设及兼容引用；见 `tests/libslic3r/test_advancer3_presets.cpp` 和[执行记录](../validation/2026-10-09-adv3-custom-feasibility.md)。

## 关联模块

[配置向导与预设创建](guide.md)、[参数面板与对象覆盖](gui-parameters.md)、[打印机、耗材与工艺资源](resource-profiles.md)、[应用入口与 CLI](cli.md)。
