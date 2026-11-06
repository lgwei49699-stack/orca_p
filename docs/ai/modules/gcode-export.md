# G-code 生成与后处理

<!-- module-review
{
  "id": "gcode-export",
  "source_fingerprint": "sha256:7d9f50781c519f2d6e3aa7e0ec24824d741232926f37a60afcca31f805b40db9",
  "reason": "2026-11-06：复核共享 PrintConfig 差异仅在 CLIMiscConfigDef 新增四项重建准入配置并明确内存为启动前估算；未改本模块工艺参数、默认值或执行路径，无需修改正文。"
}
-->

## 职责与边界

本模块负责G-code 生成与后处理。归属以[模块清单](../modules.json)的 `gcode-export` 为准。

## 入口与调用

- [GCode.cpp](../../../src/libslic3r/GCode.cpp)：层路径、换刀、空移与最终输出协调。
- [GCodeWriter.cpp](../../../src/libslic3r/GCodeWriter.cpp)：机器 flavor 对应的指令写入。
- [SeamPlacer.cpp](../../../src/libslic3r/GCode/SeamPlacer.cpp)：`SeamPlacer::init` 准备接缝候选、可见性及层关系，供导出阶段选择轮廓起点。

## 核心流程

GCode 消费 Print 的层和挤出集合，协调工具顺序、接缝、回抽和空移；GCodeWriter 输出机器指令，CoolingBuffer/FanMover/PressureEqualizer 等进一步处理。配置块、缩略图和自定义起止代码随导出写入。

`Print::export_gcode` 末尾的内部 `collect_analysis=false` 仅决定其 `GCode`/`GCodeProcessor` 是否额外收集报告统计；CLI显式报告传true，既有GUI和普通调用使用默认false。构造开关经过导出初始化的reset保持，普通估时/耗材与最终M73等后处理不依赖该统计，不改变打印参数或路径。没有新增用户配置，也不从模型/预设推断启用报告。

显式采集时，`extrude_loop`复用既有路径遍历汇总小轮廓原生选中环数、普通非桥接墙路径数/长度及选择的速度范围；阈值仍按环周长判断，百分比仍相对外墙，普通内墙也使用该档。摘要在串行生成结束和处理器finalize后写入结果，不在并行后处理时共享修改计数。该证据位于首层/流量/冷却覆盖之前，不能把选择值当最终到达速度；不新增G-code标签、配置、扫描或速度重写。普通GUI/无报告CLI关闭采集，固定字段与分支仍有小量开销。

`total_layer_count`、导出总层数与按层进度使用实际将输出的层组数：工具顺序建立后，以 `process_layer` 相同的“extruders 非空”条件计数，在展开启动代码前完成。复用 `collect_layers_to_print` 的真实层组与 Z 合并规则；逐层按全盘合并、逐对象按各对象实例数累加。不能只看模型层 `has_extrusions`，否则会漏掉支撑、筏层或擦料塔独占的层。空组仍留在原输出管线，不通过删层改变几何、冷却或已有空层检查；逐对象缓存层组，避免对每个副本重复收集/校验。

文件头层数仍由 GCodeProcessor 解析实际换层标签后填入；本次修正上游计数，不在后处理里搜索替换用户机器代码。`max_z_height` 保留原有计划最高模型层语义，不把它当作实际最后一个挤出层，也不调整尺寸或安全抬升。

Cura V1 的首层间隙校验保留实际垫层接触面锚点：层号与计划顶面一致、来源标记对应的路径有挤出体积，或接触层已由合并垫层覆盖。即使普通支撑先打印到气隙中，也可允许有实际路径且位于 `raft_contact_top_z + gap_raft_object + first_object_layer_height` 的模型首层通过。该条件不扩大后续层、错位首层、无接触路径或 legacy 的允许间隙，不用额外容差掩盖缺层。

GUI 的处理阶段（`Print::process` 内 psSkirtBrim）与 GUI/CLI 导出均调用同一 `collect_layers_to_print`；两阶段一起覆盖。具体正反例及 GUI 实测见[底部空层修复](../validation/2026-10-27-raft-bottom-empty-layers.md)。

## 配置与约束

机器 flavor、绝对／相对挤出、速度／加速度限制、耗材流量和占位符都是最终指令的输入。保序集合必须保留块内顺序，但块间允许必要空移；不能用“没有空移”作为正确标准。

当前分支可检索的配置键：`gcode_flavor`、`use_relative_e_distances`、`machine_start_gcode`、`machine_end_gcode`、`filament_flow_ratio`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

占位符失败、磁盘写入失败、取消和后处理进程错误需检查实际出口。导出完成不等于真实打印成功。不要将额外路径与预览中的空移线混为一谈。

## 修改与验证

旧多耗材矩阵兼容在 `Print::apply` 的配置差异计算前完成，导出的完整配置与实际擦料塔共用补齐后的矩阵，保留用户倍率；`[FlushMatrixCompatibility]` 验证真实切片/导出及重复apply不失效。正常任务指令保持，结果见[EP7记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。

颜色表缺项不再阻止按已知耗材参数补齐矩阵。CLI回归同时检查实际T指令、显式同色不同耗材配置，并对15个正常场景的非注释G-code逐条比对；原任务显式绑定的96459条指令也保持一致。项目3MF的原配置元数据和实际G-code有效配置分开检查，不能仅以导出OBJ有颜色证明打印路径正确。

比较层/角色/挤出/空移而非单一文件哈希（时间头可能变化）；修改路径顺序要确认模型路径未被无关改变，缩略图生成由格式模块负责。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/fff_print/test_gcodewriter.cpp`；`tests/fff_print/test_printgcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[LayerCount]` 对照文件头/尾、启动/结束/逐层占位符与实际换层标签，覆盖健康层、空尾层、中间空层、部分对象空层、仅支撑层及逐层/逐对象多副本、筏层和真正启用的双材料擦料塔。模型路径、输入配置的真实前后对照与 GUI 待验范围见[层数修正记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-输出层数一致性修正)。GUI 与 CLI 共用 `Print::export_gcode → GCode::_do_export`，不单独修改任一端预设/温度。

## 关联模块

[FFF 切片步骤与层状态](slicing.md)、[筏层计划、合并与分块](raft.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 解析、统计与预览](gcode-preview.md)。
