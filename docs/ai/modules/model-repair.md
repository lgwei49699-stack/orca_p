# 模型检测、修复与保色

<!-- module-review
{
  "id": "model-repair",
  "source_fingerprint": "sha256:8dc21150701c5bad40757ad157dbefd4176643a6cb2e80e4ede57e9130112646",
  "reason": "核对共享 CGAL 阶段计时、局部拓扑预算/证明失败诊断，以及已确定拒绝时跳过距离计算；阈值、修复步骤、公共协议和 GUI/CLI 策略未变。补充诊断专项及当前交接，不沿用已撤回保守模式结论。"
}
-->

## 职责与边界

本模块负责模型检测、修复与保色。归属以[模块清单](../modules.json)的 `model-repair` 为准。

## 入口与调用

- [ModelRepair.cpp](../../../src/libslic3r/ModelRepair.cpp)：repair_model/repair_staged_model；私有候选与事务提交。
- [MeshBoolean.cpp](../../../src/libslic3r/MeshBoolean.cpp)：网格布尔、分割与 CGAL／Mcut 适配；高级修复从 ModelRepair 调用相关核心接口。
- [FixModelByCgal.cpp](../../../src/slic3r/Utils/FixModelByCgal.cpp)：GUI 修复与工作线程/主模型之间的桥接。
- [ModelCheckWorkflow.cpp](../../../src/libslic3r/ModelCheckWorkflow.cpp)：run_model_check_workflow 与业务结果投影。
- [ModelInspection.cpp](../../../src/libslic3r/ModelInspection.cpp)：inspect_model_mesh 及受限基础清理计划。

## 核心流程

GUI 修复入口经 FixModelByCgal 调用模型修复核心。repair_model 创建隔离的 staging 模型，选择 MODEL_PART 体积；修复前检查、网格修复、属性校验成功后才提交。repair_staged_model 供已隔离的调用者使用，失败结果不可直接写回原模型。

共享 CGAL 修复在 info 日志记录 `stage_finished` 与 `timing_summary`，以单次调用 `repair_id` 关联、单调时钟毫秒计时。统计仅覆盖该次 CGAL 候选生成/校验，不含外层模型检测、绘制恢复、导出或 GUI 等待；阶段包含其中的检查点工作，不能将汇总时间与阶段时间重复相加。`candidate_rejected` 不等于整个模型失败，外层仍可能采用已验证的部分成果；异常退出单独标为 `exception`。

局部拓扑准备记录 `soup_topology_result`：`face_limit`、`boundary_query_budget`、`boundary_split_limit`、`boundary_pass_limit`、`removed_face_limit`、`interior_contact_limit` 表示对应资源保护；`no_topology_change` 表示无适用修改；其余 `orientation_unproven`、`candidate_*`、`removed_face_*`、`float_output_invalid` 等说明几何证明/验收未通过。跳过本分支仍走既有后续处理，不把超限说成整个引擎不能修复；全部限制未改。

## 配置与约束

几何是否需要修复、是否允许拆壳／删除非三维部件、是否允许清除绘制与能否保色是独立政策。GUI／CLI 的默认策略必须分别查看调用处，不能因为共用 CGAL 就认为都允许丢弃材质。相同修复选项下应按同一规则保留几何与属性；现存入口差异需记录原因／影响，新改动不能只更新一端。

闭合候选及 polygon-soup 候选的形状验收先判断原有部件数、包围盒、面积/体积条件；已确定拒绝时不计算 Hausdorff 距离，日志 `distance_checked=false`，距离字段为未计算的 NaN，不代表零偏差。廉价条件通过时仍运行原有单向/双向距离检查，阈值、候选接受条件、保色、公开检测/修复 JSON 均不变。不增加保守模式、不改变取消架构或 20,000 面上限。

## 失败与取消

单体积失败或用户取消可导致整个事务回退；CGAL 单次内部操作不保证即时可中断。检查函数抛错不能转换成“需要修复”继续更重操作。成功写文件也不证明修复后的网格正确。

## 修改与验证

改核心需检验健康模型、开边、退化／重复面、空体积、材质与绘制、失败原模型不变。按验证规范选 `[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`，无匹配不能计为通过。

现有测试/工具范围：`tests/libslic3r/test_model_repair.cpp`；`tests/libslic3r/test_meshboolean.cpp`；`tests/libslic3r/test_saved_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

计时/退出原因专项标签为 `[ModelRepair][Diagnostics]`：覆盖成功、拒绝、异常、边界查询预算及面数上限，断言后续分支继续、拒绝不修改原网格；耗时仅要求非负，不使用脆弱的速度阈值。必要优化的构建、CLI 证据和 GUI 交接复用[执行记录](../validation/2026-10-04-smart-print-engine.md)的当前任务章节，后半部分已撤回方案不得作为现行行为。

## 分支现状

本分支新增 ModelInspection/ModelCheckWorkflow：检查入口与导出产物复检共享 256 MiB 输入上限及 300 万面/顶点默认，交叉检测另有资源限制。检测不完整不能计为通过。基础清理限精确零面积/同向重复面并保留属性与事务约束；公开业务检测结果由专用投影给出，不把内部历史结构直接输出。普通 STL/OBJ 导出与此流程不同。

## 关联模块

[模型数据与网格属性](model.md)、[模型／工程格式、颜色与缩略图](formats.md)、[应用入口与 CLI](cli.md)、[后台切片、任务和取消](jobs.md)。
