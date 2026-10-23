# G-code 生成与后处理

<!-- module-review
{
  "id": "gcode-export",
  "source_fingerprint": "sha256:576162eacc7548ac6ffc3cded5594cd602102d93b8f5294828589094bf932675",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助，只在模型检测组合解释；GCode/GCodeWriter、指令风格、速度流量、后处理及GUI导出路径未改，证据流程不输出G-code。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-19 复核GCode构造的collect_analysis仅传解析器，不修改写入器、速度/挤出/冷却/后处理。正文补默认false的内部导出边界，CLI40组指令/配置/统计严格等价，原GCode七项回归通过；GUI待用户兼容验证。 2026-10-22仅共享Print运行时模式选择上游模型路径；GCodeWriter/冷却/速度/导出后处理无差异。默认恢复原路径，显式开启才消费修正后分段与E，报告保持独立只读；真实模型指令/参数对照见实施记录。 2026-10-22复核新增CLI层高边界只改变显式计划的输入层；原G-code生成/后处理未改，新增验收在导出前。报告不改有效参数/执行指令的回归另记录。 2026-10-23仅collect_analysis显式开启时在既有循环汇总小轮廓选择，finalize后发布；原速度决策、路径、指令、GUI默认未改。正文区分选择值与最终输出，正/负命中及报告开关回归见质量策略记录。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。"
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

## 配置与约束

机器 flavor、绝对／相对挤出、速度／加速度限制、耗材流量和占位符都是最终指令的输入。保序集合必须保留块内顺序，但块间允许必要空移；不能用“没有空移”作为正确标准。

当前分支可检索的配置键：`gcode_flavor`、`use_relative_e_distances`、`machine_start_gcode`、`machine_end_gcode`、`filament_flow_ratio`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

占位符失败、磁盘写入失败、取消和后处理进程错误需检查实际出口。导出完成不等于真实打印成功。不要将额外路径与预览中的空移线混为一谈。

## 修改与验证

比较层/角色/挤出/空移而非单一文件哈希（时间头可能变化）；修改路径顺序要确认模型路径未被无关改变，缩略图生成由格式模块负责。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/fff_print/test_gcodewriter.cpp`；`tests/fff_print/test_printgcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[LayerCount]` 对照文件头/尾、启动/结束/逐层占位符与实际换层标签，覆盖健康层、空尾层、中间空层、部分对象空层、仅支撑层及逐层/逐对象多副本、筏层和真正启用的双材料擦料塔。模型路径、输入配置的真实前后对照与 GUI 待验范围见[层数修正记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-输出层数一致性修正)。GUI 与 CLI 共用 `Print::export_gcode → GCode::_do_export`，不单独修改任一端预设/温度。

## 关联模块

[FFF 切片步骤与层状态](slicing.md)、[筏层计划、合并与分块](raft.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 解析、统计与预览](gcode-preview.md)。
