# 工程保存、撤销和恢复

<!-- module-review
{
  "id": "project-state",
  "source_fingerprint": "sha256:17a6e6a2f51f858697535ba781407dd8df8f3ac58cc9162b0ecc99a8361af2b2",
  "reason": "复核共享导出失败返回及原文件保护；新增PlateReferences覆盖无效引用/缺映射和临时包清理，未修改脏标记、Undo或崩溃恢复流程。"
}
-->

## 职责与边界

本模块负责工程保存、撤销和恢复。归属以[模块清单](../modules.json)的 `project-state` 为准。

## 入口与调用

- [UndoRedo.cpp](../../../src/slic3r/Utils/UndoRedo.cpp)：`UndoRedo` 快照栈序列化可变／共享对象，用于撤销恢复，与磁盘工程保存分开。
- [ProjectDirtyStateManager.cpp](../../../src/slic3r/GUI/ProjectDirtyStateManager.cpp)：`update_from_undo_redo_stack`／`update_from_presets` 汇总脏状态；成功保存后调用 reset_after_save。
- [Project.cpp](../../../src/slic3r/GUI/Project.cpp)：`ProjectPanel` 承载工程信息网页及 OnScriptMessage；磁盘保存主入口在 Plater。
- [Snapshot.cpp](../../../src/slic3r/Config/Snapshot.cpp)：`Snapshot` 保存应用配置快照和预设选择，用于配置恢复，不是 3MF 文件序列化器。

## 核心流程

UndoRedo 记录模型与选择相关快照，ProjectDirtyStateManager 汇总模型与预设脏状态；Plater 承担工程打开、保存、备份和关闭确认。Config/Snapshot 保存应用配置快照，与 3MF 工程保存不是一个机制。

## 配置与约束

工程路径、导入源路径、缓存／临时目录和恢复目录的生命周期不同。最终用户保存应使用可持久保存的位置，历史引用失效不能误报成模型检测失败。

## 失败与取消

保存失败不能清除脏标记；取消保存和取消退出要区分。临时目录中直接保存可能仍受清理策略影响，不因用户换目录就宣称软件清理缺陷已修复。

切片包导出与普通保存分离：前者在刷新或文件框后失效/缺 G-code 时要求重切并禁止成功空包；后者允许没有有效切片的模型工程。共享导出写失败继续使用临时文件回滚，不能覆盖已有用户文件。`[SlicedExport]` 已覆盖源文件在进度回调消失时保留旧目标，以及普通未切片工程仍成功；不代表 Undo/崩溃恢复全部重新验过。


共享3MF导出器遇到无效盘成员或缺失导出对象映射会明确失败，不再继续写损坏元数据。`[PlateReferences]` 验证拒绝错误引用时旧目标不被覆盖、临时包被清理；普通未切片工程及纯G-code盘仍可保存。GUI文件框、脏标记提示及重开仍按[交接清单](../validation/2026-10-19-cli-fixed-position-slicing.md)手测，不能用格式单测替代。

## 修改与验证

改保存逻辑需测试另存为、覆盖保存、关闭取消、崩溃恢复、中文盘名和重开一致性。检查文件实际路径和 ZIP 内容，通知文字不是持久化证据。

现有测试/工具范围：`tests/libslic3r/test_3mf.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

云方案来源记录 `user/gfd_cloud_process_records.json` 保留实际保存档位与本地机型／工艺／耗材对应关系；新记录不写来源环境或账号，历史 environment/account/account_kind 字段不参与来源解析、按钮显示或保存校验。成功导入／替换另记 `cloud_import_fineness`，用于旧标准工艺的列表显示后缀；上传／保存保留已有导入标记，但不会为未导入项新建标记。没有标记的历史关联不推断为已导入，下一次成功同步后补记。该标记不重命名文件或来源键。旧记录无档位、null或空串按 standard，未知类型拒绝。保存窗口默认来源类型，可改选目标类型；成功回读后记录实际保存档位。导入后沿用恢复原选中工艺的业务，来源记录仍绑定实际导入工艺；本轮不新增自动切换或改变来源绑定。它用于GUI云端保存目标识别，不等同工程保存或Undo快照，详见[云模块](cloud.md)。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[模型数据与网格属性](model.md)、[GUI 生命周期与主工作台](gui-shell.md)、[场景、对象与交互编辑](scene-editing.md)。
