# FFF 切片步骤与层状态

<!-- module-review
{
  "id": "slicing",
  "source_fingerprint": "sha256:05645126d63b0fcb040edcd76f51d03218211eda0f0ccf150d4f470e165a62f4",
  "reason": "修正同次网格替换和实例变换后的部分有效区域缓存：复用前验证坐标兼容，不兼容则同步重建边界和层区。补导出后无变更重入、分层/未改体积、新建Print对照和真实配置/位置修改失效回归，不修改切片参数或绕过失效。"
}
-->

## 职责与边界

本模块负责FFF 切片步骤与层状态。归属以[模块清单](../modules.json)的 `slicing` 为准。

## 入口与调用

- [Print.cpp](../../../src/libslic3r/Print.cpp)：Print::process；对象切片到输出准备的全流程。
- [PrintApply.cpp](../../../src/libslic3r/PrintApply.cpp)：Print::apply；模型/有效配置差异及步骤失效。
- [PrintObject.cpp](../../../src/libslic3r/PrintObject.cpp)：对象墙、填充、支撑及状态依赖处理。
- [PrintObjectSlice.cpp](../../../src/libslic3r/PrintObjectSlice.cpp)：对象切层与区域构造。
- [SlicingAdaptive.cpp](../../../src/libslic3r/SlicingAdaptive.cpp)：层高曲线解析及层级生成。

## 核心流程

Print::apply 将 Model 和完整配置拆成 PrintObject／PrintRegion，检测差异使相应步骤失效；Print::process 组织切层、轮廓、填充、支撑、工具排序和输出准备。层对象保存 surfaces 与 extrusion collections，缓存序列化还保存路径顺序属性。

网格替换时可保留未改体积的边界缓存，但 `PartiallyValid` 也必须检验 `trafo_bboxes` 与当前实例变换的兼容性。Z 平移、Z 缩放或倾斜不兼容时，在当前 apply 中清除旧体积边界/层区并用新变换生成；不能先用旧坐标切片、下一次无改动刷新才失效。原有允许绕 Z 旋转等兼容变换的缓存复用规则保留。

## 配置与约束

全局打印、对象、体积／区域和分层覆盖需按 apply 合成。可变层高、首层和筏层抬升会改变物理 Z；层编号并不总等于模型第几层。复用支撑缓存前必须恢复正确来源。

当前分支可检索的配置键：`layer_height`、`initial_layer_print_height`、`print_sequence`、`wall_loops`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

PrintTryCancel 与步骤状态配合，异常／取消不得标为完成。只使 posSlice 失效而漏掉依赖阶段会造成重切与首次切片不一致；修改依赖应验证新建和重用 Print 两条路径。

## 修改与验证

关注 invalidate_state_by_config_options、apply 差异和完整 process 链。`[PrintApplyCache]` 覆盖仅换网格、同时落床/Z缩放/倾斜/绕Z旋转，有无分层覆盖、未改体积缓存与新建 Print 的边界对照，以及完整切片/G-code 后重复 apply 稳定。真实参数或位置改变仍须失效。GUI 后台刷新和 CLI 切片共用此核心；具体执行结果、真实龙模型修前修后证据见[验证记录](../validation/2026-10-04-smart-print-engine.md)，GUI 首次导出按交接清单另验。

现有测试/工具范围：`tests/fff_print/test_print.cpp`；`tests/fff_print/test_printobject.cpp`；`tests/fff_print/test_flow.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[填充、顶底面与窄区](fill.md)、[普通、树状与有机支撑](support.md)、[筏层计划、合并与分块](raft.md)、[G-code 生成与后处理](gcode-export.md)。
