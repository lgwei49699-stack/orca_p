# 开发与 AI 模块文档

本轮文档归档基准为 `codex/model-feature-analysis-20261018` 的 `f7df7e11c8`，范围与验证见[整理记录](validation/2026-11-06-feature-documentation-consolidation.md)。最初的 `master/d73b954fc3` 仅是文档初始化基线，当前行为以所在分支源码、指纹和[分支边界](branch-scope.md)为准。

## 开始阅读

1. 确认 `git status --short`、`git branch --show-current`、`git rev-parse HEAD`，阅读 [仓库规则](../../AGENTS.md)。
2. 按下表只读任务涉及的模块与对应源码；跨模块流程再读 [架构与流程](architecture.md)，跨分支同步再读 [分支边界](branch-scope.md)。历史记录仅在追溯证据时打开，不每次全量阅读。
3. 读模块入口与相关源码；提交前遵循 [增量维护](maintenance.md) 和 [验证规范](validation.md)。需要用户操作界面时按 [GUI 验证交接](gui-validation.md) 输出并保存具体清单。工具不可用时按 [CodeGraph 降级](tooling.md) 继续。

日常入口以 `AGENTS.md` 为准；`CLAUDE.md` 仅为兼容其他客户端的短转接，无需重复阅读。代码图可用时辅助定位，不可用时文档与源码足以继续任务。

App 检测／修复精简：[2026-10-30 两阶段实现与本地耗时验证](validation/2026-10-30-app-repair-latency.md)（本地验证通过，轻量化保持原流程，QA／真机待验）。

## 模块索引

| ID | 文档 | 主要入口 |
| --- | --- | --- |
| `cli` | [应用入口与 CLI](modules/cli.md) | `src/OrcaSlicer.cpp` |
| `gui-shell` | [GUI 生命周期与主工作台](modules/gui-shell.md) | `src/slic3r/GUI/GUI_App.cpp` |
| `scene-editing` | [场景、对象与交互编辑](modules/scene-editing.md) | `src/slic3r/GUI/GUI_ObjectList.cpp` |
| `gui-parameters` | [参数面板与对象覆盖](modules/gui-parameters.md) | `src/slic3r/GUI/Tab.cpp` |
| `jobs` | [后台切片、任务和取消](modules/jobs.md) | `src/slic3r/GUI/BackgroundSlicingProcess.cpp` |
| `arrangement` | [自动分盘与打印板](modules/arrangement.md) | `src/libslic3r/Arrange.cpp` |
| `model` | [模型数据与网格属性](modules/model.md) | `src/libslic3r/Model.hpp` |
| `model-repair` | [模型检测、修复与保色](modules/model-repair.md) | `src/libslic3r/ModelRepair.cpp` |
| `geometry` | [二维／三维几何与空间算法](modules/geometry.md) | `src/libslic3r/Geometry.cpp` |
| `slicing` | [FFF 切片步骤与层状态](modules/slicing.md) | `src/libslic3r/Print.cpp` |
| `walls` | [周长墙与 Arachne 可变线宽](modules/walls.md) | `src/libslic3r/PerimeterGenerator.cpp` |
| `fill` | [填充、顶底面与窄区](modules/fill.md) | `src/libslic3r/Fill/Fill.cpp` |
| `support` | [普通、树状与有机支撑](modules/support.md) | `src/libslic3r/Support/SupportMaterial.cpp` |
| `raft` | [筏层计划、合并与分块](modules/raft.md) | `src/libslic3r/Support/RaftPlan.hpp` |
| `adhesion` | [Brim、Skirt 与平台附着](modules/adhesion.md) | `src/libslic3r/Brim.cpp` |
| `multimaterial` | [多材料、颜色分配与擦料塔](modules/multimaterial.md) | `src/libslic3r/MultiMaterialSegmentation.cpp` |
| `gcode-export` | [G-code 生成与后处理](modules/gcode-export.md) | `src/libslic3r/GCode.cpp` |
| `gcode-preview` | [G-code 解析、统计与预览](modules/gcode-preview.md) | `src/libslic3r/GCodeReader.cpp` |
| `formats` | [模型／工程格式、颜色与缩略图](modules/formats.md) | `src/libslic3r/Format/bbs_3mf.cpp` |
| `presets` | [配置定义、预设继承与资源更新](modules/presets.md) | `src/libslic3r/PrintConfig.cpp` |
| `guide` | [配置向导与预设创建](modules/guide.md) | `src/slic3r/GUI/WebGuideDialog.cpp` |
| `project-state` | [工程保存、撤销和恢复](modules/project-state.md) | `src/slic3r/Utils/UndoRedo.cpp` |
| `network-hosts` | [网络传输与打印主机](modules/network-hosts.md) | `src/slic3r/Utils/Http.cpp` |
| `devices` | [设备、AMS 与打印任务监控](modules/devices.md) | `src/slic3r/GUI/DeviceManager.cpp` |
| `cloud` | [账号、云端导入与模型服务](modules/cloud.md) | `src/slic3r/GUI/GFDAuthManager.cpp` |
| `calibration` | [校准模型与向导](modules/calibration.md) | `src/libslic3r/calib.cpp` |
| `render-ui` | [渲染、控件与平台 UI](modules/render-ui.md) | `src/slic3r/GUI/GLCanvas3D.cpp` |
| `sla` | [SLA 切片与支撑](modules/sla.md) | `src/libslic3r/SLAPrint.cpp` |
| `resource-profiles` | [打印机、耗材与工艺资源](modules/resource-profiles.md) | `resources/profiles/Custom.json` |
| `resource-ui` | [图标、着色器及静态辅助资源](modules/resource-ui.md) | `resources/data/hints.ini` |
| `web-assets` | [内嵌网页与原生消息桥](modules/web-assets.md) | `resources/web/guide/index.html` |
| `localization` | [中英文与翻译资源](modules/localization.md) | `localization/i18n/zh_CN/OrcaSlicer_zh_CN.po` |
| `build` | [构建、打包与平台集成](modules/build.md) | `CMakeLists.txt` |
| `test-tools` | [测试基础设施与维护工具](modules/test-tools.md) | `tests/CMakeLists.txt` |
| `platform` | [公共运行时与平台辅助](modules/platform.md) | `src/libslic3r/libslic3r.cpp` |

