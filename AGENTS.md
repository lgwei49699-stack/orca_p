# 仓库协作指南

## 项目结构与模块组织
OrcaSlicer 的 C++17 源码位于 `src/`，按功能模块和平台适配层拆分。用户资源、图标、打印机预设位于 `resources/`；翻译文件位于 `localization/`。测试位于 `tests/`，按领域分组，例如 `libslic3r/`、`sla_print/` 等，测试夹具放在 `tests/data/`。CMake 辅助文件位于 `cmake/`，较长的参考文档位于 `doc/` 和 `SoftFever_doc/`。自动化脚本应放在 `scripts/` 和 `tools/`。`deps/` 与 `deps_src/` 中的内容视为第三方快照，除非同步对应上游 tag，否则不要直接修改。

## 构建、测试与开发命令

### AI 编译与验证约束

- 默认不主动执行编译、构建或打包，包括全量编译、局部编译、测试目标编译，以及会触发编译的脚本。用户会自行编译、验证并提供日志。
- 只有用户明确要求编译、构建或打包时，才执行对应命令；修改代码、添加日志或分析问题不视为编译授权。
- 默认通过源码审阅、`git diff --check` 等不触发编译的静态检查验证改动，并在交付时明确说明未编译。若必须依赖编译结果才能继续，先说明原因并询问用户。
- 本约束优先于本文其他构建验证建议；下述命令仅供参考，不构成执行授权。

