# 周长墙与 Arachne 可变线宽

<!-- module-review
{
  "id": "walls",
  "source_fingerprint": "sha256:139ca856632d3aeed74e1bbd7479c1ad10d5dfa516a23d6e29a9c6e7697f6c76",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
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

## 配置与约束

最小线宽、最小特征、过渡角／长度／过滤间距控制不同阶段。min_length_factor 是最短墙过滤系数：路径最小线宽 × 系数，不是固定毫米长度；顶／底面存在保护分支。合法 0 是否传递取决于本分支参数工厂。

当前分支可检索的配置键：`wall_generator`、`wall_transition_angle`、`wall_transition_filter_deviation`、`wall_transition_length`、`wall_distribution_count`、`min_bead_width`、`min_feature_size`、`min_length_factor`、`initial_layer_min_bead_width`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

过短线过滤会影响模型挤出数量，不能为匹配历史 G-code 简单关闭过滤。未初始化字段造成的不稳定应修默认值，并用相同配置、构建身份与重复切片对照。

## 修改与验证

改参数工厂和 Fill 的调用同时审阅；比较模型挤出坐标、线宽、挤出量与顺序，单看总时间不够。GUI 单位说明与 CLI 存储值需保持一致。

现有测试/工具范围：`tests/fff_print/test_fill.cpp`；`tests/fff_print/test_arachne_params.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支给 min_length_factor=0.5、is_top_or_bottom_layer=false 明确默认；make_paths_params 保留存在的合法 0，按首层与普通层选择最小线宽。填充维持既有半线宽过滤策略。min_length_factor 的 GUI 单位已修为倍，存储键和 CLI 值未改；这是历史参数初始化/解释修复，不能反推全部旧新路径差异都来自该处。

## 关联模块

[填充、顶底面与窄区](fill.md)、[FFF 切片步骤与层状态](slicing.md)、[参数面板与对象覆盖](gui-parameters.md)、[二维／三维几何与空间算法](geometry.md)。
