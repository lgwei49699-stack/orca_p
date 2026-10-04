# 公共运行时与平台辅助

<!-- module-review
{
  "id": "platform",
  "source_fingerprint": "sha256:0cb5c33d631149288f69ffa83491ff9cf9406b5e7c1130d5aabc9701ff4762e1",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责公共运行时与平台辅助。归属以[模块清单](../modules.json)的 `platform` 为准。

## 入口与调用

- [libslic3r.cpp](../../../src/libslic3r/libslic3r.cpp)：定义公共 `SCALING_FACTOR`；几何内部坐标单位约定从 libslic3r.h 引入。
- [utils.cpp](../../../src/libslic3r/utils.cpp)：维护资源／用户／临时目录、日志及文件系统辅助；目录生命周期由调用方决定。
- [Platform.cpp](../../../src/libslic3r/Platform.cpp)：平台枚举及名称转换，供平台差异判断和诊断使用。
- [Thread.cpp](../../../src/libslic3r/Thread.cpp)：线程命名与主线程身份辅助，供后台任务与主线程约束检查使用。
- [LocalesUtils.cpp](../../../src/libslic3r/LocalesUtils.cpp)：`float_to_string_decimal_point` 等接口处理数值与 locale，避免配置和 G-code 小数点歧义。

## 核心流程

公共库提供路径、日志、时间、线程、平台检测、区域设置和异常辅助；GUI Utils 的字体、进程、Retina/暗色支持适配操作系统。此层是 GUI／CLI 共享基础，常见故障表现为上层资源或解析问题。

## 配置与约束

UTF-8、文件系统路径、数字 locale、线程生命周期和平台宏是重要约束。不要依赖用户语言解析配置数值，不能把本机绝对路径固化到预设或文档。

## 失败与取消

线程退出、信号/异常捕获和进程返回码需按平台区分，第三方调用不保证可恢复。资源路径定位失败时先核对数据目录与打包布局，不自动改用户配置。

## 修改与验证

修改 locale／文件／线程工具要检查中文及空格路径、取消与清理、GUI/CLI 共享调用；时间工具有独立测试，系统集成仍需目标平台。

现有测试/工具范围：`tests/libslic3r/test_timeutils.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[构建、打包与平台集成](build.md)、[GUI 生命周期与主工作台](gui-shell.md)、[配置定义、预设继承与资源更新](presets.md)、[网络传输与打印主机](network-hosts.md)。
