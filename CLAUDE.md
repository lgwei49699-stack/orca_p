# Claude Code 协作入口

本仓库共享规则以 [AGENTS.md](AGENTS.md) 为准。先确认当前分支，阅读 [模块索引](docs/ai/README.md) 和任务涉及的模块，再定位源码。

提交前更新受影响模块并运行文档的 `--all`、`--staged` 检查。应用通常由用户构建；AI 负责测试目标与测试，缺少当前验证所需构建时按长期约定补齐。GUI／CLI 同步要求及构建身份按 [验证规范](docs/ai/validation.md)，GUI 手测按 [交接规范](docs/ai/gui-validation.md)。没有 CodeGraph MCP/索引不等于必须初始化，按 [降级规则](docs/ai/tooling.md) 继续。

旧版 CLAUDE 的长篇构建/架构说明保存在 [历史原文](docs/ai/history/claude-before-module-docs.md)，其中旧数量和平台说明不作为当前事实。当前分工以 AGENTS 和验证规范为准，旧编译限制不再适用。
