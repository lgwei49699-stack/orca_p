# SLA 切片与支撑

<!-- module-review
{
  "id": "sla",
  "source_fingerprint": "sha256:f09951f500e305778ddb8db378b2fe879bb6a7bef740de526a50b245d020054a",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助仅供独立模型建议；SLAPrint中空/排孔/支撑/光栅化和曝光参数不消费此键，GUI SLA工艺行为未改。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-22共享PrintConfig仅CLIMisc新增FFF模型路径选择，setter绑定Print而非SLAPrint；SLA算法、曝光、支撑和预设未改，正文无需变更，不宣称SLA运行验证。 2026-10-22复核新增层高边界是CLI FFF自适应封装，未改SLA配置或算法；非FFF入口沿用已有拒绝，不声称SLA运行验证。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-30：复核本次共享PrintConfig.cpp差异仅在CLI配置区新增model_inspection_mode（默认full），由独立check-model入口读取；本模块使用的打印配置、默认值及执行路径均未改，不新增计算，正文无需改动。 2026-10-30末轮：PrintConfig 仅修正已有 CLI basic 检查范围帮助文字（去除七类全检误述），键/类型/默认值及本模块读取规则未改，已核对逐行差异；同步中英文。ModelCheckWorkflow 头仅更正范围注释，详细说明及测试见模型修复记录。 2026-10-30本模块仅因watch的PrintConfig.cpp中App检查帮助改为原生未连接边而更新指纹；无配置键/默认值/有效值或本模块算法变化，正文无需变更。"
}
-->

## 职责与边界

本模块负责SLA 切片与支撑。归属以[模块清单](../modules.json)的 `sla` 为准。

## 入口与调用

- [SLAPrint.cpp](../../../src/libslic3r/SLAPrint.cpp)：`SLAPrint` 维护 SLA 对象、变换及任务步骤；支撑和底座配置分别解析。
- [SLAPrintSteps.hpp](../../../src/libslic3r/SLAPrintSteps.hpp)：中空→排孔→切层→支撑/底座→光栅化步骤接口。
- [SupportTree.cpp](../../../src/libslic3r/SLA/SupportTree.cpp)：`SupportTree::create` 构建树，`retrieve_full_mesh`／`slice` 输出支撑网格或层轮廓。

## 核心流程

SLAPrint/Steps 组织中空、排液孔、模型切层、支撑点、支撑树、底座、支撑切层以及光栅化。SLA/ 子目录实现树、底座和栅格，Format/SL1 处理对应容器；它与 FFF 的熔融筏层计划不是同一流程。

## 配置与约束

SLA 物体层与整盘合并／光栅化步骤分别失效，材料曝光与修正参数不应映射成 FFF 流量或速度。技术类型由配置决定，切换机型需清理对应状态。

## 失败与取消

Steps 使用 Print 取消状态，不能在失败后提交半套切片层。中空和树生成的几何限制需专门验证，FFF 支撑通过不能证明 SLA 通过。

## 修改与验证

现有 tests/sla_print 源码在 tests/CMakeLists.txt 的 add_subdirectory 被注释；存在测试文件不等于默认构建或已执行。按[验证规范](../validation.md)确认可用目标、配置及必要构建；无法运行时记录实际限制。

现有测试/工具范围：`tests/sla_print/**`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[二维／三维几何与空间算法](geometry.md)、[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[后台切片、任务和取消](jobs.md)。
