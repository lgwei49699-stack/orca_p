# 多材料、颜色分配与擦料塔

<!-- module-review
{
  "id": "multimaterial",
  "source_fingerprint": "sha256:c0d2a970d2b073b5cedb529314ab15b43beb22bbdc4fe12e7d50ba2aa1577def",
  "reason": "本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助，不进入分色、工具排序或擦料塔生成；多材料映射、冲刷参数及GUI颜色配置保持，不因建议策略丢弃绘制。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-22新CLI开关不是打印/材料配置，无分色、材料映射、冲刷参数及擦料塔算法修改。Print模式切换使对象及整盘步骤失效，防止擦料塔沿用旧输出；默认模式不增加失效，不宣称多材料实印通过。 2026-10-22复核min/max只为CLI临时计划，不增加耗材/工具映射参数；仍受有效机型边界约束，未修改冲刷/擦料塔算法，未声称多材料实印通过。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-30：复核本次共享PrintConfig.cpp差异仅在CLI配置区新增model_inspection_mode（默认full），由独立check-model入口读取；本模块使用的打印配置、默认值及执行路径均未改，不新增计算，正文无需改动。 2026-10-30末轮：PrintConfig 仅修正已有 CLI basic 检查范围帮助文字（去除七类全检误述），键/类型/默认值及本模块读取规则未改，已核对逐行差异；同步中英文。ModelCheckWorkflow 头仅更正范围注释，详细说明及测试见模型修复记录。 2026-10-30本模块仅因watch的PrintConfig.cpp中App检查帮助改为原生未连接边而更新指纹；无配置键/默认值/有效值或本模块算法变化，正文无需变更。"
}
-->

## 职责与边界

本模块负责多材料、颜色分配与擦料塔。归属以[模块清单](../modules.json)的 `multimaterial` 为准。

## 入口与调用

- [MultiMaterialSegmentation.cpp](../../../src/libslic3r/MultiMaterialSegmentation.cpp)：把涂色三角面与切片轮廓关联，分离各材料区域并处理顶底面分区。
- [FlushVolCalc.cpp](../../../src/libslic3r/FlushVolCalc.cpp)：`FlushVolCalculator::calc_flush_vol` 根据源／目标颜色计算冲刷体积。
- [ToolOrdering.cpp](../../../src/libslic3r/GCode/ToolOrdering.cpp)：`LayerTools` 确定各路径的挤出机，按层组织换料顺序和擦料需求。
- [WipeTower.cpp](../../../src/libslic3r/GCode/WipeTower.cpp)：`WipeTower::prime`／`tool_change` 生成擦料塔及换料路径，供 G-code 导出插入。

## 核心流程

模型绘制和材质映射进入 MultiMaterialSegmentation 形成区域，ToolOrdering 按层决定换刀次序；WipeTower/FlushVolCalc 根据实际换色与配置组织擦料及冲刷。GUI 材料映射和打印机多耗材能力在设备模块。

## 配置与约束

外部颜色、内部挤出机索引和设备槽位不是同一编号。flush 矩阵、单喷嘴多材料、塔位置和各盘耗材集合共同决定占地与输出；同色模型不能直接代表无需塔。

当前分支可检索的配置键：`enable_prime_tower`、`prime_tower_width`、`flush_volumes_matrix`、`flush_multiplier`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

缺材质／映射越界应在导入或配置校验阶段暴露；预览可打开不等于所有换刀指令可执行。取消不得保留部分换刀计划，塔冲突需按实际包络验证。

## 修改与验证

改换色策略要核对 ToolOrdering、塔生成、G-code T 指令及分盘预留。验证单色、多色、多盘、冲刷量和颜色保留；本轮不做设备下发或实机。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/libslic3r/test_texture_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 生成与后处理](gcode-export.md)、[自动分盘与打印板](arrangement.md)、[设备、AMS 与打印任务监控](devices.md)。
