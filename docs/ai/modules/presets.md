# 配置定义、预设继承与资源更新

<!-- module-review
{
  "id": "presets",
  "source_fingerprint": "sha256:99002d6747e03f4d590ae945b0a7c8b793ff2ea4470b436c77fd19fe47a2e9ae",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
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

`full_config` 按 FFF／SLA 分派。FFF 先叠加默认配置、当前工艺、默认耗材、当前机器和 project_config，再按单／多耗材分支填入耗材值。对象／体积／层范围的覆盖继续由切片应用配置阶段处理；排查时记录最终有效值，而不只看 JSON 父项。

`PresetUpdater::sync` 获取更新，`config_update` 处理安装及通知；重新加载前检查未保存预设。AppConfig 保存应用偏好／启用选择，PrintConfig 描述打印参数，两者不是同一份配置。

## 配置与约束

父项提供基础、子项显式值覆盖；机器／耗材／工艺及模型覆盖由不同阶段合成。renamed_from 是旧名称迁移别名，不是当前默认耗材选择。资源版本变化也不等于已覆盖用户本地参数。

## 失败与取消

Config 的反序列化与兼容替换规则决定未知／旧值如何处理；系统与用户配置可能采用不同替换策略。`update_map_system_profile_renamed` 对重复旧名称映射记录错误；它不直接改变当前耗材的打印数值。

PresetUpdater 在重载前经 check_and_save_current_preset_changes 处理未保存修改，用户取消后不继续重载。AppConfig::save 要求主线程并先写带 PID 的临时文件。缺父项、循环或坏 JSON 的隔离范围要回到实际加载入口；[向导](guide.md) 的单项跳过能力不能推断为所有加载路径共有。

## 修改与验证

改键/默认值需测试序列化、合法 0、继承、旧工程兼容及资源升级；修改资源包元信息时审查 version 与实际差异，不能顺便改打印参数。

现有测试/工具范围：`tests/libslic3r/test_config.cpp`；`tests/libslic3r/test_epone_presets.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[配置向导与预设创建](guide.md)、[参数面板与对象覆盖](gui-parameters.md)、[打印机、耗材与工艺资源](resource-profiles.md)、[应用入口与 CLI](cli.md)。
