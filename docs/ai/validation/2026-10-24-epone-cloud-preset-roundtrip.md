# EPONE 云端工艺导入参数丢失修复

## 当前执行状态

- 目标：在 master 修复云端导入差量工艺后线宽归零、树状支撑变普通的问题，完成检查后提交并推送。用户已明确授权本次提交和推送。
- 基准：master，`4a674a7114159769284dec045bfbcfdb75764ba0`；工作目录 `orca-slicer-master`。已 fetch 确认与 `codeup/master` 相同。
- 原有未提交修改：`2026-10-19-cli-fixed-position-slicing.md`，保持原样并排除提交；原分析分支工作目录不切换、不清理、不提交。
- 本次修改：导入工艺按 `Preset::print_options()` 优先读取工程有效配置，缺项才取嵌入预设；新增无 GUI 依赖的合成函数及工艺保存/重载回归。机器、耗材分支、上传格式、云协议和资源默认值不变。
- 当前状态：master 代码修复与自动化验证完成，回归 27 用例/1922 断言全部通过，CLI --help 退出 0；本记录随修复提交，目标远端 codeup/master，实际提交号/推送结果见 Git 和任务回执。此前分析分支的测试结果不作为本次 master 验证结果。
- CodeGraph：会话未暴露 MCP；已安装 CLI 位于 `~/.local/bin/codegraph`，本工作目录无 `.codegraph` 索引，按模块文档和源码定位，未初始化或复制其他目录索引。
- 验证：应用/测试构建和 CLI --help 已通过。首次回归发现测试夹具临时目录未初始化，修正后已重建复测通过（过程见下文）。GUI 真实云端往返和 Windows 验证保留待验。

## 原因与修复边界

用户附件 `71a5bd6b-719d-42b7-bdcb-6bb450f30ffd.3mf`，SHA256 `69aaea52e05f70b02c03285b86fb2938fa9a1e0a42bd11bf9fb45a6d3e22a1eb`。
其中 `project_settings.config` 保存完整有效配置，所选 `process_settings_1.config` 只有 36 个 JSON 字段，继承 `0.20mm Standard @EPONE`；仅外墙线宽显式为 0.45，其他七项线宽和支撑类型没有显式保存。

原导入优先取嵌入工艺 config，没有展开继承，也没有优先取工程快照；新建工艺缺项落到通用默认值，替换工艺缺项可能保留本地旧值。修复后新建、替换、另存使用同一有效工艺：工程值优先，只有缺键才从嵌入工艺补充，合法的 0/百分比保持原值。继续保留现有本地名称、机型绑定和取消流程。

这项修复兼容已有差量/全量包。历史导出代码可以从工程预设取得差量，也可以克隆当前完整预设，同名优先保留前者；现有证据不足以证明各样例上传当时的具体状态，不将这种条件分析写成已确认的上传操作历史。

GUI 入口是 `Plater::priv::gfd_apply_prepared_cloud_config`；共享工艺合成为 `GFD::cloud_process_config`，持久化继续使用 `PresetCollection`。CLI 没有云方案导入管理命令，普通 CLI 预设加载和切片路径未改；CLI 启动检查不代替云业务验收。

## 验证记录

构建目录：本工作目录 `build/arm64`，Ninja Multi-Config / Release / arm64，SDK MacOSX26.5，最低系统 14.0，MeshLib OFF；依赖前缀沿用已配置的原仓库静态依赖。开始前未发现并行 CMake/Ninja/Clang 构建。

