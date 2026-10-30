# 填充、顶底面与窄区

<!-- module-review
{
  "id": "fill",
  "source_fingerprint": "sha256:4bec33f2e3f93cc0efadd61362a95d948d792233ba14d2202a33589dd745840f",
  "reason": "2026-10-22Layer模型填充传递Print运行时选择，FillBase/Rectilinear/ConcentricInternal及两种Arachne同心入口显式消费。默认false恢复旧variable_width，未传入的支撑/筏层Fill也为false；不改密度/目标/过滤。关闭基线及开启候选同模型核查见实施记录。 本轮只修改支撑调用侧 ZigZag 的坐标系；FillRectilinear 通用实现、模型填充、增强路径的搭墙延伸限制未变。复核新增覆盖面积断言，正文明确两项修复相互独立。 2026-10-26合入master至Linux发布分支：两侧业务说明与发布配置均保留；源码无冲突，合并验证单独记录。 2026-10-26目标分支复核：合入master的云工艺快照优先、MSVC时钟名称与CuraV1偏心填充修复；保留已存细节opt-in和Linux专用构建。模块正文已合并对应说明，共享配置/测试引起的身份更新不代表另改本模块默认策略。运行与GUI边界见2026-10-26发布合并记录。 2026-10-22Layer模型填充传递Print运行时选择，FillBase/Rectilinear/ConcentricInternal及两种Arachne同心入口显式消费。默认false恢复旧variable_width，未传入的支撑/筏层Fill也为false；不改密度/目标/过滤。关闭基线及开启候选同模型核查见实施记录。 2026-10-22复核共享配置新增项仅CLI层高边界，未改填充/补缝参数或算法；显式变层高使用原填充流程，默认调用不变。 2026-10-23本批复核共享PrintConfig增量仅CLITransform的model_layer_height_plan注册，空列表默认；不改本模块打印参数、默认值、算法或GUI入口，正文无需为该注册变化改写。 2026-10-26合入f9bcf0afe：保留master修复、Linux打包与特征CLI边界，当前组合验证另记。 2026-10-26发布分支最终复核：保留master支撑侧偏心ZigZag修复与已有默认关闭的细节路径模式；新CLI计划通过既有层位消费，不改普通填充默认值。 构建/回归及GUI待验见发布合并记录。 2026-10-30合入App修复：显式basic检查与分部件高级修复，默认full及普通切片/分盘不变；保留本发布分支兼容修复。指纹按合并后的实际源码复核，验证边界见2026-10-30-app-repair-release记录。"
}
-->

## 职责与边界

本模块负责填充、顶底面与窄区。归属以[模块清单](../modules.json)的 `fill` 为准。

## 入口与调用

- [Fill.cpp](../../../src/libslic3r/Fill/Fill.cpp)：填充表面、pattern 分派及挤出集合组织。
- [FillConcentric.cpp](../../../src/libslic3r/Fill/FillConcentric.cpp)：同心填充的普通与可变线宽入口。

## 核心流程

LayerRegion 的表面分类和桥接结果进入 Fill；不同 pattern 生成多段线或可变线宽路径，再转换为挤出集合。同心填充和窄区实心填充会走 Arachne，填充不是独立于墙参数的完全封闭系统。

两种Arachne同心填充共用[薄墙总退化恢复](walls.md#核心流程)，仅CLI显式`--optimize-model-detail-paths`才开启。Layer创建Fill时从Print复制默认false的运行时状态，再传入WallToolPathsParams及variable_width；普通CLI、GUI及其他未传入的Fill调用均保持关闭，不从报告开关或预设推断开启。仅开放正宽片段经简化全部消失时尝试重合端点优先重拼，仍执行原半线宽短线过滤；正常填充路径不重拼。

显式开启优化时，补缝和可变宽实体填充经`variable_width`转换的末组与中间组一样，按每段两端宽度均值乘段长积分，分组上下界包含每段两端和共享接点宽度跳变。关闭时恢复原分组判据、上下界更新时机和末组起点积分，不在正常切片中使用本批修正。未扩展填充目标、改变密度、过滤或最小线宽；优化转换保留原拐点，插入点有整数舍入，后续原resolution简化可因分组变化选择不同中间点。

开启时修正会影响名义覆盖面积与G-code的E；报表面积增减不代表实物优劣，也不能据极细线有正E断言稳定打印。应验证关闭与修正前基线一致、开启与先前优化候选一致，以及两种模式各自不受报告开关影响。既有候选的补缝/内部实体分段、几何偏差及适用模型身份见实施记录；这不是关闭模式的输出承诺。

## 配置与约束

FillRectilinear 默认在折返内轮廓与外侧扫描轮廓之间保留搭墙延伸。内部属性 `max_endpoint_overlap`
以毫米限制该延伸，负值保留原表达式和默认行为；不是新用户配置项。仅 RaftMerge 的增强筏层
无墙 ZigZag 设置为 0.001 mm，使凹口附近扫描线也在安全中心线域内，避免生成后裁断。
模型填充、普通支撑、有墙筏层及关闭增强的旧路径不启用此限制。

另一个独立的坐标问题由 SupportCommon 调用侧处理：偏心 CuraV1 ZigZag 区域先以垫层包围盒
中心为原点填充，再把路径移回。此转换包含普通未分块、有墙填充及最后接触层；不修改
FillRectilinear 的通用算法或模型填充，也不改变上述增强模式的安全裁剪设置。

密度、间距、角度、线宽、连接及实心／稀疏角色一起决定结果；模型坐标与 pattern 原点要稳定。no_sort 集合表达保序意图，下游不能扁平化后任意重排。

当前分支可检索的配置键：`sparse_infill_pattern`、`sparse_infill_density`、`internal_solid_infill_pattern`、`top_surface_pattern`、`bottom_surface_pattern`、`infill_direction`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

小区域可能生成空填充，需分清合法省略和短线误删。连接线不能穿出允许区域，桥接和实体填充不可只因显示颜色相近就合并。取消由处理阶段与长循环传入。

## 修改与验证

修改 pattern 应覆盖实心、稀疏、带孔、窄区、旋转、多层方向和短线。与墙共同验证两种同心路径及首层／其他层最小线宽。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_extrusion_entity.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

垫层调用侧坐标修复、真实任务 CLI 与 GUI 待验步骤见[偏心垫层修复](../validation/2026-10-25-cura-raft-offset-fill.md)。

`test_support_material.cpp` 的 `[EndpointOverlap]` 使用捕获的阶梯轮廓验证四个方向、两种线宽/密度、
原模式能复现裁断、限制后裁剪前后逐点不变、流量属性保持及恢复原模式输出。
GUI/CLI 共享入口与验收见[筏层进度及交接](../../../doc/print_settings/support/raft_geometry_trial_progress.md#2026-10-08-当前调查ep3-plus-复杂轮廓断开与平滑对照)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[FFF 切片步骤与层状态](slicing.md)、[二维／三维几何与空间算法](geometry.md)、[G-code 生成与后处理](gcode-export.md)。
