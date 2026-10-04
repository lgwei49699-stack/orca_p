# 自动分盘与打印板

<!-- module-review
{
  "id": "arrangement",
  "source_fingerprint": "sha256:3c2301b631301420911ce1e47228472f44bff1532d7acaebbcfc213de7d32de6",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
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

## 配置与约束

有效间距由用户设置、打印顺序、对象占地和平台限制共同决定；brim／筏层外扩不是模型尺寸。颜色分组、锁定对象、禁旋转、擦料塔与逐对象打印均限制候选。不能把一种补放策略推广到所有模式。

当前分支可检索的配置键：`print_sequence`、`brim_type`、`brim_width`、`brim_object_gap`、`raft_mode`、`bed_exclude_area`、`printable_area`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

检查候选是否越床、碰固定物体／禁放区和真正分到几盘，再提交变换。取消不应移动半批对象。GUI 成功通知和 CLI 返回成功都不能代替几何验收。

## 修改与验证

修改 Arrange、ModelArrange 时同时检查 GUI ArrangeJob；跑本分支 model_arrange 测试，并验证固定项、空盘、100／180 mm、显式与自动间距。盘名需保存重开及 headless 导出，不能调用要求 OpenGL 上下文的绘制。

现有测试/工具范围：`tests/libslic3r/test_model_arrange.cpp`；`tests/libnest2d/**`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支有 mixed_raft_clearance 和 compact_raft_arrangement。兼容的逐层、同有效配方、无支撑独立单实例 Cura 筏层可按模型距离紧凑排列；混摆和不兼容对象继续保守预留。有限空矩形盘补放允许可裁的外扩，但模型本体仍须满足平台/限高。固定项精度补偿与候选验证保留，其他模式不能据此放宽。

## 关联模块

[筏层计划、合并与分块](raft.md)、[Brim、Skirt 与平台附着](adhesion.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[应用入口与 CLI](cli.md)。
