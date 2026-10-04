# 筏层计划、合并与分块

<!-- module-review
{
  "id": "raft",
  "source_fingerprint": "sha256:f39e99bc214f243df5076cbeed59ed0d3f68ab6ff8a2a048b877e58d4b4ba70e",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
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

## 关联模块

[普通、树状与有机支撑](support.md)、[自动分盘与打印板](arrangement.md)、[填充、顶底面与窄区](fill.md)、[Brim、Skirt 与平台附着](adhesion.md)、[FFF 切片步骤与层状态](slicing.md)。
