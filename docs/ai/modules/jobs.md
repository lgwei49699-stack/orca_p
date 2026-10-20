# 后台切片、任务和取消

<!-- module-review
{
  "id": "jobs",
  "source_fingerprint": "sha256:75f4f790a6258c9bad107dce6132c1f7ea405a9a01b9b41cfe087b0a7b1d7ed9",
  "reason": "复核PrintApplyCache既有语义不变；此次发布分支合并保留BoostThreadWorker.cpp显式wx/app.h，供无PCH构建获取wxTheApp声明，相对功能分支仅头依赖变化，任务线程、取消和通知业务未改。GUI交互及最终发布构建仍需对应验收。"
}
-->

## 职责与边界

本模块负责后台切片、任务和取消。归属以[模块清单](../modules.json)的 `jobs` 为准。

## 入口与调用

- [BackgroundSlicingProcess.cpp](../../../src/slic3r/GUI/BackgroundSlicingProcess.cpp)：apply/start/stop/call_process；后台生命周期与错误传递。
- [Job.hpp](../../../src/slic3r/GUI/Jobs/Job.hpp)：process/finalize/Ctl；工作线程与主线程合同。
- [BoostThreadWorker.cpp](../../../src/slic3r/GUI/Jobs/BoostThreadWorker.cpp)：任务排队、结束通知和取消调度。

## 核心流程

BackgroundSlicingProcess 的 apply/start/stop 管理 Print 的后台状态，`call_process()` 捕获处理异常。普通 Job 把 process 放到工作线程、finalize 放回 UI 线程，Ctl 提供进度、取消和主线程调用 future。

## 配置与约束

Print 步骤状态与 GUI 工作线程状态不同。模型／配置差异决定失效范围；只更新参数面板而没有失效会复用旧路径。跨线程函数不可返回仍持有栈引用的后台任务。

后台 apply 依据共享 Print 的失效结果重置 G-code。网格替换并落床时，部分有效区域缓存的坐标必须在这一次 apply 同步更新，避免首次导出刷新误清完成结果；不能通过忽略 INVALIDATED 或回用旧 G-code 绕过问题。GUI/CLI 共享核心已补 `[PrintApplyCache]` 回归，后台导出实际交互仍以[GUI交接](../validation/2026-10-05-reconstruction-gui.md)为准。

## 失败与取消

Job::finalize 接收 canceled 和 exception_ptr；未消化异常会再抛出。取消必须在耗时循环内合作检查并避免提交部分结果；不能假设 stop 总能打断不可中断的第三方单次调用。

## 修改与验证

Linux 发布分支保留 `BoostThreadWorker.cpp` 对 `wx/app.h` 的显式包含，不依赖预编译头提供 `wxTheApp`；此次合并未调整任务派发/取消逻辑。工作流检查不等于 GUI 运行通过，当前状态见[Linux记录](../validation/2026-10-19-linux-github-packaging-review.md)。

检查开始、连续参数修改、取消后重切和关窗口；算法修改还要验证 Print 状态及缓存恢复，不能只看进度达到 100%。

现有测试/工具范围：`tests/fff_print/test_print.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[FFF 切片步骤与层状态](slicing.md)、[自动分盘与打印板](arrangement.md)、[模型检测、修复与保色](model-repair.md)、[GUI 生命周期与主工作台](gui-shell.md)。
