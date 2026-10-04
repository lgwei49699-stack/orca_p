# 应用入口与 CLI

<!-- module-review
{
  "id": "cli",
  "source_fingerprint": "sha256:aa0bca78f6c5f9f2d9023a1f09a42a362afc3dbd423a9c15208c2d53f41fd34e",
  "reason": "按 2026-10-04 用户约定补齐 GUI／CLI 同步检查、必要构建与测试分工、GUI 验证交接；只改维护规则，源码指纹及既有测试证据未变。"
}
-->

## 职责与边界

本模块负责应用入口与 CLI。归属以[模块清单](../modules.json)的 `cli` 为准。

## 入口与调用

- [OrcaSlicer.cpp](../../../src/OrcaSlicer.cpp)：main → CLI::run；动作、模型加载与导出路由。
- [OrcaSlicer.hpp](../../../src/OrcaSlicer.hpp)：CLI 动作与配置成员的接口边界。

## 核心流程

`main()` 进入 `CLI::run()`，解析动作、变换和配置，加载模型后按请求分盘、切片或导出；需要交互界面时转入 GUI 启动。模型导出走 `CLI::export_models()`，工程导出走 `export_project()`，不是同一种输出。无 GUI 的业务依然复用 Model、Print 和文件格式层。

## 配置与约束

命令行加载的机器／耗材／工艺配置与输入工程的配置合成为有效配置，再应用动作及对象覆盖。输入顺序、单位变换、多盘及颜色分组都会影响输出，不能只比较命令末尾几个参数。日志、进程退出码与结果 JSON 分别核对。

## 失败与取消

输入解析、参数组合、IO 和切片校验失败有不同出口；分盘未排入与进程成功不能画等号。应同时核对盘归属、警告和实际产物。用户没有要求时不要启动真实设备任务，也不要把运行生成的 result.json 入库。

## 修改与验证

修改动作路由优先检查 run/export_models 和结果序列化；验证无参数启动、模型导出、分盘→切片和不合法参数。先查看本分支可用的 CLI 脚本，测试写入隔离输出目录。共享功能同时核对 GUI 的调用及生效配置，验证实际产物与错误语义一致；对应 GUI 步骤按[交接规范](../gui-validation.md) 提供，不能只凭 CLI 退出 0 结束验收。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

master 包含独立 `--check-model` 工作流、受约束的高级修复与对象分盘接入；检测只允许单个未变换 STL/OBJ，不能与任意切片变换混用。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[配置定义、预设继承与资源更新](presets.md)、[自动分盘与打印板](arrangement.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
