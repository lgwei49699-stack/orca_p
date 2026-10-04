# master 专题的当前状态与历史边界

本页仅整理现有提交与文档，不代表重跑业务验收。初始化基线为 `d73b954fc3af`。

| 范围 | 已存在提交 | 当前阅读入口 |
| --- | --- | --- |
| 筏层折返和分块路径顺序 | `54619c6c0f` | [筏层模块](../modules/raft.md) |
| Arachne 初始化与合法零值 | `88a8a140d4` | [墙模块](../modules/walls.md) |
| 最小墙长系数单位说明 | `b72b8769aa` | [参数模块](../modules/gui-parameters.md) |
| 向导机器继承与坏项隔离 | `7b886a25e6` | [向导模块](../modules/guide.md) |
| EP3 迁移别名与 Custom .79 | `c476920331` | [资源模块](../modules/resource-profiles.md) |
| 喷嘴硬度说明 | `d73b954fc3` | [预览模块](../modules/gcode-preview.md) |

原工作区两份筏层文档的增量记录已保留在 master 工作目录。其中日期较早段落的“留待提交/未提交”描述当时的工作区状态，后续独立提交已改变状态；不删除旧记录，也不把组合构建说成每笔提交的干净构建。

历史 40 条短线的完整触发未还原、传统筏层短连接重复性、真实大模型/取消延迟/跨平台和实机打印质量仍按原证据边界记录。本轮不宣布这些事项已经解决。下一轮是否扩大业务范围需按用户后续要求；文档工具通过不等于新增切片验收。

[进度原文](../../../doc/print_settings/support/raft_geometry_trial_progress.md)；[后续计划](../../../doc/print_settings/support/raft_next_iteration_plan.md)。
