# 筏层计划、合并与分块

<!-- module-review
{
  "id": "raft",
  "source_fingerprint": "sha256:93099887e7c9263952d8dca4ee3f280a76e0b9b1970e13025f9931ee0ca9e4b5",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助，不进入RaftPlan/RaftGeometry/RaftMerge；筏层分块、平滑、气隙、阶段参数和GUI筏层面板均未改变。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-19 本轮共享Print.cpp差异只在export_gcode传递报告统计开关，不改Raft计划、首层参数、合并/分块及气隙；正文无需调整，既有G-code/筏层导出回归和CLI场景结果见本轮记录，不宣称GUI/实印已验。 2026-10-22新增CLI模型路径运行时选择，未改RaftPlan/Geometry/Merge或阶段参数，筏层Fill不设置新选择，默认false。Print模式切换清缓存，不复用跨模式切片数据；普通模式的筏层流程不改，正文无需调整。 2026-10-22复核新增CLI边界不改筏层参数与计划；有界封装保留固定首模型层，默认筏层调用不变。未声称本批覆盖所有筏层实际场景。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-30：复核本次共享PrintConfig.cpp差异仅在CLI配置区新增model_inspection_mode（默认full），由独立check-model入口读取；本模块使用的打印配置、默认值及执行路径均未改，不新增计算，正文无需改动。 2026-10-30末轮：PrintConfig 仅修正已有 CLI basic 检查范围帮助文字（去除七类全检误述），键/类型/默认值及本模块读取规则未改，已核对逐行差异；同步中英文。ModelCheckWorkflow 头仅更正范围注释，详细说明及测试见模型修复记录。 2026-10-30本模块仅因watch的PrintConfig.cpp中App检查帮助改为原生未连接边而更新指纹；无配置键/默认值/有效值或本模块算法变化，正文无需变更。"
}
-->

## 职责与边界

本模块负责筏层计划、合并与分块。归属以[模块清单](../modules.json)的 `raft` 为准。

## 入口与调用

- [RaftPlan.hpp](../../../src/libslic3r/Support/RaftPlan.hpp)：声明独立于 PrintConfig 的 RaftPlanConfig／RaftPhaseConfig 与分阶段层计划，统一单位与解析边界。
- [RaftPlan.cpp](../../../src/libslic3r/Support/RaftPlan.cpp)：三阶段参数解析成物理层计划。
- [RaftAngleResolver.cpp](../../../src/libslic3r/Support/RaftAngleResolver.cpp)：模型首层底面与顶部筏层方向的解析。
- [RaftGeometry.cpp](../../../src/libslic3r/Support/RaftGeometry.cpp)：plan_raft_geometry/prepare_raft_path_regions；轮廓和切缝。
- [RaftMerge.cpp](../../../src/libslic3r/Support/RaftMerge.cpp)：restore_unmerged_rafts/merge_overlapping_rafts；共享筏层事务。

## 核心流程

GUI“筏层”配置与 CLI 加载工艺/对象覆盖后的有效配置均经 `Print::process` 到共享合并核心；
未修改两端参数定义、单位或默认值。结果需对比实际路径，不能用预设名称代替有效配置。

RaftPlan 将基底／中间／顶部三个阶段解析成物理层的 Z、厚度、间距、流量、速度和风扇参数。RaftAngleResolver 根据模型首层底面方向选择顶部角度，没有唯一方向时使用稳定回退值；普通支撑生成端消费计划。

## 配置与约束

传统筏层和 CuraV1 走不同配置解释；总层数、气隙和起始层重叠共同决定模型抬升，不能把模型首层高度直接当筏层基底高度。分块和轮廓平滑是两个独立能力，是否存在必须看本分支说明。

当前分支可检索的配置键：`raft_mode`、`raft_layers`、`raft_segmented`、`raft_chunk_size`、`raft_chunk_gap`、`raft_smoothing`、`raft_base_layers`、`raft_interface_layers`、`raft_surface_layers`、`raft_airgap`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

筏层物理层和普通支撑的来源不能混淆，重切时必须保持原始数据可恢复。平台裁剪只允许外扩区域被裁，不代表模型本体可越界。屏幕上的缝隙还需对照实际挤出路径。

## 修改与验证

验证按阶段检查实际线宽、挤出覆盖、空移／跨缝、边界和普通支撑。模型路径应单独比较，筏层改变导致总时长变化不证明模型改变。实机跨缝可靠性仍由模型师验证。

现有测试/工具范围：`tests/fff_print/test_support_material.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支有 RaftGeometry/RaftMerge。Print::process 在导出准备前恢复/合并兼容筏层；分块默认关闭，尺寸 40 mm、缝宽 0.8 mm，平滑默认 0，非零平滑可独立生效。RaftGeometry 分层 union、轮廓处理、阶段内缩、分块及平台裁剪；路径中心线按实际线宽准备。RaftMerge 按物理 Z 与实际路径厚度分组，保留普通支撑及原所有权，整组候选验证后提交，否则保留原始合并结果。新分离无承托需求的小残片可按明确来源清理；保序路径维持块内连接，块间仍允许空移。不能把所有细条都当可删除残片。

增强无墙 ZigZag 同时约束折返轮廓和扫描外轮廓：转弯内缩至少半线宽 +0.003 mm，
外侧延伸最多 0.001 mm，仍执行半线宽 +0.001 mm 的最终安全裁剪。不能只约束折返而让
扫描线跨过凹口的中心线禁区，再由裁剪打断。分块缝、孔洞和床面边界不放宽；自然凹形
区域仍可生成多段路径，不保证每块必为一条。原短线不强行删除、块间不增加挤出连接。
平滑是独立的几何操作，不为修复路径而静默提高用户数值。验证与待 GUI 步骤见
[10-08 专题记录](../../../doc/print_settings/support/raft_geometry_trial_progress.md#2026-10-08-当前调查ep3-plus-复杂轮廓断开与平滑对照)。

## 关联模块

[普通、树状与有机支撑](support.md)、[自动分盘与打印板](arrangement.md)、[填充、顶底面与窄区](fill.md)、[Brim、Skirt 与平台附着](adhesion.md)、[FFF 切片步骤与层状态](slicing.md)。
