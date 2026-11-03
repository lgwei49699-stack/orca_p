# 多材料、颜色分配与擦料塔

<!-- module-review
{
  "id": "multimaterial",
  "source_fingerprint": "sha256:5d86a0ef8fe63501c296d1ef276cdc34d8c3b0e20092b7720f597402b5843545",
  "reason": "保留发布分支ColorUtils/TextureToColor的boost/next_prior.hpp显式头依赖。合入的模型细节路径开关属于CLI运行时选择，不新增材料/打印配置，不改分色、材料映射、冲刷参数或擦料塔算法；切换Print模式沿用步骤失效机制，默认关闭。正文无需新增多材料行为，本次仅解决审核头冲突并核对合并指纹，未运行多材料实印。 本轮复核共享PrintConfig.cpp只增加默认关闭的CLI repair_evidence选项，未改打印参数定义、预设或本模块消费者逻辑；正文无需调整，未以此宣称该模块全量运行验证。本次追加CLI repair_evidence_close_boundaries及帮助，不进入分色、工具排序或擦料塔生成；多材料映射、冲刷参数及GUI颜色配置保持，不因建议策略丢弃绘制。 10-17 复核本次共享PrintConfig差异仅CLI misc的model_stage_report/reference及帮助；不进入本模块有效打印配置/GUI参数，相关算法、默认值和消费者未改，正文无需调整，不宣称本模块全量运行验证。 10-18 复核共享PrintConfig差异仅在CLIMiscConfigDef新增默认关闭的slice_analysis_report；本模块打印参数、算法、GUI消费者和默认行为均未改，正文无需变动，不宣称本模块全量运行验证。 2026-10-22新CLI开关不是打印/材料配置，无分色、材料映射、冲刷参数及擦料塔算法修改。Print模式切换使对象及整盘步骤失效，防止擦料塔沿用旧输出；默认模式不增加失效，不宣称多材料实印通过。 2026-10-22复核min/max只为CLI临时计划，不增加耗材/工具映射参数；仍受有效机型边界约束，未修改冲刷/擦料塔算法，未声称多材料实印通过。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-26合入f9bcf0afe：保留master修复、Linux打包与特征CLI边界，当前组合验证另记。 2026-10-26发布分支最终复核：共享PrintConfig的CLI计划注册不更改换料/颜色参数；显式计划单STL限制不扩展至普通多材料流程。 构建/回归及GUI待验见发布合并记录。 2026-10-30合入App修复：显式basic检查与分部件高级修复，默认full及普通切片/分盘不变；保留本发布分支兼容修复。指纹按合并后的实际源码复核，验证边界见2026-10-30-app-repair-release记录。 2026-10-30: resolve only zero outer-wall width through existing PrintRegion::flow before color-region spacing. This honors object/nozzle automatic width without changing explicit nonzero handling, configuration defaults, repair or purge policies. Reproduced on a healed OBJ without repair; explicit G-code is unchanged across the fix, automatic/object fallback matches resolved explicit output. Local CLI passed; GUI and physical printing pending. 合入 master 修复记录：本轮共享换料/矩阵实现未再改；CLI对不同普通耗材保留原槽位，新增4组回归核对对象/面及实际G-code，25项通过。补充自动合并仅适用于同默认配置的范围。"
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

分色的外墙宽度统计中，`outer_wall_line_width=0`表示自动，不能直接当零宽度计算间距。此时复用`PrintRegion::flow`按对象`line_width`、实际外墙挤出机喷嘴和层高求有效宽度；全局也为0时使用现有喷嘴默认。明确的非零外墙宽度继续原处理，不修改默认参数、换色顺序或冲刷策略。GUI和CLI共用这段切片核心。

MTL 读色后续修正位于 formats 共享层：明确 Kd 优先，缺失才回退 Ka。原 EP7 的黑色恢复后默认槽加四种材质为5槽/25项冲刷矩阵；这是恢复真实材质后的结果，不是额外耗材业务规则。显式映射仍为4槽，换料指令保持；未改冲刷算法。

## 配置与约束

外部颜色、内部挤出机索引和设备槽位不是同一编号。flush 矩阵、单喷嘴多材料、塔位置和各盘耗材集合共同决定占地与输出；同色模型不能直接代表无需塔。

当前分支可检索的配置键：`enable_prime_tower`、`prime_tower_width`、`flush_volumes_matrix`、`flush_multiplier`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

缺材质／映射越界应在导入或配置校验阶段暴露；预览可打开不等于所有换刀指令可执行。取消不得保留部分换刀计划，塔冲突需按实际包络验证。

## 修改与验证

Linux 发布分支在 `ColorUtils.cpp` 与 `TextureToColor.cpp` 显式包含 `boost/next_prior.hpp`，补齐无预编译头构建所需的头依赖，不改纹理转颜色算法。此次合并保留该编译修正；实际构建和多材料 GUI 验收分别记录，见[Linux记录](../validation/2026-10-19-linux-github-packaging-review.md)。

旧工程矩阵不完整时，共享 `Print::apply` 先自动兼容，成功仅记录info日志，不触发切片错误：维度同时考虑颜色表及 `filament_diameter` 参数槽数，不能因颜色表缺项而漏掉已有耗材。保留可确定行列的旧配对值（含0），按 `flush_volumes_vector` 补新增配对，沿用GUI第一槽/140回退。非方阵重建，足够大的完整方阵和倍率不改。不会为了补矩阵虚构耗材/颜色或将工具改成第一个槽；无法恢复的引用仍保留后续校验。

`ToolOrdering` 的换料排序求解入口在任何矩阵索引前检查参与工具及起始工具，空集合直接返回；保证早于擦料塔生成的排序也不会越界。正常求解算法、换料顺序和有效配对冲刷量保持。颜色表缺项的两份历史3MF现在按既有6份耗材补齐运行矩阵，保留原T1/T2/T3；不会从已丢失的历史颜色表猜测或恢复黑色。

普通多 OBJ 的旧自动配色仅在 CLI 确认耗材列表全部复用同一非空配置路径时累积统一槽表，并重映射后续模型涂色和对象/体积挤出机；默认槽单独保留。普通不同配置、空列表或空槽保持旧编号，显式材质绑定策略也不变；不能按 MTL 颜色将模型改绑到另一份耗材，默认耗材标志不放开该条件。共享 MTL 读色另按下文修正。`Print::_make_wipe_tower` 在需要擦料塔时校验冲刷矩阵为 N×N 且所有实际工具均有对应索引；`WipeTower2::prime` 再检查耗材参数、矩阵行和工具范围，空工具直接返回，单工具不再读取前一个工具。异常为 `SlicingError`，GUI 与 CLI 共用，正常冲刷量计算和换料顺序保持。详见[EP7 多 OBJ 修复记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。

改换色策略要核对 ToolOrdering、塔生成、G-code T 指令及分盘预留。验证单色、多色、多盘、冲刷量和颜色保留；本轮不做设备下发或实机。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/libslic3r/test_texture_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

2026-10-30修复后切片补验发现彩色OBJ自动外墙宽度报负间距；已修好OBJ不执行修复直接切片也可复现。补`test_cli_repair_acceptance.py`的喷嘴默认/对象百分比宽度两项实际G-code回归。明确线宽修正前后执行指令相同，自动宽度与解析后的等效显式浮点值相同；采用隔离合成工艺，GUI操作及真实打印机未验证。构建身份、反例和完整结果见[续接记录](../validation/2026-10-30-thingi-repair-242-comparison.md)。

## 关联模块

EP7 多 OBJ 旧自动配色覆盖全局颜色表、导致擦料塔矩阵越界的分析与本地对照见 [2026-11-02 分析记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。记录包含限定范围修复、原任务回放和正常路径对照；GUI/线上 Linux 状态单列。

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 生成与后处理](gcode-export.md)、[自动分盘与打印板](arrangement.md)、[设备、AMS 与打印任务监控](devices.md)。
