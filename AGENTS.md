# 仓库协作指南

## 项目结构与模块组织
OrcaSlicer 的 C++17 源码位于 `src/`，按功能模块和平台适配层拆分。用户资源、图标、打印机预设位于 `resources/`；翻译文件位于 `localization/`。测试位于 `tests/`，按领域分组，例如 `libslic3r/`、`sla_print/` 等，测试夹具放在 `tests/data/`。CMake 辅助文件位于 `cmake/`，较长的参考文档位于 `doc/` 和 `SoftFever_doc/`。自动化脚本应放在 `scripts/` 和 `tools/`。`deps/` 与 `deps_src/` 中的内容视为第三方快照，除非同步对应上游 tag，否则不要直接修改。

## 构建、测试与开发命令
使用 out-of-source 构建：
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`：配置依赖并生成构建文件。
- `cmake --build build --target OrcaSlicer --config Release`：编译应用；可加 `--parallel` 加速。
- `cmake --build build --target tests` 后执行 `ctest --test-dir build --output-on-failure`：运行自动化测试。

平台辅助脚本如 `build_linux.sh`、`build_release_macos.sh`、`build_release_vs2022.bat` 会封装同一套流程并附带工具链参数。复现 macOS 构建问题时优先使用 `build_release_macos.sh -sx`，需要可复现的容器构建时使用 `scripts/DockerBuild.sh`。

## 代码风格与命名约定
`.clang-format` 约定 4 空格缩进、140 列限制、对齐初始化器，并对类和函数使用花括号换行。提交前对改动文件执行 `clang-format -i <file>`；当 LLVM 工具位于 PATH 中时，也可以使用 CMake 的 `clang-format` target。类名优先使用 `CamelCase`，函数和局部变量使用 `snake_case`，常量使用 `SCREAMING_CASE`，并与 `src/` 中既有风格保持一致。头文件应自包含，include 顺序需与 IWYU pragma 保持一致。

## 测试指南
单元测试基于 Catch2（`tests/catch2/`）。测试文件按被测组件命名，例如 `tests/libslic3r/TestPlanarHole.cpp`；长耗时用例应添加标签，保证 `ctest -L fast` 仍然可用。新算法应配套确定性的 fixture 或样例 G-code，放在 `tests/data/`。当自动化覆盖不足时，在 PR 中说明手动打印机验证或切片回归检查过程。

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
