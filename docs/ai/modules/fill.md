# 填充、顶底面与窄区

<!-- module-review
{
  "id": "fill",
  "source_fingerprint": "sha256:c648ec3e7984de70c02e399e7075e9e74c439f9559f5a2a0ceab27c889080d0c",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助仅供模型建议，未传入Fill；密度、图案、搭墙延伸、顶底填充和GUI工艺控件均未改变，证据流程不生成填充。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。"
}
-->

## 职责与边界

本模块负责填充、顶底面与窄区。归属以[模块清单](../modules.json)的 `fill` 为准。

## 入口与调用

- [Fill.cpp](../../../src/libslic3r/Fill/Fill.cpp)：填充表面、pattern 分派及挤出集合组织。
- [FillConcentric.cpp](../../../src/libslic3r/Fill/FillConcentric.cpp)：同心填充的普通与可变线宽入口。

## 核心流程

LayerRegion 的表面分类和桥接结果进入 Fill；不同 pattern 生成多段线或可变线宽路径，再转换为挤出集合。同心填充和窄区实心填充会走 Arachne，填充不是独立于墙参数的完全封闭系统。

## 配置与约束

FillRectilinear 默认在折返内轮廓与外侧扫描轮廓之间保留搭墙延伸。内部属性 `max_endpoint_overlap`
以毫米限制该延伸，负值保留原表达式和默认行为；不是新用户配置项。仅 RaftMerge 的增强筏层
无墙 ZigZag 设置为 0.001 mm，使凹口附近扫描线也在安全中心线域内，避免生成后裁断。
模型填充、普通支撑、有墙筏层及关闭增强的旧路径不启用此限制。

密度、间距、角度、线宽、连接及实心／稀疏角色一起决定结果；模型坐标与 pattern 原点要稳定。no_sort 集合表达保序意图，下游不能扁平化后任意重排。

当前分支可检索的配置键：`sparse_infill_pattern`、`sparse_infill_density`、`internal_solid_infill_pattern`、`top_surface_pattern`、`bottom_surface_pattern`、`infill_direction`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

小区域可能生成空填充，需分清合法省略和短线误删。连接线不能穿出允许区域，桥接和实体填充不可只因显示颜色相近就合并。取消由处理阶段与长循环传入。

## 修改与验证

修改 pattern 应覆盖实心、稀疏、带孔、窄区、旋转、多层方向和短线。与墙共同验证两种同心路径及首层／其他层最小线宽。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_extrusion_entity.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`test_support_material.cpp` 的 `[EndpointOverlap]` 使用捕获的阶梯轮廓验证四个方向、两种线宽/密度、
原模式能复现裁断、限制后裁剪前后逐点不变、流量属性保持及恢复原模式输出。
GUI/CLI 共享入口与验收见[筏层进度及交接](../../../doc/print_settings/support/raft_geometry_trial_progress.md#2026-10-08-当前调查ep3-plus-复杂轮廓断开与平滑对照)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[G-code 生成与后处理](gcode-export.md)。
