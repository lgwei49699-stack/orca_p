# 填充、顶底面与窄区

<!-- module-review
{
  "id": "fill",
  "source_fingerprint": "sha256:4808fd0471d6c2890d28ddfd352552a3a25e7c11d48c77fe7496d537fc7e0a19",
  "reason": "本轮只修改支撑调用侧 ZigZag 的坐标系；FillRectilinear 通用实现、模型填充、增强路径的搭墙延伸限制未变。复核新增覆盖面积断言，正文明确两项修复相互独立。"
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

另一个独立的坐标问题由 SupportCommon 调用侧处理：偏心 CuraV1 ZigZag 区域先以垫层包围盒
中心为原点填充，再把路径移回。此转换包含普通未分块、有墙填充及最后接触层；不修改
FillRectilinear 的通用算法或模型填充，也不改变上述增强模式的安全裁剪设置。

密度、间距、角度、线宽、连接及实心／稀疏角色一起决定结果；模型坐标与 pattern 原点要稳定。no_sort 集合表达保序意图，下游不能扁平化后任意重排。

当前分支可检索的配置键：`sparse_infill_pattern`、`sparse_infill_density`、`internal_solid_infill_pattern`、`top_surface_pattern`、`bottom_surface_pattern`、`infill_direction`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

小区域可能生成空填充，需分清合法省略和短线误删。连接线不能穿出允许区域，桥接和实体填充不可只因显示颜色相近就合并。取消由处理阶段与长循环传入。

## 修改与验证

修改 pattern 应覆盖实心、稀疏、带孔、窄区、旋转、多层方向和短线。与墙共同验证两种同心路径及首层／其他层最小线宽。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_extrusion_entity.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

垫层调用侧坐标修复、真实任务 CLI 与 GUI 待验步骤见[偏心垫层修复](../validation/2026-10-25-cura-raft-offset-fill.md)。

`test_support_material.cpp` 的 `[EndpointOverlap]` 使用捕获的阶梯轮廓验证四个方向、两种线宽/密度、
原模式能复现裁断、限制后裁剪前后逐点不变、流量属性保持及恢复原模式输出。
GUI/CLI 共享入口与验收见[筏层进度及交接](../../../doc/print_settings/support/raft_geometry_trial_progress.md#2026-10-08-当前调查ep3-plus-复杂轮廓断开与平滑对照)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[G-code 生成与后处理](gcode-export.md)。
