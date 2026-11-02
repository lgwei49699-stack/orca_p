# FFF 切片步骤与层状态

<!-- module-review
{
  "id": "slicing",
  "source_fingerprint": "sha256:a754acda51f6e591aca211625c644685e912b050231beb9e3535fed5e2dfd261",
  "reason": "旧矩阵维度兼顾颜色表与真实耗材参数，避免坏颜色表绕过兼容；保留手调配对/倍率和绑定。10种完整Print生命周期场景及两份历史崩溃工程通过，重复apply保持。"
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

只读 `slice_mesh_with_diagnostics` 仍走普通 `slice_mesh` 的同一连接/补缝实现；记录开放链/长度、非零补缝及模式处理前闭圈数/绝对面积。`slice_mesh_ex_with_diagnostics` 只切一次，并与普通 `slice_mesh_ex` 共用区域构造 helper（填充规则、closing/offset、模式和简化不变），逐层释放原始闭圈。未焊接重合顶点不是裂缝，缺水平顶面仍可有闭合 Z 截面；原闭圈面积和不是实体面积。只在 --repair-evidence 的受限残留缺陷路径做一次 Z 观察；普通旧修复调用不新增扫描，不自动升级重建，不生成墙/填充/支撑。

`PrintObject::slice` 的既有“没有层”检查移至 slice_volumes 后，早于 polyhole/brim 的首层访问，避免反向重叠壳导致全空层后访问 layers.front()。沿用原 SlicingError 和文案，正常非空切片行为不变；这不是自动修补丢失区域。

Print::apply 将 Model 和完整配置拆成 PrintObject／PrintRegion，检测差异使相应步骤失效；Print::process 组织切层、轮廓、填充、支撑、工具排序和输出准备。层对象保存 surfaces 与 extrusion collections，缓存序列化还保存路径顺序属性。

网格替换时可保留未改体积的边界缓存，但 `PartiallyValid` 也必须检验 `trafo_bboxes` 与当前实例变换的兼容性。Z 平移、Z 缩放或倾斜不兼容时，在当前 apply 中清除旧体积边界/层区并用新变换生成；不能先用旧坐标切片、下一次无改动刷新才失效。原有允许绕 Z 旋转等兼容变换的缓存复用规则保留。

## 配置与约束

全局打印、对象、体积／区域和分层覆盖需按 apply 合成。可变层高、首层和筏层抬升会改变物理 Z；层编号并不总等于模型第几层。复用支撑缓存前必须恢复正确来源。

当前分支可检索的配置键：`layer_height`、`initial_layer_print_height`、`print_sequence`、`wall_loops`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

PrintTryCancel 与步骤状态配合，异常／取消不得标为完成。只使 posSlice 失效而漏掉依赖阶段会造成重切与首次切片不一致；修改依赖应验证新建和重用 Print 两条路径。

## 修改与验证

用户要求优先兼容旧配置：`Print::apply` 在配置差异计算之前，按已配置耗材数补齐缺失、空白、非方阵或过小的冲刷矩阵。完整旧方阵的已有配对值原样保留（含手动0），缺项沿用GUI增减耗材时的 `flush_volumes_vector` 换出+换入规则；对应项缺失时回退第一槽同项，再回退140。非方阵无法确定行步长，按预设换色量重建；倍率、温度、颜色映射不变。补齐写入本次有效配置副本，使预览、切片、G-code及重复apply一致；完整且足够大的矩阵不变，无法识别的耗材引用仍由擦料塔边界检查保护。

槽数取颜色表与 `filament_diameter` 数量的较大值，旧工程缺少颜色时仍能识别已配置耗材；不扩写颜色表或重绑定模型。换料排序自身也在首次读取矩阵前校验工具索引，不能只依赖排序结束后的擦料塔检查。新增空/1色/2色表与4份真实耗材的生命周期用例，验证工具0/3、完整配置和重复apply；CLI旧项目的原项目元数据仍保留输入值，实际切片/G-code使用 `Print::apply` 补齐后的配置，再打开会重新兼容。

`Print::_make_wipe_tower` 在工具排序确认需要擦料塔后、创建擦料塔前校验冲刷矩阵为 N×N，且覆盖实际使用的工具。异常抛出可读 `SlicingError`，GUI 后台切片与 CLI 共享；不改层计划、墙/填充/支撑生成、正常冲刷量和步骤失效规则。原 EP7 139 复现及修后验证见[EP7 多 OBJ 修复记录](../validation/2026-11-02-ep7-multi-obj-wipe-tower-crash.md)。

关注 invalidate_state_by_config_options、apply 差异和完整 process 链。`[PrintApplyCache]` 覆盖仅换网格、同时落床/Z缩放/倾斜/绕Z旋转，有无分层覆盖、未改体积缓存与新建 Print 的边界对照，以及完整切片/G-code 后重复 apply 稳定。真实参数或位置改变仍须失效。GUI 后台刷新和 CLI 切片共用此核心；具体执行结果、真实龙模型修前修后证据见[验证记录](../validation/2026-10-04-smart-print-engine.md)，GUI 首次导出按交接清单另验。

现有测试/工具范围：`tests/fff_print/test_print.cpp`；`tests/fff_print/test_printobject.cpp`；`tests/fff_print/test_flow.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[MeshSliceDiagnostics]` 对照普通/诊断输出，覆盖区域转换的模式组合、closing/offset/resolution、变换、抵消空区域和取消。`test_printobject.cpp` 的 `[SectionPreflight]` 用真实 PrintObject 层计划对照健康/空腔/部分及全部抵消，区分 slice_z 与 print_z；只在开发回归运行，不在生产额外切片。证据解释归 model-repair；测试通过不等于所有工艺路径或打印质量保证。

## 关联模块

[周长墙与 Arachne 可变线宽](walls.md)、[填充、顶底面与窄区](fill.md)、[普通、树状与有机支撑](support.md)、[筏层计划、合并与分块](raft.md)、[G-code 生成与后处理](gcode-export.md)。
