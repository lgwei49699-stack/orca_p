# 模型修复报告合入 Linux 2.3.2

## 当前状态

- 用户授权：推送已审核修复提交，在主目录同步并合入发布分支，再推送Linux打包标签。范围只包含Orca；其他服务不在本轮提交/发布范围。
- 来源：`orca-slicer-soft` 的 `codex/model-feature-analysis-20261018` / `aa39c82eb278014fa9046b44daf2fd932905db8f`，已推送GitHub同名分支。
- 主目录：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer`。发布基准 `linux/release2_3_2` / `e93370733b3b6c4221e4b407ba2d46925795b786`，与GitHub同名分支一致；功能分支先快进同步，再合并到发布分支。
- 开始时只有未跟踪文件 `docs/ai/validation/2026-10-27-tree-support-fill-isolation.md`，保留且不纳入合并。未使用reset、force或清理未跟踪文件。
- 3处冲突仅为cli/model-repair/test-tools模块的审核JSON；保留发布分支现有兼容修复说明与本次新增说明，按实际合并源码更新指纹。业务源文件自动合并，Linux工作流、构建脚本与version.inc保持发布分支原值。
- 本地最小构建：主目录 `build/arm64`，macOS arm64 Release，MeshLib OFF；构建`libslic3r_tests`验证合并后的报告几何逻辑。不能把此构建当Linux/MeshLib ON包运行通过。来源分支Mac ON/44 CLI及报告开关性能证据见[修复审核](2026-10-28-local-repair-adoption.md)。
- 本地构建退出0，`[ModelStageReport]~[slow]` 的6用例/121断言全部通过；这是主目录合并版本的新执行结果，未使用另一目录的旧程序替代。模块全量、暂存、diff与翻译格式检查通过后整理中文合并提交。随后按用户授权推送发布分支与 `manual-build-linux-20261029-01` 标签。
- 本轮证据放在 `build/diagnostics/model-repair-release-20261029/`，包括build.log、native.log、模块/翻译检查日志、源文件身份及后续GitHub运行记录。推送前Actions没有运行或排队的任务，不会因工作流并发策略取消既有打包。

## 打包与验证边界

- 标签规则：`manual-build-linux-*`触发`.github/workflows/build-linux.yml`，Ubuntu 24.04 x86_64，MeshLib ON；版本保持 `2.3.2.3-Release`，具体包身份以新标签/提交和产物摘要识别。
- 既有工作流仍执行包内MeshLib核验、普通固定位置切片、分盘、局部层高/进度/精简导出回归，成功后上传AppImage并创建对应标签Release。工作流未新增业务参数或自动部署服务。
- GUI没有新报告入口，未做交互验证；服务联调/实际打印不在此次Git合并与构建范围。GitHub构建状态及产物不能用本地静态检查代替。