清单 [modules.json](modules.json) 是源码范围及测试关系的唯一机器输入。35 个模块覆盖业务与维护工具，内部子目录归所属模块；不为每个 C++ 文件生成文档。新文件未归属会报错，第三方/历史产物排除必须有明确原因。

## 记录与检查

[Thingi10K 六模型与官方 2.4.2 / 2.5.0 修复对照](validation/2026-10-30-thingi-repair-242-comparison.md) 记录2.4.2/2.5.0 Beta实测、当前共享核心移植及全入口策略修正；包含孔壁保留、两种重建回归和待GUI/平台验证，历史证据与当前代码分开。

[特征分支集中测试合并](validation/2026-10-30-feature-branch-consolidation.md) 记录发布分支回合、原工作区保留、影响范围与集中测试清单。

[Linux 2.3.2 发布分支同步](validation/2026-10-26-release-2.3.2-merge.md) 记录此次 master/特征分支合并、发布配置保留及验证边界。

[机关夹夹乐无支撑配置与实印不符](validation/2026-10-29-clamp-unexpected-support.md) 记录生产打印任务、历史3MF导入差异及旧系统切片，结论为方案导入与产物更新问题；仅分析，未修改引擎或生产数据。

[飞机尾部承托与原打印包参数核对](validation/2026-10-23-airplane-tail-support-review.md) 记录实物缺陷对应的原 G-code 路径、截图与包内配置差异及待执行的重切清单；参数建议尚未实印验证。

[AD5X 打印中跳过失败对象分析](validation/2026-10-19-ad5x-object-exclusion-analysis.md) 区分原厂屏幕能力、CLI 对象标记与自有平台远程控制缺口，附待执行的实机清单。

[模型打印优化：方案与首批指标](validation/2026-10-18-slice-analysis.md) 记录总体指标、原生功能复用和默认关闭的 CLI 只读报告；不将报告导出视为自动优化或实印质量验收。

[模型特征检测与一次匹配策略：工作进展](validation/2026-10-23-feature-strategy-progress.md) 是2026-10-23用户明确范围后的续接入口：G-code用于研发验证和修正规则，正常业务只匹配/切片一次，不建设线上多轮试参或自动择优。

[历史归档](history/README.md) 保存原始规则与验证流水；[本轮检查结果](validation-report.md) 只说明文档工具验证，不表示重新验证了切片算法。

[2026-10-13 OBJ 盖子空层与退出码 156 排查](validation/2026-10-13-obj-tree-support-156.md) 记录输入身份、短墙过滤单因素对照及本地／线上构建差异。

```sh
python3 scripts/check_ai_module_docs.py --all
python3 scripts/check_ai_module_docs.py --staged
```

检查器不会自动更新正文或审核标记，不安装 hooks/CI。`doc/` 的已有专题与 Wiki 工作流继续保留，AI 模块文档不参与 Wiki 发布。
