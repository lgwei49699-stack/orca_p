# 普通、树状与有机支撑

<!-- module-review
{
  "id": "support",
  "source_fingerprint": "sha256:4f27d18f29642be9a4d20307cfe2a347a46eb7d79c7b89b31f6cc4f2d13db5ad",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
}
-->

## 职责与边界

本模块负责普通、树状与有机支撑。归属以[模块清单](../modules.json)的 `support` 为准。

## 入口与调用

- [SupportMaterial.cpp](../../../src/libslic3r/Support/SupportMaterial.cpp)：`PrintObjectSupportMaterial::generate` 组织普通支撑的接触层、中间层和筏层生成。
- [TreeSupport.cpp](../../../src/libslic3r/Support/TreeSupport.cpp)：`TreeSupport::generate` 组织悬垂检测和树生成，`generate_toolpaths` 转为挤出路径。
- [TreeSupport3D.cpp](../../../src/libslic3r/Support/TreeSupport3D.cpp)：有机树生成的初始承托区域、树尖／分支和碰撞处理；与二维树路径分别核对。
- [SupportCommon.cpp](../../../src/libslic3r/Support/SupportCommon.cpp)：`generate_raft_base`／`generate_support_toolpaths` 提供筏层与普通支撑共用的路径生成。

## 核心流程

PrintObject::generate_support_material 根据支撑类型分派普通和树状实现。普通生成接触、界面和中间支撑层，树状／有机路径构建节点、体积及工具路径；SupportCommon 汇合公共层和路径处理。

## 配置与约束

开启支撑、样式、阈值、仅平台、首层扩展、界面层和 XY/Z 间隙分别控制区域与承托。一个 SupportLayer 可包含不同实际厚度的路径，不能把整层厚度套到每根支撑线。

当前分支可检索的配置键：`enable_support`、`support_type`、`support_style`、`support_threshold_angle`、`support_on_build_plate_only`、`support_object_xy_distance`、`support_top_z_distance`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

取消、节点生成失败和空接触区域要按原分支回退／报错；不能删除来源保护来强行启用分块。相同树状输入的独立生成不一定逐点相同，恢复测试应比较同一次生成的原始路径。

## 修改与验证

支撑类型切换、普通 Grid/Snug、树状／有机／混合树都要覆盖。测试应同时核对普通支撑保留、筏层边界和重切缓存，而非只检查分块看得见。

现有测试/工具范围：`tests/fff_print/test_support_material.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[筏层计划、合并与分块](raft.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[填充、顶底面与窄区](fill.md)。
