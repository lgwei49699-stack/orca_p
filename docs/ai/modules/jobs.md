# 后台切片、任务和取消

<!-- module-review
{
  "id": "jobs",
  "source_fingerprint": "sha256:a29df77a4c6885ee4351a00437e8d32167891775ce24413854cbdc0801f131db",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
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

## 失败与取消

Job::finalize 接收 canceled 和 exception_ptr；未消化异常会再抛出。取消必须在耗时循环内合作检查并避免提交部分结果；不能假设 stop 总能打断不可中断的第三方单次调用。

## 修改与验证

检查开始、连续参数修改、取消后重切和关窗口；算法修改还要验证 Print 状态及缓存恢复，不能只看进度达到 100%。

现有测试/工具范围：`tests/fff_print/test_print.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[FFF 切片步骤与层状态](slicing.md)、[自动分盘与打印板](arrangement.md)、[模型检测、修复与保色](model-repair.md)、[GUI 生命周期与主工作台](gui-shell.md)。
