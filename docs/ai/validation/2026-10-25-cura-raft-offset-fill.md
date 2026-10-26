# CuraV1 偏心薄片垫层路径修复

## 当前执行状态

- 目标：修复 `cura_v1` 垫层区域偏离对象原点时，中间层／顶部 ZigZag 路径缺失；不改首层高度、薄片采样规则、气隙和空层检查。
- 工作目录：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master`，分支 `master`，基线 `0d70bb1d568a183e2955b2606ed39ab1de6906b5`。
- 原有未提交修改：`2026-10-19-cli-fixed-position-slicing.md`，本任务保留不动。
- 当前步骤：代码修复、构建、自动化、CLI 及提交前审核完成；按用户授权提交至 master，不推送。GUI 手测待用户检查。
- 构建：本工作目录 `build/arm64`、Ninja Multi-Config / arm64 / Release；修复前后测试、应用构建均退出 0。应用 SHA256 `189ab1d3a56dbd26ee14805db8b850a31ec828a3df93c2e39bdb7c7638cda6ea`，源文件与测试身份存于 `planning/debug/cura-raft-offset-20261025/identity.json`。
- 工具：CodeGraph MCP 未暴露，CLI 已安装，但本工作目录没有索引；依模块文档及源码定位，不初始化／复制索引。
- 结果：筏层专项 89 个用例／50,203 个断言通过（包含新用例 16 组参数／960 个断言），真实模型 8 组 CLI 对照全部退出 0、无告警；文档检查 35 模块零问题，差异空白检查通过。
- 下一步：按文末步骤在新应用中重新切片并检查预览。Linux／Windows 构建和实机打印未验证。

## 关键证据与范围

线上任务 `1563620823055826944`（右上角）与 `1563616726365863936`（左上角）的本地复现配置为 `raft_mode=cura_v1`、底／中间／表面层数 1／2／2、首层 0.24 mm、气隙 0.22 mm，分块关闭。模型包含 0.10 与 0.25 mm 薄片；0.10 mm 被首层采样忽略后，实际垫层区域不再位于对象中心。

当前 master 的 `SupportCommon.cpp` 将实际垫层的偏心包围盒交给 ZigZag；该填充器扩展包围盒时假设原点中心，可能遗漏旋转后的轮廓。`RaftMerge.cpp` 已采用局部中心坐标，但未分块、未合并的单对象不经过该路径。修复应涵盖无墙、带墙及最后接触层，不通过关闭告警或改变模板数值绕过。

GUI 参数面板与 CLI 加载工艺均经共享 `Print::process`、支撑生成和 `SupportCommon`；本次不改界面、协议或默认值。实际打印质量、其他平台构建与 GUI 交互结果另列，不能以 CLI 通过代替。

## 步骤记录

| 步骤 | 结果／证据 | 状态 |
| --- | --- | --- |
| 核对 master 与公共生成链 | 既有合并路径居中；普通路径及最后接触层未居中 | 已确认 |
| 修复前回归 | `planning/debug/cura-raft-offset-20261025/test-before.log`：1 个用例、16 组参数，8 处失败；实际两任务 `before-<SN>/summary.json` 都退出 156 | 已确认 |
| 修复 | 在公共支撑填充辅助函数内，仅对偏心 ZigZag 使用原包围盒中心作为共同原点；填充后恢复包围盒并平移路径。已居中的合并路径、Rectilinear 及模型填充不变 | 已验证 |
| 修复后构建 | `cmake --build build/arm64 --config Release --target fff_print_tests OrcaSlicer --parallel 2`；日志 `planning/debug/cura-raft-offset-20261025/build-after.log` | 退出 0 |
| 聚焦回归 | `fff_print_tests '[OffsetFill]' --reporter compact`；`test-after.log` | 1 用例／16 组参数／960 断言通过 |
| 完整筏层专项 | `fff_print_tests '[SupportMaterial][Raft]' --reporter compact`；`test-raft-suite.log` | 89 用例／50,203 断言通过；不与聚焦测试重复计数 |
| CLI 对照 | 原左右任务、右侧首层 0.1、关闭垫层、左右分块、右侧普通支撑、右侧带墙；`after/<case>/command.json`、`summary.json`、`output/` | 8 组退出 0、无告警、有 G-code；未使用 `--no-check` |
| 文档与差异 | `docs-final.log`、`git diff --check` | 35 模块零问题；无空白错误 |

## 实际路径结果

原左右任务在未修改模板参数时，修复前均退出 156，报告 1.12～1.78 mm 空层；修复后均退出 0。
右侧原配置各层正挤出移动数如下（不是估算层数）：

| 打印 Z / mm | 角色 | 挤出移动数 |
| --- | --- | --- |
| 0.32 | 垫层底层 | 61 |
| 0.62 | 垫层中间层 | 55 |
| 0.92 | 垫层中间层 | 58 |
| 1.12 | 垫层表面层 | 102 |
| 1.32 | 垫层表面层 | 101 |
| 1.78 | 模型首层 | 184 |

证据 `after/path-counts.json` 按左下／右上位置统计模型移动，不把不同模型位置统一标成同一厚度。
右侧 0.24 mm 首层仍只生成 0.25 mm 薄片，0.10 mm 薄片没有模型路径；将首层改成 0.1 mm 后两片均有路径（170／272 次）。
这是原采样规则，不能把本次垫层修复描述成薄片问题全部解决。

## 提交前审核与影响

- 调用范围：新增转换位于 SupportCommon 私有填充辅助函数，条件为 `pattern == ipZigZag` 且包围盒有效、中心非零。当前生产调用中该 pattern 来自 CuraV1 中间／表面阶段；普通支撑和 legacy 仍使用原 pattern，模型填充不调用此辅助函数。
- 路径属性：只平移轮廓、填充器包围盒和输出折线；流量、线宽、厚度、角色与外墙生成保持原值。已居中的 RaftMerge 不重复移动，既有床面／禁区裁剪和空层校验保持。
- 并发／异常：生成端为每个并行层／分段创建独立填充器；正常返回及既有 `InfillFailedException` 分支均恢复包围盒。其他异常仍向上传播并结束当前生成，不增加吞异常或共享可变状态。
- 回归证据：重新核对源码、测试程序和应用 SHA256 与运行记录一致；89 个筏层用例含合并、分块、普通支撑保留、取消恢复及床面裁剪；8 组 CLI 同参对照通过。审核期间无源码修改，不重复运行相同测试。
- 结论：未发现阻断提交的问题。仅提交本任务源码、测试、5 份模块说明和本记录；原有 `2026-10-19-cli-fixed-position-slicing.md` 修改不纳入。GUI／其他平台／实机验证范围仍按下文保持待验。

## GUI 验证交接

当前 GUI 手测未执行；下列工程已由本次新程序导出并检查归档中的有效配置，用户按“项目”打开以加载机器、耗材和工艺。

新应用：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。
资源链接到本 master 工作目录 `resources`；不要使用另一工作目录或尚未更新的打包副本证明本修复。

本机复现工程（各一个对象／一盘、模型缩放 1、旋转 0、中心 X/Y=50/50，原双薄片尺寸及朝向保留）：

- 右上角：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/planning/debug/cura-raft-offset-20261025/gui/right/raft-offset-right.3mf`
- 左上角：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/planning/debug/cura-raft-offset-20261025/gui/left/raft-offset-left.3mf`

有效配置来自真实任务 EPONEPro 标准档，喷嘴 0.4 mm、模型首层 0.24 mm、后续层高 0.2 mm；
工艺 `EPONE Pro-PLA 无支撑 15% 1022`；不另套用户预设、不新增对象覆盖。`cura_v1` 1／2／2 层、
分块关闭、气隙 0.22 mm；模型首层 Z=1.78 mm。工程 machine 内部名称可能与业务机型显示名不同，以工程值为准。

| 场景 | 具体操作 | 预期结果 | GUI 状态 |
| --- | --- | --- | --- |
| 原配置 | 用新应用打开右／左工程，选“准备”，确认工艺的筏层模式与上述值，重新“切片单盘”；必要时改首层为 0.23 后再恢复 0.24 以使旧缓存失效 | 无原 156 空层告警；预览 Z=0.32、0.62、0.92、1.12、1.32 均有垫层路径，模型首层仍为 1.78 | 待验 |
| 分块重切 | 同工程启用筏层分块、块尺寸 15 mm，重新切片；再关闭并重新切片 | 分块后每层有有效路径，关闭恢复未分块布局；没有空层告警 | 待验 |
| 薄片边界 | 右工程关闭分块，首层改 0.1 mm，重新切片 | 两片均有模型路径；垫层仍有五层，模型首层 Z=1.64 mm | 待验 |

请回传导出的项目或 G-code.3mf，以及 0.92／1.32 mm 层预览。对应 CLI 文件已保存在 `after/`，
软件路径通过不代表粘附、气隙或实际打印质量已验收。
