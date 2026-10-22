# 周长墙与 Arachne 可变线宽

<!-- module-review
{
  "id": "walls",
  "source_fingerprint": "sha256:1b7e240d98bc2eae37757dd7e8667299afedefc7bca386234a43c38a352fe39f",
  "reason": "2026-10-22将本批薄墙恢复与VariableWidth修正隔离为显式CLI运行时opt-in，默认false；关闭恢复旧拼接/分组/末组积分，GUI默认关闭。开启才保留两端完整分组和同段积分，无参数阈值放宽。补旧算法冻结结果、开启恢复及默认值测试，原min_length_factor参数工厂修正仍保留。"
}
-->

## 职责与边界

本模块负责周长墙与 Arachne 可变线宽。归属以[模块清单](../modules.json)的 `walls` 为准。

## 入口与调用

- [PerimeterGenerator.cpp](../../../src/libslic3r/PerimeterGenerator.cpp)：经典墙/Arachne 选择及顶底保护配置。
- [WallToolPaths.cpp](../../../src/libslic3r/Arachne/WallToolPaths.cpp)：make_paths_params 与可变线宽墙生成。
- [WallToolPaths.hpp](../../../src/libslic3r/Arachne/WallToolPaths.hpp)：WallToolPathsParams 数据合同。

## 核心流程

PerimeterGenerator 选择经典墙或 Arachne；WallToolPaths 根据边界及 beading strategy 产生可变线宽路径，再交给周长排序。Arachne 同时被同心填充及窄区填充调用，并非只在 GUI“墙生成器”下生效。

薄墙恢复仅在CLI显式`--optimize-model-detail-paths`时启用，普通CLI与GUI默认关闭，报告开关不会启用它。Print运行时状态经LayerRegion/PerimeterGenerator传至WallToolPathsParams，同心填充通过Fill独立传递；不写入工艺、机料或工程。全为开放、正宽odd墙片段的小岛，若短线过滤后仍有非零路径，却在简化后全部变为重合点，会以原片段进行一次重合端点优先拼接，再执行同样的短线过滤/内轮廓分离/简化。仅总丢失时重试，不改普通拼接、非零路径、短线过滤导致的合法省略、零宽内轮廓或普通墙。优先拼接只用一个缩放单位，不扩大原0.01 mm吸附容差或改变线宽/流量。关闭时不保留重试输入、不进行额外非零路径扫描。不能将恢复一条中心线等同小岛完整覆盖或实印成功。

## 配置与约束

`variable_width`把线性变化的线宽（传入值为中心距）转换为常宽挤出段，新增末尾默认false的内部选择参数。关闭时保留原分组判据、分组上下界更新时机及末组起点积分；开启时每组以段长加权的两端均值计算，末组与中间完成组使用相同积分，避免只取起点宽度造成方向性少料/多料。开启时分组判断纳入旧组及新段a/b两端的完整范围，新组也登记b端。原0.05 mm分组/拆段容差、过滤条件、角色和层高不变，不新增流量倍率或降低最小线宽。两种实现为编译期分支，未开启时不执行新分组算法；调用方必须显式传入运行时选择。

优化开启时，转换本身保留原拐点和中心线路径，只按原容差插入分段点（整数坐标有原有舍入）；分组边界变化会影响后续`LayerRegion::simplify_path`按既有resolution进行的独立简化。因此优化输出可能增加移动段并略改中间点，应同时验证简化前路径、最终几何偏差和正E。经典细墙、墙/填充补缝和可变宽实体填充均可能消费此共享转换。按用户明确要求，GUI不接入该CLI运行时选择，始终默认关闭；不是根据GUI/CLI身份在算法内部使用不同规则。桥接圆截面不在该非桥接转换的假定内，可变宽的物理可打印性仍需材料/设备验证。

最小线宽、最小特征、过渡角／长度／过滤间距控制不同阶段。min_length_factor 是最短墙过滤系数：路径最小线宽 × 系数，不是固定毫米长度；顶／底面存在保护分支。合法 0 是否传递取决于本分支参数工厂。

当前分支可检索的配置键：`wall_generator`、`wall_transition_angle`、`wall_transition_filter_deviation`、`wall_transition_length`、`wall_distribution_count`、`min_bead_width`、`min_feature_size`、`min_length_factor`、`initial_layer_min_bead_width`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

过短线过滤会影响模型挤出数量，不能为匹配历史 G-code 简单关闭过滤。未初始化字段造成的不稳定应修默认值，并用相同配置、构建身份与重复切片对照。

## 修改与验证

改参数工厂和 Fill 的调用同时审阅；比较模型挤出坐标、线宽、挤出量与顺序，单看总时间不够。GUI 单位说明与 CLI 存储值需保持一致。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_arachne_params.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[ResidualIsland]`包含原生25点小岛的退化复现/显式开启恢复、关闭保持旧退化结果、非零旧路径不变、短线过滤与0墙圈不被绕过、共享端点8种排列方向、宽度/总长不变，以及普通墙/零宽/非接触路径的旧算法对照。`[DetailPathOptIn]`补旧分组/末组体积冻结值、默认关闭及Print模式切换缓存失效。GUI调用共享核心的默认关闭路径；CLI显式开启时经典细墙/补缝和Arachne同心填充接入运行时选择。修复、分盘入口没有新增算法。真实模型默认与旧版、开启与候选的G-code对照及用户明确不改GUI的边界见[本批记录](../validation/2026-10-18-slice-analysis.md)。

## 分支现状

本分支给 min_length_factor=0.5、is_top_or_bottom_layer=false 明确默认；make_paths_params 保留存在的合法 0，按首层与普通层选择最小线宽。填充维持既有半线宽过滤策略。min_length_factor 的 GUI 单位已修为倍，存储键和 CLI 值未改；这是历史参数初始化/解释修复，不能反推全部旧新路径差异都来自该处。

## 关联模块

[填充、顶底面与窄区](fill.md)、[FFF 切片步骤与层状态](slicing.md)、[参数面板与对象覆盖](gui-parameters.md)、[二维／三维几何与空间算法](geometry.md)。
