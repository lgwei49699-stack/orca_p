# 多材料、颜色分配与擦料塔

<!-- module-review
{
  "id": "multimaterial",
  "source_fingerprint": "sha256:3d18c010d63fd3b837c10804eb94924f9ca74e2d224ae9d725f955ba361a5340",
  "reason": "本轮共享换料/矩阵实现未再改；CLI对不同普通耗材保留原槽位，新增4组回归核对对象/面及实际G-code，25项通过。补充自动合并仅适用于同默认配置的范围。"
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

MTL 读色后续修正位于 formats 共享层：明确 Kd 优先，缺失才回退 Ka。原 EP7 的黑色恢复后默认槽加四种材质为5槽/25项冲刷矩阵；这是恢复真实材质后的结果，不是额外耗材业务规则。显式映射仍为4槽，换料指令保持；未改冲刷算法。

## 配置与约束

外部颜色、内部挤出机索引和设备槽位不是同一编号。flush 矩阵、单喷嘴多材料、塔位置和各盘耗材集合共同决定占地与输出；同色模型不能直接代表无需塔。

当前分支可检索的配置键：`enable_prime_tower`、`prime_tower_width`、`flush_volumes_matrix`、`flush_multiplier`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

缺材质／映射越界应在导入或配置校验阶段暴露；预览可打开不等于所有换刀指令可执行。取消不得保留部分换刀计划，塔冲突需按实际包络验证。

## 修改与验证

旧工程矩阵不完整时，共享 `Print::apply` 先自动兼容，成功仅记录info日志，不触发切片错误：维度同时考虑颜色表及 `filament_diameter` 参数槽数，不能因颜色表缺项而漏掉已有耗材。保留可确定行列的旧配对值（含0），按 `flush_volumes_vector` 补新增配对，沿用GUI第一槽/140回退。非方阵重建，足够大的完整方阵和倍率不改。不会为了补矩阵虚构耗材/颜色或将工具改成第一个槽；无法恢复的引用仍保留后续校验。

`ToolOrdering` 的换料排序求解入口在任何矩阵索引前检查参与工具及起始工具，空集合直接返回；保证早于擦料塔生成的排序也不会越界。正常求解算法、换料顺序和有效配对冲刷量保持。颜色表缺项的两份历史3MF现在按既有6份耗材补齐运行矩阵，保留原T1/T2/T3；不会从已丢失的历史颜色表猜测或恢复黑色。

普通多 OBJ 的旧自动配色仅在 CLI 确认耗材列表全部复用同一非空配置路径时累积统一槽表，并重映射后续模型涂色和对象/体积挤出机；默认槽单独保留。普通不同配置、空列表或空槽保持旧编号，显式材质绑定策略也不变；不能按 MTL 颜色将模型改绑到另一份耗材，默认耗材标志不放开该条件。共享 MTL 读色另按下文修正。`Print::_make_wipe_tower` 在需要擦料塔时校验冲刷矩阵为 N×N 且所有实际工具均有对应索引；`WipeTower2::prime` 再检查耗材参数、矩阵行和工具范围，空工具直接返回，单工具不再读取前一个工具。异常为 `SlicingError`，GUI 与 CLI 共用，正常冲刷量计算和换料顺序保持。详见[EP7 多 OBJ 修复记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。

改换色策略要核对 ToolOrdering、塔生成、G-code T 指令及分盘预留。验证单色、多色、多盘、冲刷量和颜色保留；本轮不做设备下发或实机。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/libslic3r/test_texture_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

EP7 多 OBJ 旧自动配色覆盖全局颜色表、导致擦料塔矩阵越界的分析与本地对照见 [2026-11-02 分析记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。记录包含限定范围修复、原任务回放和正常路径对照；GUI/线上 Linux 状态单列。

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 生成与后处理](gcode-export.md)、[自动分盘与打印板](arrangement.md)、[设备、AMS 与打印任务监控](devices.md)。
