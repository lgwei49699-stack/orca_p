# 自动分盘与打印板

<!-- module-review
{
  "id": "arrangement",
  "source_fingerprint": "sha256:83c1be113e2089ff092488426c75c42de25208ca9a76b052fba9d0ba4e76ea69",
  "reason": "本次仅因共享 PrintConfig.cpp 新增 CLI misc 的 model_stage_report/reference 更新复核指纹；未修改 Arrange、盘归属、坐标输出或 GUI 分盘行为，不新增本模块业务验证结论。"
}
-->

## 职责与边界

本模块负责自动分盘与打印板。归属以[模块清单](../modules.json)的 `arrangement` 为准。

## 入口与调用

- [Arrange.cpp](../../../src/libslic3r/Arrange.cpp)：arrangement::arrange；多边形与固定障碍排列。
- [ModelArrange.cpp](../../../src/libslic3r/ModelArrange.cpp)：实例/配置到 ArrangePolygon 的公共转换及 CLI 接入。
- [ArrangeJob.cpp](../../../src/slic3r/GUI/Jobs/ArrangeJob.cpp)：process 准备与调用底层摆盘，finalize 应用候选。
- [PartPlate.cpp](../../../src/slic3r/GUI/PartPlate.cpp)：盘归属、盘原点和名称状态。

## 核心流程

GUI ArrangeJob 在主线程准备实例与锁定项，在工作线程调用 arrangement::arrange，finalize 再应用结果。CLI 经 ModelArrange 共用底层。PartPlate/PartPlateList 管理盘原点、排除区域、对象归属及名称，摆盘坐标与盘局部切片坐标要转换。

`store_to_3mf_structure` 为每盘记录临时 `has_printable_instances` 快照，供 GUI/CLI 切片包导出共同判断目标盘；不能把对象成员非空等同于参与打印。快照不序列化、不改变实例打印开关或排布。CLI 全部切片跳过空盘和全部实例明确不打印的盘；显式指定该盘或启用但越界的模型仍走切片校验。

## 配置与约束

有效间距由用户设置、打印顺序、对象占地和平台限制共同决定；brim／筏层外扩不是模型尺寸。颜色分组、锁定对象、禁旋转、擦料塔与逐对象打印均限制候选。不能把一种补放策略推广到所有模式。

当前分支可检索的配置键：`print_sequence`、`brim_type`、`brim_width`、`brim_object_gap`、`raft_mode`、`bed_exclude_area`、`printable_area`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

CLI `--export-model-transforms` 为越界或无归属实例各分配一个只存在于 JSON 的返回盘号，XY 为床面包围盒中心、Z为0；普通盘内实例仍保留实际底面Z（允许正值或负值），详见[CLI模块](cli.md)及[验证记录](../validation/2026-10-17-cli-oversized-arrangement.md)。这不创建实际盘、不改变GUI、核心排布、盘归属和保存重开；不能将该返回盘号用于推断真实盘数或可打印性。

检查候选是否越床、碰固定物体／禁放区和真正分到几盘，再提交变换。取消不应移动半批对象。GUI 成功通知和 CLI 返回成功都不能代替几何验收。

## 修改与验证

修改 Arrange、ModelArrange 时同时检查 GUI ArrangeJob；跑本分支 model_arrange 测试，并验证固定项、空盘、100／180 mm、显式与自动间距。盘名需保存重开及 headless 导出，不能调用要求 OpenGL 上下文的绘制。

现有测试/工具范围：`tests/libslic3r/test_model_arrange.cpp`；`tests/libnest2d/**`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支有 mixed_raft_clearance 和 compact_raft_arrangement。兼容的逐层、同有效配方、无支撑独立单实例 Cura 筏层可按模型距离紧凑排列；混摆和不兼容对象继续保守预留。有限空矩形盘补放允许可裁的外扩，但模型本体仍须满足平台/限高。固定项精度补偿与候选验证保留，其他模式不能据此放宽。

## 关联模块

[筏层计划、合并与分块](raft.md)、[Brim、Skirt 与平台附着](adhesion.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[应用入口与 CLI](cli.md)。
