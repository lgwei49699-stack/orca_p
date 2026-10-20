# Linux 打包分支合入盘外 JSON 姿态修复

## 当前状态

- 目标分支：`linux/release2_3_2`，合并前 HEAD 为 `68e4699efa15ffeb82454a8280bdc2d6719beab3`。
- 用户已开始合并 `master/4a674a7114159769284dec045bfbcfdb75764ba0`；本次处理该进行中的合并，不新建其他合并或推送。
- 初始冲突仅涉及 `cli.md`、`arrangement.md`、`test-tools.md`，其余三个自动合并文件为 CLI 源码、专项测试和 master 验证记录；没有额外用户工作区改动。
- 已解决三份文档冲突并按当前分支更新指纹；逐行核对自动合并的 CLI／专项测试增删内容与 master 此次提交完全相同，Linux 分支既有差异保留。本目录应用构建、184 次盘外专项和 64 次固定位置／分析报告回归均通过。
- 交付为本次进行中合并的中文提交，不推送、不触发 GitHub 工作流或发布；后续 Linux x86_64 包仍按已有打包流程验证。
- 本目录无 CodeGraph 索引，使用当前分支模块文档和源码，不初始化或复制索引。

## 合并范围与边界

保留 Linux 分支已有阶段报告、切片分析、MeshLib 集成和构建缓存／打包配置。
新增的业务变化仅为 CLI 多盘 JSON 的盘外实例使用排布前姿态，位置规则仍为独立返回盘、XY 居中、Z=0。
不回写模型变换，不修改 GUI ArrangeJob、共享排布算法、实际盘归属或 3MF；正常盘内及显式单盘规则保持。
虚拟返回盘及返回成功仍不表示模型已通过可打印性检查。

三份模块文档的审核信息按最终分支源码重算，测试文档保留原有报告回归说明并追加新薄片回归说明。
master 的运行记录仅作为来源历史；本分支验证单独记录，不将其他工作目录的通过结论当成本次合并结果。

## 验证

文档全量检查已通过（35 个模块、0 问题），合并文件无冲突标记。检查器在 Git index 尚有冲突项时拒绝计算指纹；先标记人工解决的文档，再重新计算成功，未忽略该错误。
本目录已有 macOS arm64 Release、MeshLib OFF 构建，CMAKE_HOME_DIRECTORY 指向当前工作目录，未发现同时写入该目录的构建。
首轮 `cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 2` 需重建 577 步。
确认主机为 16 核／64 GiB 后，为缩短等待主动中断本任务 Ninja（退出 130），保留已完成的 97 步；该中断不是编译错误。
以 `--parallel 6` 继续同一构建目录后退出 0。日志分别为 `planning/linux-release-merge-20261020/overflow-build.log`
和 `overflow-build-parallel6.log`。使用该目录新程序运行 `test_cli_oversized_arrange.py` 和
`test_cli_fixed_position_slice.py`，独立输出目录为 `overflow-regression` 和 `overflow-fixed-regression`；
两者分别保存命令、输入和程序哈希，顶层运行日志为 `overflow-test.log`、`overflow-fixed-test.log`。
本轮构建是 Linux 分支源码在 macOS 上的验证，不冒充 Linux x86_64 包或 GitHub CI 验证。
GUI 没有新增交互，代码入口和实际几何只读边界已静态复核；本轮未进行 GUI 手测、生产部署或实机打印。

## 结果与身份

- `test_cli_oversized_arrange.py` 退出 0，184 次调用全部通过；涵盖薄片、非零输入旋转、镜像／缩放、自动定向、正常盘内、单盘、多盘及 37 个盘外实例。JSON 开关下实际 3MF 的几何、变换和盘成员保持一致。
- `test_cli_fixed_position_slice.py` 退出 0，64 个案例全部通过，包含固定位置、越界拒绝、禁用盘、多盘、导出及本分支分析报告交叉场景；预期错误码也逐例断言。
- 两份报告的程序哈希与本目录构建相同，验证后源码和测试脚本没有变化。完整身份在 `overflow-build-identity.json`。
- 源码 `src/OrcaSlicer.cpp` SHA256：`af99b187a99fcf7c21af5bc97fa74c9bf48ba6574993021b676be36920705033`。
- macOS 程序 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA256：`c291d2fdc80982056476e541104be3ed228d372ce2a28f724a016d66a10dcdd6`。
- Linux 工作流、`build_linux.sh`、MeshLib 配置和打包工具、SliceAnalysis／ModelStageReport 与 GUI ArrangeJob 源码均与合并前 HEAD 一致。