已执行 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 6`，退出 0；日志 `planning/epone-cloud-roundtrip/build.log`，后续增量检查见 `build-final.log`，夹具修正后重建见 `build-test-fixture.log`。已运行 `[EPONE],[3mf]`；检查完整/差量来源、工程显式值优先、旧包回退、隔离临时用户目录中的新建/替换/另存与重载。

## GUI 验证清单（待执行）

使用本工作目录 `build/arm64/src/Release/OrcaSlicer.app` 的本次构建，不能用其他分支或旧打包副本代替。

1. 在 EPONE 0.4 喷嘴下导入附件对应的“树状支撑、常规生成、15%填充”云方案，手动切到导入工艺（现有导入行为会恢复导入前选择）。八项线宽应依次为 0.42、0.5、0.45、0.42、0.45、0.42、0.42、0.4；首层高度 0.24，支撑类型树状(自动)，填充 15%。
2. 将该本地工艺改为其他线宽和普通支撑，再导入同方案选择“替换”，应恢复上述数值；重复导入选择“另存为”也应一致。
3. 保存并重启应用、重新选中该工艺，核对数值；保存项目并重新打开后核对相同参数。取消替换应保留原本地参数。
4. 导入原本正常的 EPONE Plus/Pro 全量方案，应保持云包原值；显式线宽 0 或百分比也应保留。中英文界面均无新增文案，本轮未改机型资源。

回传本次应用路径、操作时间、质量/支撑截图及重新保存的 3MF；日志不包含登录凭据。真实上传/导入端到端与 Windows 需单独验证，不能以单元测试代替。

### 首次回归与测试夹具修正

应用和测试构建退出 0；最终增量检查仅执行两个编码检查。`[EPONE],[3mf]` 首次退出 2，27 用例中 26 通过、1 个新增归档用例失败（2 个差量/全量分支断言）；原因是 Model::read_from_archive 内部新建 Model 使用未初始化的全局 temporary_dir，尝试写 `/orcaslicer_model` 而失败。没有放宽断言，改用现有 ModelBackupPathGuard 给 BBS 解析器提供隔离临时目录，沿用云导入的 LoadConfig/CheckVersion 标志。此为测试夹具修正，业务代码未改；初次日志保留在 `regression-tests.log`，复测另存。

### 最终自动化结果与身份

- 夹具修正后重建仅包含 test_3mf 对象和测试程序链接，应用源码/程序未变化；构建退出 0。
- `libslic3r_tests '[EPONE],[3mf]'`：27 用例、1922 断言全部通过，退出 0；日志 `planning/epone-cloud-roundtrip/regression-tests-final.log`。包括实际 BBS 3MF 解析差量/全量、显式零/百分比、缺项回退及临时用户预设新建/替换/另存重载。
- 当前应用 `--help` 退出 0，日志 `cli-help.log`；CLI 无云管理命令，此项只证明启动。
- `python3 scripts/check_ai_module_docs.py --all`：35 模块、0 问题；差异检查通过。选择性暂存后 `--staged` 模块检查及 `git diff --cached --check` 均通过；原有无关文档、运行日志和 result.json 未纳入提交。
- 构建前核对源码/资源身份，测试前再次验证；完整 SHA256 记录在 `planning/epone-cloud-roundtrip/identity.json`。
- build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer: `3915c50fa7c35777d4d46ad27415e2a72d26433d41de71021732ea4297ae5528`。
- build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests: `d2c668d99d8c10d98853bd9c04b18490d49f2570be6b790430add9256717fbd4`。
- src/slic3r/GUI/Plater.cpp: `6ddc0c05fb81f1bae2c75dd79324fe55a99ff5c6efaf5db834a893e1d210e793`。
- src/slic3r/Utils/GFDCloudPreset.hpp: `5e774b9810e6ded63966ce7d9a044132ca9ebefdc059d6c707034cbfe14f3432`。
- tests/libslic3r/test_epone_presets.cpp: `3a25f9eb82676891182118a2e2deb69534120dd168d226c1191e61a5775bb33d`。
- tests/libslic3r/test_3mf.cpp: `788338ef95a9d2b4708244406d9daa7e47243db51d4436881533832f8558801e`。
- 未验证：真实云端上传/导入交互、GUI替换/取消/重启和 Windows 构建；GUI 操作步骤见上文。自动化不代替这些验收。
