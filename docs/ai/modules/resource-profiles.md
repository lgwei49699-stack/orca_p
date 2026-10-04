# 打印机、耗材与工艺资源

<!-- module-review
{
  "id": "resource-profiles",
  "source_fingerprint": "sha256:2cdc1a7ca07c3b9402367b73dbf6fce32640e05e5b2d16c696e578de799ad189",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责打印机、耗材与工艺资源。归属以[模块清单](../modules.json)的 `resource-profiles` 为准。

## 入口与调用

- [Custom.json](../../../resources/profiles/Custom.json)：Custom 品牌资源包版本与机器／耗材／工艺清单，驱动加载和升级。
- [gfd_button_config.json](../../../resources/gfd_button_config.json)：按设备类型登记业务按钮能力；页面入口还需检查对应代码及分支限制。
- [EPONE Plus.json](../../../resources/profiles/Custom/machine/EPONE%20Plus.json)：EPONE Plus 机型声明：协议标识 EPONEPlus、床资源、喷嘴与 default_materials。

## 核心流程

品牌清单注册机器、工艺和耗材子文件，子项通过 inherits 复用基础参数；设备封面、床模型和床贴图供向导及主界面引用。profiles_template 是模板输入，不能当已安装用户配置。

## 配置与约束

Custom 包版本、预设内部 version、name、setting_id、renamed_from、compatible_printers 含义不同。EP3 系列当前选择依赖默认耗材和兼容范围，不由旧名称迁移别名决定。

## 失败与取消

清单缺注册／继承缺父项／资源文件缺失会表现为加载或显示异常；不要为修一张封面同步整个分支。贴图标注与实际行程必须双向核对，但不替代实机行程验收。

## 修改与验证

改资源用 profile validator 和本分支 epone_presets 测试；差异范围要列出，用户预设覆盖保持。资源升级需要隔离配置目录，不能拿清空后的新安装替代旧配置升级。

现有测试/工具范围：`tests/libslic3r/test_epone_presets.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

当前 Custom 资源版本 02.03.02.79；三个 EP3 耗材移除了误复制的 My Generic PLA @MyToolChanger 别名，真实 Generic PLA 保留映射，EP3 名称、ID 与参数不变。EP3/EPONE 系列封面及 EPONE Plus 已同步指定范围，没有整分支同步其他品牌行为。

## 关联模块

[配置定义、预设继承与资源更新](presets.md)、[配置向导与预设创建](guide.md)、[设备、AMS 与打印任务监控](devices.md)、[图标、着色器及静态辅助资源](resource-ui.md)。
