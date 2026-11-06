# SLA 切片与支撑

<!-- module-review
{
  "id": "sla",
  "source_fingerprint": "sha256:e1cc7f8984eb460af50467e07664c142b20ead2b852797a10489afcb5a433d16",
  "reason": "2026-11-06：复核共享 PrintConfig 差异仅在 CLIMiscConfigDef 新增四项重建准入配置并明确内存为启动前估算；未改本模块工艺参数、默认值或执行路径，无需修改正文。"
}
-->

## 职责与边界

本模块负责SLA 切片与支撑。归属以[模块清单](../modules.json)的 `sla` 为准。

## 入口与调用

- [SLAPrint.cpp](../../../src/libslic3r/SLAPrint.cpp)：`SLAPrint` 维护 SLA 对象、变换及任务步骤；支撑和底座配置分别解析。
- [SLAPrintSteps.hpp](../../../src/libslic3r/SLAPrintSteps.hpp)：中空→排孔→切层→支撑/底座→光栅化步骤接口。
- [SupportTree.cpp](../../../src/libslic3r/SLA/SupportTree.cpp)：`SupportTree::create` 构建树，`retrieve_full_mesh`／`slice` 输出支撑网格或层轮廓。

## 核心流程

SLAPrint/Steps 组织中空、排液孔、模型切层、支撑点、支撑树、底座、支撑切层以及光栅化。SLA/ 子目录实现树、底座和栅格，Format/SL1 处理对应容器；它与 FFF 的熔融筏层计划不是同一流程。

## 配置与约束

SLA 物体层与整盘合并／光栅化步骤分别失效，材料曝光与修正参数不应映射成 FFF 流量或速度。技术类型由配置决定，切换机型需清理对应状态。

## 失败与取消

Steps 使用 Print 取消状态，不能在失败后提交半套切片层。中空和树生成的几何限制需专门验证，FFF 支撑通过不能证明 SLA 通过。

## 修改与验证

现有 tests/sla_print 源码在 tests/CMakeLists.txt 的 add_subdirectory 被注释；存在测试文件不等于默认构建或已执行。按[验证规范](../validation.md)确认可用目标、配置及必要构建；无法运行时记录实际限制。

现有测试/工具范围：`tests/sla_print/**`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[二维／三维几何与空间算法](geometry.md)、[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[后台切片、任务和取消](jobs.md)。
