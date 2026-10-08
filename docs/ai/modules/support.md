# 普通、树状与有机支撑

<!-- module-review
{
  "id": "support",
  "source_fingerprint": "sha256:d02b3f537f6522c24f05684cb48ad5894cba9e856b37456a6bde921ae12cd1a5",
  "reason": "共享测试新增增强筏层阶梯凹口裁断回归；普通支撑、悬空告警及SupportCommon安全裁剪实现未改，新增端点限制只由RaftMerge无墙增强分支设置，正文沿用。筏层/填充专项通过，不宣称全量普通支撑或实机验收。"
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

未启用支撑时的 `TreeSupport::detect_overhangs(true)` 用于告警。仅在周长墙和填充均已完成时，对无下层承托的几何尖尾候选追加实际挤出线宽覆盖检查：perimeters/fills（含 GapFill）完全无交集才排除。小面积本身不豁免；中心线在外但线宽相交仍告警。路径未完成时保留几何判断，支撑生成的 `false` 调用不做这项筛选，不能借此声称网格形状或打印可行性通过。

并行层检测只写本层结果，结束后归并尖尾/悬臂与降级标志。超过 100 悬垂区域或 30 秒仍可跳过该层详细检测，并保守停用后续尖尾延伸；日志明确 region_budget/time_budget，不能将无告警等同完整检测。

## 配置与约束

开启支撑、样式、阈值、仅平台、首层扩展、界面层和 XY/Z 间隙分别控制区域与承托。一个 SupportLayer 可包含不同实际厚度的路径，不能把整层厚度套到每根支撑线。

当前分支可检索的配置键：`enable_support`、`support_type`、`support_style`、`support_threshold_angle`、`support_on_build_plate_only`、`support_object_xy_distance`、`support_top_z_distance`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

取消、节点生成失败和空接触区域要按原分支回退／报错；不能删除来源保护来强行启用分块。相同树状输入的独立生成不一定逐点相同，恢复测试应比较同一次生成的原始路径。

## 修改与验证

支撑类型切换、普通 Grid/Snug、树状／有机／混合树都要覆盖。测试应同时核对普通支撑保留、筏层边界和重切缓存，而非只检查分块看得见。

现有测试/工具范围：`tests/fff_print/test_support_material.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[FloatingWarning]` 覆盖无挤出微区、真实周长墙、GapFill、实际线宽相交；龙模型优化前后同有效配置运动命令一致。全量支撑旧用例的摆放夹具异常与新专项结果分列，见[验证记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-悬空微区与切片包导出修正)，不将失败抹成全量通过。

## 关联模块

[筏层计划、合并与分块](raft.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[填充、顶底面与窄区](fill.md)。
