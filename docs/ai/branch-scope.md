# 分支功能边界

两分支基线分别为 master `d73b954fc3af`、master_v_user `9ce27fdbcc83`，源分支相差 330 文件（基线对比，不是本次改动量）。本表为初始化时的实际差异导航；后续修改应随对应模块一起更新，不拿历史基线证明新代码。

| 范围 | master | master_v_user |
| --- | --- | --- |
| [模型检查](modules/model-repair.md)／[CLI](modules/cli.md) | 独立 --check-model 与产物复检工作流 | 有高级修复，无该独立检测工作流 |
| [筏层](modules/raft.md) | Cura 计划、兼容合并、分块／平滑及连续路径处理 | Cura 计划与角度解析，未同步合并／分块后处理 |
| [分盘](modules/arrangement.md)／[Brim](modules/adhesion.md) | 混摆预留、兼容紧凑排列及有限贴边补放／裁剪 | 既有排列与边界处理，未同步这些增强 |
| [墙与 Arachne](modules/walls.md) | 确定默认值、合法零值传递及“倍”单位说明 | 尚未同步上述参数修正 |
| [配置向导](modules/guide.md) | 机器元数据递归继承，坏项隔离 | 隐藏机型选择保留，机器直接字段及名称回退 |
| [文件格式](modules/formats.md)／[G-code 预览](modules/gcode-preview.md) | 保留本分支来源／版本读取规则 | 产品／Orca 兼容标记及共用 G-code 品牌识别 |
| [云端](modules/cloud.md) | 保留工艺／耗材云同步入口 | 去掉参数云同步入口，其他云服务仍保留 |
| [设备资源](modules/resource-profiles.md) | EP3 旧别名修正与指定 EP 系列封面／EPONE Plus | 保留用户版设备筛选及本分支资源版本 |
| [构建与品牌](modules/build.md) | OrcaSlicer 名称及平台打包规则 | WiseBeginnerSlicer 品牌及对应平台资源 |

本表只作差异导航，数值、限定条件和验证状态集中在当前分支的模块正文。跳转链接只打开当前分支版本；查看另一分支时先切换到其工作目录，不按此表推断其测试已通过。

## 本分支专题

- [筏层进度](../../doc/print_settings/support/raft_geometry_trial_progress.md)、[下一轮计划](../../doc/print_settings/support/raft_next_iteration_plan.md)：原工作区两份未提交文档已纳入本分支整理，并补充文档入口提示；原有历史正文、结论及未验证边界保留。
- [紧凑分盘](../../doc/print_settings/support/raft_compact_arrange_progress.md)、[模型修复](../../doc/model-check-workflow.md)、[设备资源同步](../../doc/ep-device-resources-sync-analysis.md) 为专题入口。

本轮不复制业务代码、不修改打印参数/项目格式、不编译切片程序，不自动合并或推送。相同规范/检查工具可维护同步，分支正文和指纹需要独立复核。
