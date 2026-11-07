# CGAL 高级修复的编译配置与回归

基准为 `linux/release2_3_2` 的 `0f7c72560f`，CGAL 6.2.1。QA 的头骨和 PolySnap 在高级修复时异常退出。此前相同 Linux 发布二进制在本地对两个输入均返回 134，调用栈经过局部自交修补、Kd_tree 与 pthread_mutex_lock。Mac 旧程序的两个输入均正常返回。

本次移除只给 MeshRepair.cpp 打开的 CGAL TBB 宏。该宏会改变共享模板 Kd_tree 的成员布局。修复目标继承其余编译单元使用的配置；自交对枚举使用该配置提供的 Parallel_if_available_tag。

复核了源码、构建脚本及 Mac Release 编译命令。MeshRepair、MeshBoolean 和 ModelInspection 使用一致的 CGAL 配置，未发现第二处单文件覆盖该宏的情况。Linux 链接另有一条来自固定依赖 oneTBB 2021.5 的 `notify_type` 枚举定义差异告警，位置在依赖的内存分配器和 profiling 头文件。上述 CGAL 修改未涉及这两个文件，本轮用例未观察到与这条告警相关的故障。

## Linux x86-64

在 Mac 上使用 Ubuntu 24.04 x86-64 容器及 GCC 13.3 重建 Release CLI。分别验证了关闭 MeshLib、启用固定 SDK 3.1.3.429 的构建，以及通过仓库打包脚本生成的服务端可执行文件 `package/bin/orca-slicer`。临时移走构建用 SDK 后，包内运行库路径和哈希校验通过。三个构建入口下，以下模型处理结果一致。

| 同一模型、basic 高级修复 | 旧 Linux 发布包 | 新 Linux 包内 CLI |
| --- | --- | --- |
| 头骨 | 进程退出 134 | 退出 0，返回 `repaired`，生成修复 STL。重新读入该文件单独检查也返回 `passed` |
| PolySnap | 进程退出 134 | 退出 0，返回 `unresolved` 和 `needs_repair`，保留原件 |
| bed 正常对照 | 本轮未测旧 Linux 包 | 退出 0，返回 `not_needed`，沿用原件 |

各次校验均核对了原件哈希。使用该包内 CLI 运行 `scripts/test_cli_model_stage_report.py --meshlib enabled`，65/65 通过，输入文件完整性检查通过。新包内 CLI 的 SHA-256 为 `d1716b25db0a4779f5e0a10a6dca2139cb17341c74b42e74dfc301a92c1a09e1`。以上为本地 Linux 验证，下一步用同一批输入复测部署后的 QA 接口。

## Mac arm64

Release CLI 与测试目标已重建。`[MeshRepair],[ModelRepair]` 共 163 个用例、5105 个断言通过。另使用同一份源码、相同的编译和链接参数制作对照程序，仅恢复旧的单文件宏。两份 CLI 交替运行三次，结果如下，时间为包含进程启动的中位数。

| basic 高级修复输入 | 旧配置 | 新配置 | 两个构建的业务结果 |
| --- | ---: | ---: | --- |
| 头骨 | 1.71 s | 1.92 s | 均提交修复，复检通过。输出 STL 的三角面记录逐字节相同 |
| PolySnap | 0.13 s | 0.13 s | 均返回 unresolved，保留原文件 |
| bed | 0.13 s | 0.13 s | 均返回 not_needed，沿用原文件 |

同机型、耗材和工艺的 bed 普通切片也做了单因素对照。两份 G-code 的 2479 行可执行命令逐行一致。

## GUI 验证

GUI 和 CLI 都调用共享高级修复核心。桌面交互待手测。在包含本修复的构建中导入头骨，选择高级修复，确认界面能正常结束并显示模型，再对 PolySnap 检查失败或残留提示及原模型保留情况。
