# FFF 切片步骤与层状态

<!-- module-review
{
  "id": "slicing",
  "source_fingerprint": "sha256:b737bbbfc865bb5e50666da24d8bc2a50b2bb36b2981ef1154c8dcf4ce8d2883",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
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

## 配置与约束

全局打印、对象、体积／区域和分层覆盖需按 apply 合成。可变层高、首层和筏层抬升会改变物理 Z；层编号并不总等于模型第几层。复用支撑缓存前必须恢复正确来源。

当前分支可检索的配置键：`layer_height`、`initial_layer_print_height`、`print_sequence`、`wall_loops`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

PrintTryCancel 与步骤状态配合，异常／取消不得标为完成。只使 posSlice 失效而漏掉依赖阶段会造成重切与首次切片不一致；修改依赖应验证新建和重用 Print 两条路径。

## 修改与验证

关注 invalidate_state_by_config_options、apply 差异和完整 process 链。测试模型层数/Z、重复切片、改配置后重切、取消恢复；本轮没有运行切片业务验收。

现有测试/工具范围：`tests/fff_print/test_print.cpp`；`tests/fff_print/test_printobject.cpp`；`tests/fff_print/test_flow.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[填充、顶底面与窄区](fill.md)、[普通、树状与有机支撑](support.md)、[筏层计划、合并与分块](raft.md)、[G-code 生成与后处理](gcode-export.md)。
