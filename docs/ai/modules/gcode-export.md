# G-code 生成与后处理

<!-- module-review
{
  "id": "gcode-export",
  "source_fingerprint": "sha256:046d6b5fe328060292d52b4089415ad9c24e6e1f602fbe26a076db73b7bd0299",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。补登记公共参数定义依赖；核对配置约束沿用本分支实现。"
}
-->

## 职责与边界

本模块负责G-code 生成与后处理。归属以[模块清单](../modules.json)的 `gcode-export` 为准。

## 入口与调用

- [GCode.cpp](../../../src/libslic3r/GCode.cpp)：层路径、换刀、空移与最终输出协调。
- [GCodeWriter.cpp](../../../src/libslic3r/GCodeWriter.cpp)：机器 flavor 对应的指令写入。
- [SeamPlacer.cpp](../../../src/libslic3r/GCode/SeamPlacer.cpp)：`SeamPlacer::init` 准备接缝候选、可见性及层关系，供导出阶段选择轮廓起点。

## 核心流程

GCode 消费 Print 的层和挤出集合，协调工具顺序、接缝、回抽和空移；GCodeWriter 输出机器指令，CoolingBuffer/FanMover/PressureEqualizer 等进一步处理。配置块、缩略图和自定义起止代码随导出写入。

## 配置与约束

机器 flavor、绝对／相对挤出、速度／加速度限制、耗材流量和占位符都是最终指令的输入。保序集合必须保留块内顺序，但块间允许必要空移；不能用“没有空移”作为正确标准。

当前分支可检索的配置键：`gcode_flavor`、`use_relative_e_distances`、`machine_start_gcode`、`machine_end_gcode`、`filament_flow_ratio`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

占位符失败、磁盘写入失败、取消和后处理进程错误需检查实际出口。导出完成不等于真实打印成功。不要将额外路径与预览中的空移线混为一谈。

## 修改与验证

比较层/角色/挤出/空移而非单一文件哈希（时间头可能变化）；修改路径顺序要确认模型路径未被无关改变，缩略图生成由格式模块负责。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`；`tests/fff_print/test_gcodewriter.cpp`；`tests/fff_print/test_printgcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[FFF 切片步骤与层状态](slicing.md)、[筏层计划、合并与分块](raft.md)、[多材料、颜色分配与擦料塔](multimaterial.md)、[模型／工程格式、颜色与缩略图](formats.md)、[G-code 解析、统计与预览](gcode-preview.md)。
