# 自动分盘与打印板

<!-- module-review
{
  "id": "arrangement",
  "source_fingerprint": "sha256:988d14059277e6e4aaa3be8fd45a08b7b231f8bc0954c81506b674a6fb43ec79",
  "reason": "2026-11-06：复核共享 PrintConfig 差异仅在 CLIMiscConfigDef 新增四项重建准入配置并明确内存为启动前估算；未改本模块工艺参数、默认值或执行路径，无需修改正文。 2026-11-06 合并复核：保留发布分支的现有引擎及验证说明，补入特征分支历史调查和离线审计／下载工具；非测试工具模块源码未随文档归档变化，测试工具指纹按组合源码重新核对。"
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

CLI `--export-model-transforms` 仅在多盘模式为越界或无归属实例各分配一个只存在于 JSON 的返回盘号，XY 为床面包围盒中心、Z为0；多盘归属同时用共享BuildVolume核对真实床面形状，不能只按床的外接矩形；普通盘内实例仍保留实际底面Z（允许正值或负值），详见[CLI模块](cli.md)及[验证记录](../validation/2026-10-17-cli-oversized-arrangement.md)。显式 `--auto-plate 0` 保留实际归属（无归属历史回退盘0）及实际底面中心，不新增返回盘或重写XYZ。两种模式都不创建实际盘、不改变GUI、核心排布、盘归属和保存重开；不能将该返回盘号用于推断真实盘数或可打印性。

检查候选是否越床、碰固定物体／禁放区和真正分到几盘，再提交变换。取消不应移动半批对象。GUI 成功通知和 CLI 返回成功都不能代替几何验收。

失败搜索可能仍写回旋转。CLI 多盘 JSON 的盘外投影使用本次排布前保存的有效实例姿态，并组合已完成的自动定向和全局变换；正常盘内、单盘及不排布时继续用实际姿态。该例外只修正返回协议，不回滚实际实例、不修改 GUI ArrangeJob 或共享搜索规则。见[姿态验证](../validation/2026-10-20-cli-overflow-pose.md)。


`PartPlate::duplicate_all_instance` 返回原对象/新对象的 ObjectID 对；CLI repetitions 用此映射继承全局变换及自动定向的JSON记录。复制本身的几何、数量、位置和打印开关不变；该函数当前只有CLI调用，不改变GUI ArrangeJob。组合复制/定向/缩放的实际3MF与JSON回归见[固定位置修复记录](../validation/2026-10-19-cli-fixed-position-slicing.md)。

## 修改与验证

修改 Arrange、ModelArrange 时同时检查 GUI ArrangeJob；跑本分支 model_arrange 测试，并验证固定项、空盘、100／180 mm、显式与自动间距。盘名需保存重开及 headless 导出，不能调用要求 OpenGL 上下文的绘制。

现有测试/工具范围：`tests/libslic3r/test_model_arrange.cpp`；`tests/libnest2d/**`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支有 mixed_raft_clearance 和 compact_raft_arrangement。兼容的逐层、同有效配方、无支撑独立单实例 Cura 筏层可按模型距离紧凑排列；混摆和不兼容对象继续保守预留。有限空矩形盘补放允许可裁的外扩，但模型本体仍须满足平台/限高。固定项精度补偿与候选验证保留，其他模式不能据此放宽。

## 关联模块

2026-10-17 用户 GUI 分盘后的备份核对发现：两个110mm立方体超高后跳过排布器，均在盘外且实际不相交，但原有 finalize 仍在世界坐标上追加虚拟盘偏移；当前盘模式下的盘外哨兵36还可能参与最大盘号计算，导致对象移到很远处。本轮仅分析、未修复，不改变既定 GUI 盘外规则或 CLI JSON 单独分盘边界。输入、坐标、调用链及建议回归见[启动与分盘排查](../validation/2026-10-17-gui-startup-arrangement.md)。

[筏层计划、合并与分块](raft.md)、[Brim、Skirt 与平台附着](adhesion.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[应用入口与 CLI](cli.md)。

本次 Linux 分支合入盘外 JSON 姿态修复的冲突处理与验证见[合并记录](../validation/2026-10-20-linux-overflow-merge.md)。

2026-10-29日志原尺寸39.83×105×3.6mm模型的盘外原因已用同输入CLI复现：树状支撑默认单侧12mm预留、每侧1mm缩床以及单盘失败位置输出共同造成；关闭支撑/显式间距的单因素对照可排入。补充实际切片确认原配置0悬垂层、0支撑路径：已有动态预检查把底板承托的上层图案底面误判为悬垂，随后套用固定12mm。已定位共享核心误判但未修复，GUI未验，详见[原尺寸模型排查](../validation/2026-10-29-arrangement-service-log.md)。

同一案例按用户请求另行交付布尔并集STL副本：原尺寸不变，原配置100%自动转90度居中、切片无支撑，完整模型检查通过；这解决该输入，尚未修改共享预检查。移动端重新上传步骤见上述排查记录。

同日追加的40.09×93×3.6mm单闭壳STL在旧日志工艺下完整检查、100%自动居中及无支撑切片通过；不能外推用户GUI当前1026_AI工艺。GUI实际启用Cura筏层：向下面粗检触发12mm树支撑估算，加筏层差额3.5mm后每侧15.5mm，并写为不可压缩的minimum_inflation，导致93+31=124mm长边被拒绝。同GUI程序/完整工程复现盘外；仅改传统模式且层数0自动居中成功，保留Cura固定转90度居中实际切片占地105×52.086mm也在床内。确认共享排布占地高估，未修改引擎；GUI绕过步骤及完整程序/配置证据见同一排查记录。
