# 模型数据与网格属性

<!-- module-review
{
  "id": "model",
  "source_fingerprint": "sha256:047f927a26338c77838d20174c1b21ce9e032017164e2b2ed31d789d19538a9b",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责模型数据与网格属性。归属以[模块清单](../modules.json)的 `model` 为准。

## 入口与调用

- [Model.hpp](../../../src/libslic3r/Model.hpp)：定义 Model→ModelObject→ModelVolume／ModelInstance 的所有权、ID 和配置接口。
- [Model.cpp](../../../src/libslic3r/Model.cpp)：`Model::read_from_file`／`read_from_archive` 分发导入；复制与链接更新保留对象结构。
- [TriangleMesh.cpp](../../../src/libslic3r/TriangleMesh.cpp)：网格读写、几何变换及基础属性；`ReadSTLFile`／`from_stl` 接受是否基础修复的选择。
- [TriangleSelector.cpp](../../../src/libslic3r/TriangleSelector.cpp)：三角面选择、细分和面状态存储；涂色／支撑绘制依赖这些状态。

## 核心流程

Model 包含对象、体积、实例和材质，TriangleMesh 保存三角网格，TriangleSelector 保存绘制选择；导入器建立这些结构，编辑器修改后 Print::apply 以 ID／变换／配置差异重建打印对象。共享网格与实例副本不能混为一谈。

## 配置与约束

体积类型、材质编号、挤出机编号、变换及 supported/seam/MMU/fuzzy 绘制都附着在不同对象层。几何修复若改变面索引，必须由修复事务判断是否保留或重新映射属性。

## 失败与取消

网格为空、索引越界、非有限坐标与非流形是不同问题；不要只凭有体积或 signed volume 判定可打印。导入的基础整理也不能替代完整几何检测。

## 修改与验证

修改 mesh、复制和变换优先测试导入往返、实例与体积归属、包围盒失效、绘制保存恢复；关联修复测试需确认所用二进制身份。

现有测试/工具范围：`tests/fff_print/test_model.cpp`；`tests/fff_print/test_trianglemesh.cpp`；`tests/libslic3r/test_saved_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[二维／三维几何与空间算法](geometry.md)、[模型检测、修复与保色](model-repair.md)、[场景、对象与交互编辑](scene-editing.md)。
