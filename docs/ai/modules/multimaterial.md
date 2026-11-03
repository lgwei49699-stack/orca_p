# 多材料、颜色分配与擦料塔

<!-- module-review
{
  "id": "multimaterial",
  "source_fingerprint": "sha256:cb714cefee84b77d12db075ac56ac9bd1e10b1195b5dd0d1014eb7b83f4b1e34",
  "reason": "2026-10-31：纹理转换选择共享高级修复、新面 AABB 原表面取色；实测发现强度0仍改色，按官方阈值0.3/正值门控并合并两重载的平滑实现。正常输入几何+RGB、3mm孔与失败/取消均测；GUI 待验。"
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

纹理导入由 GUI `TextureImportDialog` 和 CLI 共用 `TexturePainting::texture_to_painting` → `TextureToColor`。按 2.5.0 Beta 提供 Ask/跳过高级修复/修复后导入，后者直接调用共享 `repair_mesh`。新面按最近原三角表面取色，替换旧面中心距离。GUI 异步询问并保留本次选择；CLI 显式 `--texture-repair` 配合 `--export-texture-obj`，默认跳过高级修复。半边不兼容的基础三角汤整理仍保留，与官方一致。

转换成功只表示绘色完成，不代表缺陷全部修好。明确请求修复而失败/取消时不发布候选，不静默降级成原几何导入；没有选修复的历史几何导入回退不变。平滑同步官方门控：拓扑区域平滑仅在强度 ≥0.3，几何平滑仅在强度 >1e-6，两个颜色/标签接口共用实现；强度0不再额外改色。新入口、正常模型开关一致和 3 mm 孔壁回归、GUI 中英文清单见[当前实施记录](../validation/2026-10-31-cgal-621-upgrade.md)。

## 配置与约束

外部颜色、内部挤出机索引和设备槽位不是同一编号。flush 矩阵、单喷嘴多材料、塔位置和各盘耗材集合共同决定占地与输出；同色模型不能直接代表无需塔。

当前分支可检索的配置键：`enable_prime_tower`、`prime_tower_width`、`flush_volumes_matrix`、`flush_multiplier`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

缺材质／映射越界应在导入或配置校验阶段暴露；预览可打开不等于所有换刀指令可执行。取消不得保留部分换刀计划，塔冲突需按实际包络验证。

## 修改与验证

CGAL 6.2.1 升级仅将 ColorUtils 的 AABB_traits 改为 AABB_traits_3 并显式包含对应头；颜色分区、自动墙宽和冲刷策略不变。本机构建、绘色及彩色 OBJ 切片回归见[升级记录](../validation/2026-10-31-cgal-621-upgrade.md)，GUI 操作另行验收。

Linux 发布分支在 `ColorUtils.cpp` 与 `TextureToColor.cpp` 显式包含 `boost/next_prior.hpp`，补齐无预编译头构建所需的头依赖，不改纹理转颜色算法。此次合并保留该编译修正；实际构建和多材料 GUI 验收分别记录，见[Linux记录](../validation/2026-10-19-linux-github-packaging-review.md)。

改换色策略要核对 ToolOrdering、塔生成、G-code T 指令及分盘预留。验证单色、多色、多盘、冲刷量和颜色保留；本轮不做设备下发或实机。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/libslic3r/test_texture_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

2026-10-30修复后切片补验发现彩色OBJ自动外墙宽度报负间距；已修好OBJ不执行修复直接切片也可复现。补`test_cli_repair_acceptance.py`的喷嘴默认/对象百分比宽度两项实际G-code回归。明确线宽修正前后执行指令相同，自动宽度与解析后的等效显式浮点值相同；采用隔离合成工艺，GUI操作及真实打印机未验证。构建身份、反例和完整结果见[续接记录](../validation/2026-10-30-thingi-repair-242-comparison.md)。

## 关联模块

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 生成与后处理](gcode-export.md)、[自动分盘与打印板](arrangement.md)、[设备、AMS 与打印任务监控](devices.md)。