使用 out-of-source 构建：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`：配置依赖并生成构建文件。
- `cmake --build build --target OrcaSlicer --config Release`：编译应用；可加 `--parallel` 加速。
- `cmake --build build --target tests` 后执行 `ctest --test-dir build --output-on-failure`：运行自动化测试。

平台辅助脚本如 `build_linux.sh`、`build_release_macos.sh`、`build_release_vs2022.bat` 会封装同一套流程并附带工具链参数。复现 macOS 构建问题时优先使用 `build_release_macos.sh -sx`，需要可复现的容器构建时使用 `scripts/DockerBuild.sh`。

## 界面语言与翻译范围

- 产品主要面向简体中文和英文用户。新增或修改界面文案时，默认完整覆盖英文原文与简体中文翻译。
- 沿用项目现有国际化机制：代码中使用英文原文及翻译标记，并同步维护 `localization/i18n/zh_CN/OrcaSlicer_zh_CN.po`；不要直接硬编码中文界面文案。
- 其他语言缺少对应翻译时，允许回退显示英文，不作为默认入库阻塞项。除非用户明确要求多语言适配，否则不主动批量补译其他语言的 `.po` 文件。
- 保留现有其他语言翻译，不删除或覆盖无关条目；中英文验证应关注菜单、提示、错误信息及占位符的一致性。

## 代码风格与命名约定
`.clang-format` 约定 4 空格缩进、140 列限制、对齐初始化器，并对类和函数使用花括号换行。提交前对改动文件执行 `clang-format -i <file>`；当 LLVM 工具位于 PATH 中时，也可以使用 CMake 的 `clang-format` target。类名优先使用 `CamelCase`，函数和局部变量使用 `snake_case`，常量使用 `SCREAMING_CASE`，并与 `src/` 中既有风格保持一致。头文件应自包含，include 顺序需与 IWYU pragma 保持一致。

## 测试指南
单元测试基于 Catch2（`tests/catch2/`）。测试文件按被测组件命名，例如 `tests/libslic3r/TestPlanarHole.cpp`；长耗时用例应添加标签，保证 `ctest -L fast` 仍然可用。新算法应配套确定性的 fixture 或样例 G-code，放在 `tests/data/`。当自动化覆盖不足时，在 PR 中说明手动打印机验证或切片回归检查过程。

### 模型修复专项测试：固定命令入口

- 基础修复验收夹具在 `tests/data/model_repair_acceptance/`，由 `scripts/generate_repair_fixtures.py` 确定性生成；`manifest.json` 记录预期结果，不能当成测试已经通过。只生成夹具不会构建或运行应用。
- 新增聚焦标签为 `'[ModelRepair][Acceptance]'`，仍包含于下面的完整专项；涵盖修复后 50 层截面与健康模型对比、危险模型失败不改原件、薄实体保留、孤立涂色面的严格保色失败保护。截面测试不等同完整 G-code 切片验证。
- CLI 几何/导出验收固定入口：`python3 scripts/test_cli_repair_acceptance.py <新构建的 OrcaSlicer 路径>`。脚本创建独立临时目录，保留原始输入哈希、命令、日志、结果 JSON，检查完全修复的输出重导入无需再次修复；不编译。部分修复输出另行断言仍有未解决项，不能套用完全修复的闭合/重导入通过标准。
- 验收脚本还直接检查 OBJ 文件索引边的闭合和朝向，不通过坐标焊接掩盖色区接缝；这不替代 CGAL 自交/非流形顶点检查。单平面负例必须在修复阶段失败且不提交，不能以导入或导出失败冒充修复保护通过。
- 验收工具自身的无编译测试：`python3 scripts/test_repair_acceptance_helpers.py -v`，覆盖夹具确定性、短 STL 探测长度、色区断边、独立壳体和错误索引。它不代表 C++ 改动已通过。
- 彩色 OBJ 共享索引和涂色 T 形接缝的聚焦用例为 `'[OBJ][Topology]'`；模型清理不得删空一个对象，但同一对象仍有实体时可删除平面杂片。两者已纳入固定完整专项，无需修改构建页面测试菜单。
- 实际切片验收在同一脚本后加 `--load-settings '<打印机JSON绝对路径>;<工艺JSON绝对路径>' --load-filaments '<耗材1JSON绝对路径>;<耗材2JSON绝对路径>'`，必须使用服务器实际解析完成的参数，双色 OBJ 须配置两个耗材槽。不提供参数时脚本明确标记切片未验证；0.001 mm 薄实体只验证几何保留，不能承诺当前层高可打印。
- `--repair-model cgal --slice ...` 默认也严格保留支持的 OBJ 面颜色；不得搭配 `--no-check`、`--allow-newer-file` 或 `--load-slicedata` 跳过检查/复用旧切片缓存。未请求修复的旧流程不受本限制影响。
- 新增 `result.json.repair_validation` 区分网格检查与切片状态。`mesh=passed` 不代表已切片；`slicing=completed` 也不等于无警告或所有打印条件都满足，须同时检查任务返回码、盘数、`sliced_plates[].warning_message`、非空 G-code 和逐层预览。没有自动发往打印机的步骤。
- 2026-09-10 部分修复策略：GUI 显式“修复模型”和 CLI `--repair-model cgal` 启用 `allow_partial`；核心 API 默认及切割工具内部仍严格。可保留独立基础清理（精确零面积面、同向重复面）及其他部件的有效修改，不采用 CGAL 检查失败的候选。非法数据、内存/工作线程异常、严格保色失败及取消仍中止当前事务。
- 部分修复必须报告 `partially_repaired`、`unresolved` 计数、`mesh_validated=false` 和 `repair_validation.mesh=not_passed`；不能把 `success=true`、`failed=0` 或 CLI 退出码 0 当作网格完全合格。仅有未解决项、没有可保留修改时为 `unresolved`：仅导出任务非零退出；请求切片时允许尝试，但切片警告/冲突/导出门禁不放宽。
- 新增聚焦标签 `'[ModelRepair][Partial]'` 已包含于完整专项。CLI 验收追加同一网格“重复面+非流形边”、部分输出再次检查及两种输入顺序的批处理用例；这些仍是合成夹具，不能冒充真实 Thingi10K 验证。真实样本与本轮进展见 `planning/repair/repair-test-models/mesh-inspector/REPAIR_PROGRESS.md`，未重新编译运行前不能复用旧日志作为新策略通过证明。
- 修复后切片在 G-code 导出前后都检查警告；CRITICAL 警告或冲突令任务失败，并尝试移除本次刚生成的拒绝 G-code。删除失败会记录日志，调用方仍必须按非零返回码拒绝使用所有产物。普通不修复切片的原有警告策略不变。
- 由用户执行编译和验证并提供日志。AI 修改代码、测试或本文档，不代表获得编译或运行测试的授权；默认仅做静态检查。
- 不需要为每次修复修改构建页面的测试菜单。页面的 CTest 标签下拉框与 Catch2 标签不是一回事；不得直接把 `[ModelRepair]` 等当成 `ctest -L` 的标签。
- 固定编译目标为 `libslic3r_tests`。只编译 OrcaSlicer GUI 不会更新测试程序；编译失败时必须停止，不得继续运行旧二进制。
- 完整修复专项过滤器为 `'[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]'`，逗号表示匹配任一标签。新增修复/涂色用例应沿用这些标签，避免每次修改入口命令。
- 本轮缺陷聚焦过滤器可用 `'[ModelRepair][Scale],[SavedPainting][Provenance]'`，相邻方括号表示同时满足两个标签；它不替代完整专项回归。
- 顶点拆分候选修复的聚焦过滤器为 `'[ModelRepair][SoupOrientation]'`，已包含于完整专项；拆分后仍相交的候选直接报告不支持安全自动修复，不进入新增实验性自交修复，检查失败后网格/涂色/所有权不变及正常朝向修复。它不替代真实 OBJ/MTL 的 GUI/CLI 修复验收，也不能使用 `[OBJ][GUIColor]` 通过结果作为其验证证明。
- 主要用例文件：`tests/libslic3r/test_model_repair.cpp`、`test_meshboolean.cpp`、`test_saved_painting.cpp`、`test_obj.cpp`、`test_stl.cpp`。专项通过不等于 GUI、Undo/Redo、跨平台或完整 CTest 已通过。

#### macOS arm64 / Release（当前构建页面对应目录）

在仓库根目录执行下面整段命令。显式使用 Bash，使 macOS 默认 zsh 下也能正确读取 `PIPESTATUS`，并用子进程退出避免关闭用户终端。构建日志写入 `planning/build/`，测试日志写入 `planning/repair/`。

```bash
bash <<'BASH'
set -e -o pipefail
mkdir -p planning/build planning/repair
repair_stamp=$(date +%Y%m%d-%H%M%S)
repair_build_log="planning/build/model-repair-build-${repair_stamp}.log"
repair_test_log="planning/repair/model-repair-test-${repair_stamp}.log"
printf 'Build log: %s\nTest log: %s\n' "$repair_build_log" "$repair_test_log"

