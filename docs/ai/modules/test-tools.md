# 测试基础设施与维护工具

<!-- module-review
{
  "id": "test-tools",
  "source_fingerprint": "sha256:5ca9dac91e3848f7dbc1f9f264f6f673669e342617316291643cf9a3008e74d6",
  "reason": "核对日志接收器恢复原日志级别、成功/拒绝/异常的计时覆盖、查询预算/面数上限后仍走回退且原网格不变；修正 Boost 布尔文本的断言后重建复测，不修改既有修复验收预期。"
}
-->

## 职责与边界

本模块负责测试基础设施与维护工具。归属以[模块清单](../modules.json)的 `test-tools` 为准。

## 入口与调用

- [CMakeLists.txt](../../../tests/CMakeLists.txt)：注册 Catch2 测试子目录和目标；SLA 目录存在不等于在此启用。
- [check_ai_module_docs.py](../../../scripts/check_ai_module_docs.py)：模块清单与 Git 工作区/暂存/提交快照检查。
- [test_check_ai_module_docs.py](../../../scripts/tests/test_check_ai_module_docs.py)：临时 Git 仓库的文档工具回归。

## 核心流程

Catch2 公共配置连接 libslic3r_tests、fff_print_tests、slic3rutils 等目标；Python／CLI 工具组织隔离输入、输出与结果审计。模块文档检查独立读取 Git 快照，不调用切片、网络或 CodeGraph。

现有 C++ 测试源统一归本模块维护基础设施；各业务模块在清单 tests 中依赖相关用例，所以修改用例也会触发对应业务文档复核。scripts 平铺目录逐文件登记，新脚本不会自动归类。

模型修复 `[ModelRepair][Diagnostics]` 使用作用域日志接收器，恢复原日志级别，不改生产接口或注入测试策略。验证阶段耗时为非负、同一次调用 ID 一致及唯一汇总，并覆盖成功/拒绝/异常和资源超限原因；超限样例还必须证明后续分支继续且原网格未被失败候选修改。运行记录与 GUI 手测见[必要优化记录](../validation/2026-10-04-smart-print-engine.md)。

## 配置与约束

测试源码、测试程序、应用与资源配置四者身份应匹配，聚焦结果包含于完整结果时不重复计数。docs/ai/modules.json 是范围清单，新增文件必须分配模块或说明排除原因。

## 失败与取消

0=文档检查通过，1=需复核/缺失/未归属，2=工具或清单错误。脚本不生成正文、不自动记录审核、不暂存、不提交；日期和 mtime 不参与指纹；旧 blob 早于当前换行属性时，字节相同保持原身份，修改后按 Git 当前规范化规则计算。

## 修改与验证

修改文档工具时，在临时 Git 仓库测试部分暂存、分支切换、文件增删改名、坏映射、共享参数依赖及快照缓存隔离。路径匹配优化必须与原通配规则等价；不能借缓存跳过内容哈希。AI 负责测试目标的必要构建和自动化／CLI 测试；业务测试执行前按 [验证规范](../validation.md) 核对或补齐当前构建，不能运行旧程序声称新代码通过。

现有测试/工具范围：`scripts/tests/test_check_ai_module_docs.py`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[构建、打包与平台集成](build.md)、[应用入口与 CLI](cli.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
