# Brim、Skirt 与平台附着

<!-- module-review
{
  "id": "adhesion",
  "source_fingerprint": "sha256:52f4f70d3bdfb7bc2b401f8a1c3658267f61d0bbe36a8a17327e74ec08476b2b",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助只控制建议，未接入make_brim或Skirt；附着类型、宽度、间隙、绘制耳朵和GUI附着设置保持。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-19 共享Print.cpp只新增导出阶段内部统计开关，Brim/Skirt生成、配置和裁剪未变；正文无需调整，不以此声明附着质量或GUI已验证。 2026-10-22CLI新增模型路径模式，Brim/Skirt配置与实现未改，普通调用默认false。显式模式切换按Print原失效机制重建附着，避免使用旧模型路径缓存；本轮不新增自动Brim选择或附着质量结论。 指纹变化来自共享 test_support_material.cpp 新增 OffsetFill 双薄片回归；Brim/Skirt 源码及配置未改，原床面裁剪断言复用。复核无需调整附着业务正文，相关既有用例纳入筏层专项。 2026-10-26合入master至Linux发布分支：两侧业务说明与发布配置均保留；源码无冲突，合并验证单独记录。 2026-10-26目标分支复核：合入master的云工艺快照优先、MSVC时钟名称与CuraV1偏心填充修复；保留已存细节opt-in和Linux专用构建。模块正文已合并对应说明，共享配置/测试引起的身份更新不代表另改本模块默认策略。运行与GUI边界见2026-10-26发布合并记录。 本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助只控制建议，未接入make_brim或Skirt；附着类型、宽度、间隙、绘制耳朵和GUI附着设置保持。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-19 共享Print.cpp只新增导出阶段内部统计开关，Brim/Skirt生成、配置和裁剪未变；正文无需调整，不以此声明附着质量或GUI已验证。 2026-10-22CLI新增模型路径模式，Brim/Skirt配置与实现未改，普通调用默认false。显式模式切换按Print原失效机制重建附着，避免使用旧模型路径缓存；本轮不新增自动Brim选择或附着质量结论。 2026-10-22复核共享配置只新增CLI层高上下界，未改Brim/Skirt选择、宽度或生成规则，普通默认输入不变。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-26合入f9bcf0afe：保留master修复、Linux打包与特征CLI边界，当前组合验证另记。 2026-10-26发布分支最终复核：合并共享CLI配置注册和G-code只读统计；Brim/Skirt默认值、生成与裁剪未改。 构建/回归及GUI待验见发布合并记录。"
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