cmake --build build/arm64 --config Release --target libslic3r_tests --parallel 2 2>&1 \
  | tee "$repair_build_log"

set +e
./build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests \
  '[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]' \
  --reporter compact 2>&1 | tee "$repair_test_log"
repair_codes=("${PIPESTATUS[@]}")
printf 'Test exit code: %s; log writer exit code: %s\n' "${repair_codes[0]}" "${repair_codes[1]}" \
  | tee -a "$repair_test_log"
if (( repair_codes[0] != 0 )); then exit "${repair_codes[0]}"; fi
exit "${repair_codes[1]}"
BASH
```

前提是该目录已配置 `BUILD_TESTS=ON` 并生成 `libslic3r_tests` 目标。缺少目标时先检查既有 CMake 配置和平台依赖，不要擅自切换生成器或另建不同依赖配置。此处提供命令不构成 AI 自动执行授权。

#### 不同构建目录不能混用

| 已配置构建目录 | 编译命令 | 测试程序路径（相对仓库根目录） |
|---|---|---|
| `build/arm64`，Ninja Multi-Config | `cmake --build build/arm64 --config Release --target libslic3r_tests --parallel 2` | `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` |
| 历史 `build`，Unix Makefiles | `cmake --build build --target libslic3r_tests --parallel 2` | `build/tests/libslic3r/libslic3r_tests` |

以上路径来自本机现有生成配置；其他平台或生成器应先核对实际输出位置。Windows/Linux 沿用相同目标和 Catch2 过滤器，不照搬 macOS 的 `.app` 路径。不要运行旧 `repair_review_probe` 或复用历史 81/798 等通过数字来证明新补丁已验证。

#### 反馈与结果判定

- 基础补洞颜色规则：可映射部分保留原色，无映射部分使用所属部件默认色（OBJ exporter 的 state 0 → base_filament）；不创建新色，不推断多色洞口图案。`[ModelRepair][CGAL][Painting][HoleColor]` 用例已纳入既有专项过滤器。用户已确认不需要 UV 贴图/完整渲染材质往返，不将其列为本次基础功能门禁。

- 常用 OBJ 修复命令为 `--repair-model cgal --export-obj`：严格保留支持的 MTL 面颜色，有颜色自动输出 OBJ+MTL，无颜色输出普通 OBJ。映射失败报错；原贴图/顶点色往返不在此能力内。仅需几何时加 `--repair-ignore-materials`。显式多色参数保留兼容；无修复参数的 `--export-obj` 保持旧纯几何语义。CLI 冒烟脚本已追加自动选择、带色补洞及旧命令兼容用例，修改后须重新构建验证。

- CLI OBJ/MTL 专项新增用例使用 `[OBJ][CLI][Import]`，已包含于完整修复过滤器；无需改构建页面菜单。
- 2026-09-13 缺少 MTL：GUI/CLI 共享 OBJ 导入层在引用的库缺失、或 `usemtl` 没有库声明时自动忽略源颜色/UV并继续几何导入；多个库部分缺失按整个模型降级。CLI 的 `input_materials_ignored` 包括该自动降级；显式耗材映射仍保留用户指定的配色。存在但损坏的 MTL、严格保色时未定义的材质和非法几何仍拒绝。新增 `[OBJ][MissingMTL]` 和 CLI 材质冒烟用例，需要新构建验证，不复用旧日志。
- GUI 导入清理只移除不含三角面的空对象，不按带符号体积删除非空网格；开口、朝向抵消、平面和极薄模型保留到显式修复阶段判定，不代表这些模型可打印。零/负/非有限体积不参与体积单位猜测及其选择性缩放；用户明确指定的整模型单位转换仍生效。新增 `[ModelRepair][Import]`，并在缺少 MTL 的 GUI/CLI 对照用例中执行 GUI 清理步骤；均包含于固定完整专项，需要新构建及 GUI 复测。
- CLI 材质冒烟脚本显式使用 `--debug 3` 验证缺少 MTL 的警告。导入负例检查 JSON 的解析失败码 `-6` 和日志中的具体原因，参数冲突仍检查 JSON 错误信息；不得仅凭任意非零退出码判定保护通过。脚本修正可单独验证，但不能替代本轮 C++ 改动的新构建验收。
- GUI OBJ 原色/耗材复用策略用例使用 `[OBJ][GUIColor]`（位于 `test_obj.cpp`），已包含于完整 `[OBJ]` 过滤器；聚焦验证可用 `'[OBJ][GUIColor]'`，无需改构建页面菜单。此类用例不替代实际 GUI 导入/配色/导出的验证；修改后必须重新构建，不能复用旧 GUI 的产物检查作为通过证明。
- 2026-09-12 GUI 保色回归：显式“修复模型”使用 `gui_model_repair_options()`，不再读取实验性 `keep_painting` 偏好；始终尝试保留受支持的涂色，禁止静默清空，无法映射时仍中止当前事务。切割工具、核心 API 默认及 CLI 的既有策略不变。新增 `[OBJ][GUIColor][ModelRepair]` 夹具检查同一 GUI 策略下的 staging/提交/OBJ 导出：绿色原面面积 300 mm² 保留，新增顶面使用红色默认耗材；`[ModelRepair][GUIColor][Painting][Partial]` 检查部分模式也不得清空无法映射的涂色。两者已包含于固定完整专项，未重新编译运行及复测 GUI 前不得宣称丢色问题已验收通过。
- 用户重新构建 OrcaSlicer 应用后，可执行 `python3 scripts/test_cli_repair_materials.py ./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。服务器替换为当前平台新构建的可执行文件路径。脚本不编译，每次创建独立临时证据目录并保留 JSON/日志，验证 `--repair-ignore-materials` 策略及参数冲突；它不替代 Catch2 完整专项，也不构成 AI 自动执行授权。
- `--repair-ignore-materials` 默认关闭，仅配合 `--repair-model cgal` 显式用于不需要材质/颜色/UV 的任务。纯几何输出不是保色输出；以 JSON 的 `input_materials_ignored` 和输出语义判断，不只看 `annotations_cleared`。

