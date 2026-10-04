# 填充、顶底面与窄区

<!-- module-review
{
  "id": "fill",
  "source_fingerprint": "sha256:3ba5d6ae81838683fdcef61f1c462e7d8767c7630f23001b29889f9d7affc477",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
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

密度、间距、角度、线宽、连接及实心／稀疏角色一起决定结果；模型坐标与 pattern 原点要稳定。no_sort 集合表达保序意图，下游不能扁平化后任意重排。

当前分支可检索的配置键：`sparse_infill_pattern`、`sparse_infill_density`、`internal_solid_infill_pattern`、`top_surface_pattern`、`bottom_surface_pattern`、`infill_direction`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

小区域可能生成空填充，需分清合法省略和短线误删。连接线不能穿出允许区域，桥接和实体填充不可只因显示颜色相近就合并。取消由处理阶段与长循环传入。

## 修改与验证

修改 pattern 应覆盖实心、稀疏、带孔、窄区、旋转、多层方向和短线。与墙共同验证两种同心路径及首层／其他层最小线宽。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_extrusion_entity.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[G-code 生成与后处理](gcode-export.md)。
