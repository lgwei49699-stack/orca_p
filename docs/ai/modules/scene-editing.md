# 场景、对象与交互编辑

<!-- module-review
{
  "id": "scene-editing",
  "source_fingerprint": "sha256:18999b94d32878c5d6f1c4c9475987fe36d33008962d1f518b5b12932bac0537",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责场景、对象与交互编辑。归属以[模块清单](../modules.json)的 `scene-editing` 为准。

## 入口与调用

- [GUI_ObjectList.cpp](../../../src/slic3r/GUI/GUI_ObjectList.cpp)：`ObjectList` 管理对象树及选择索引，将模型／体积／实例选择映射到编辑动作。
- [Selection.cpp](../../../src/slic3r/GUI/Selection.cpp)：`Selection` 维护场景选中项、剪贴板与变换操作，关联 GLVolume 和模型实例。
- [GLGizmosManager.cpp](../../../src/slic3r/GUI/Gizmos/GLGizmosManager.cpp)：`GLGizmosManager` 初始化并切换编辑工具，分发鼠标拾取、渲染和工具状态。

## 核心流程

对象树映射 ModelObject／ModelVolume／实例和盘，Selection 维护场景选中集合；GLGizmosManager 将移动、旋转、切割、涂色、支撑绘制等操作分派给 gizmo。几何提交后更新包围盒、撤销快照、盘归属并使切片失效。

## 配置与约束

对象参数、体积／部件参数和变换是不同层次。模型部件、负体积和修改器不能按同一种网格直接导出。模型局部坐标、实例变换和盘原点必须分清。

## 失败与取消

没有选中项时不能索引空集合；取消交互应恢复原状态。修改几何的操作需保持对象 ID 与已保存绘制信息对应关系，不可绕过 Model 的失效与备份机制。

## 修改与验证

修改入口在 GUI_ObjectList 与具体 gizmo；检查多选、删除、拆分／合并、撤销重做、切换盘及保存重开。涂色还需 SavedPainting 回归，交互正确不等于切片已验证。

现有测试/工具范围：`tests/libslic3r/test_saved_painting.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[模型数据与网格属性](model.md)、[工程保存、撤销和恢复](project-state.md)、[参数面板与对象覆盖](gui-parameters.md)、[渲染、控件与平台 UI](render-ui.md)。
