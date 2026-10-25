# Brim、Skirt 与平台附着

<!-- module-review
{
  "id": "adhesion",
  "source_fingerprint": "sha256:8d06b1a2734d63d2ce0161374e116bf669e7a89fcd16bec5437050eb321234df",
  "reason": "指纹变化来自共享 test_support_material.cpp 新增 OffsetFill 双薄片回归；Brim/Skirt 源码及配置未改，原床面裁剪断言复用。复核无需调整附着业务正文，相关既有用例纳入筏层专项。"
}
-->

## 职责与边界

本模块负责Brim、Skirt 与平台附着。归属以[模块清单](../modules.json)的 `adhesion` 为准。

## 入口与调用

- [Brim.cpp](../../../src/libslic3r/Brim.cpp)：`make_brim` 汇集首层模型／支撑区域并生成 Brim 挤出路径；边界处理按本分支实现核对。
- [Brim.hpp](../../../src/libslic3r/Brim.hpp)：声明 `make_brim` 的取消回调、对象路径与包围盒输出契约。
- [GLGizmoBrimEars.cpp](../../../src/slic3r/GUI/Gizmos/GLGizmoBrimEars.cpp)：`GLGizmoBrimEars` 在模型表面拾取并维护手绘耳朵位置，供切片读取。

## 核心流程

Brim 从模型或支撑首层外轮廓构造外侧／内侧附着路径，Skirt 在 Print/GCode 的相关阶段生成；绘制耳朵由 gizmo 保存专用点信息。附着料线与模型本体是不同角色，也会影响分盘占地。

## 配置与约束

固定 brim_width、自动 Brim、模型间隙及实际线宽不同；自动宽度为 0 的配置不一定等于禁用自动 Brim。平台边界需计入半线宽，而不是只判路径中心线。

当前分支可检索的配置键：`brim_type`、`brim_width`、`brim_object_gap`、`skirt_loops`、`skirt_distance`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

裁剪和简化可丢失闭合外圈，边界容差不能只凭预览调整。取消须由调用者传入长循环。贴床补放、普通生成和最终边界审计是三处不同合同。

## 修改与验证

改 Brim 需测试内/外侧、自动、禁放区、旋转、偏移盘及半线宽包络；与 arrangement 的预留范围同时审阅。

现有测试/工具范围：`tests/fff_print/test_skirt_brim.cpp`；`tests/fff_print/test_support_material.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支 Brim 生成按 Print 配置的平台/禁放区进行半线宽安全裁剪，最终连接后再检查；与分盘的自动 Brim 补放配套。原首层外圈缺失的历史失败与修复记录在筏层专题，不能当当前未测版本已通过。

## 关联模块

[自动分盘与打印板](arrangement.md)、[筏层计划、合并与分块](raft.md)、[G-code 生成与后处理](gcode-export.md)、[FFF 切片步骤与层状态](slicing.md)。