- 编译失败：提供本次构建日志，不继续测试旧程序。
- 测试完成：提供本次测试日志，检查最终用例/断言统计及退出码，不只看中间是否出现 `[error]`；部分负向用例预期输出错误日志。
- `No test cases matched`、程序不存在、日志写入失败或非零退出码均不能算验证通过。
- 记录实际 checkout、构建目录、配置、平台和新日志；未执行的 GUI、完整回归或其他平台验证必须明确标注。

## AI 工具使用
本仓库已初始化 CodeGraph，项目索引位于 `.codegraph/`。在 Codex 中分析代码时，优先使用 CodeGraph 的 MCP 能力进行结构化查询，再结合 `rg`、源码阅读和测试验证。

CodeGraph 适合回答“函数/类在哪里”“谁调用了它”“它调用了谁”“修改这个 symbol 会影响哪些文件”“模块依赖关系是什么”等问题。常用命令：
- `codegraph status`：查看索引状态与统计信息。
- `codegraph sync`：增量同步当前改动。
- `codegraph index`：全量重建索引。

注意：在 zsh 中如果要写注释，请单独成行；不要执行 `codegraph sync # 增量更新`，否则某些命令解析场景会把注释内容当作额外参数。推荐直接执行 `codegraph sync`。

Understand-Anything 已安装到用户级目录，并通过 skills 暴露给 Codex。它适合做人类可读的项目理解、知识图谱、可视化 dashboard、代码问答、diff 影响分析、onboarding 文档和业务域梳理。常见触发方式：
- “分析这个代码库并构建知识图谱”
- “用中文解释这个项目架构”
- “生成 onboarding guide”
- “分析当前 git 改动的影响”
- “打开/生成 Understand-Anything dashboard”

使用建议：日常定位和改代码优先使用 CodeGraph；需要全局架构梳理、知识图谱、交接文档或面向人的解释时使用 Understand-Anything。两者输出都不能替代编译、测试和源码确认；涉及行为变更时必须回到源码与测试验证。

## 提交与 Pull Request 指南
历史提交偏好简洁的句子式标题，可带 issue 编号，例如 `Fix grid lines origin for multiple plates (#10724)`。打开 PR 前先在本地 squash fixup。填写 `.github/pull_request_template.md`，UI 改动需包含复现步骤或截图，并说明影响到的预设或翻译。关联 issue 时使用 `Closes #NNNN`，依赖升级或 profile 迁移需要在 PR 中明确提醒维护者。

## 安全与配置提示
漏洞报告遵循 `SECURITY.md`。不要把 API token 或打印机凭据写入已跟踪配置；实验性设置使用 `sandboxes/`。修改 `deps_src/` 中的第三方代码时，在 PR 描述里记录对应上游 commit 或 release，并运行相关平台构建脚本确认集成无误。
