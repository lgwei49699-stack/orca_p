# 公共运行时与平台辅助

<!-- module-review
{
  "id": "platform",
  "source_fingerprint": "sha256:e6e9b13fd226a4e99826c6df832507cbe0cc2d6df951dbedabcaa3371ac72fec",
  "reason": "macOS 原生列表可编辑查询改为无副作用；将实际编辑回调移到编辑请求，并为对象列表显式接入单击图标/双击名称。当前 arm64 构建、辅助功能重开及正常操作已回归，CLI 不涉及此原生交互。"
}
-->

## 职责与边界

本模块负责公共运行时与平台辅助。归属以[模块清单](../modules.json)的 `platform` 为准。

## 入口与调用

- [libslic3r.cpp](../../../src/libslic3r/libslic3r.cpp)：定义公共 `SCALING_FACTOR`；几何内部坐标单位约定从 libslic3r.h 引入。
- [utils.cpp](../../../src/libslic3r/utils.cpp)：维护资源／用户／临时目录、日志及文件系统辅助；目录生命周期由调用方决定。
- [Platform.cpp](../../../src/libslic3r/Platform.cpp)：平台枚举及名称转换，供平台差异判断和诊断使用。
- [Thread.cpp](../../../src/libslic3r/Thread.cpp)：线程命名与主线程身份辅助，供后台任务与主线程约束检查使用。
- [MacDarkMode.mm](../../../src/slic3r/Utils/MacDarkMode.mm)：macOS 原生控件适配；对象列表通过 `mac_enable_object_list_editing` 接入真实单击/双击动作，可编辑性查询不得打开编辑器或修改模型。
- [LocalesUtils.cpp](../../../src/libslic3r/LocalesUtils.cpp)：`float_to_string_decimal_point` 等接口处理数值与 locale，避免配置和 G-code 小数点歧义。

## 核心流程

公共库提供路径、日志、时间、线程、平台检测、区域设置和异常辅助；GUI Utils 的字体、进程、Retina/暗色支持适配操作系统。此层是 GUI／CLI 共享基础，常见故障表现为上层资源或解析问题。

AppKit 的 `shouldEditTableColumn` 同时会被辅助功能读取，必须是只读的行列/模式/启用状态判断。对象列表图标/耗材单击、名称双击，以及实际 `editColumn` 请求才转发编辑回调；自定义 wx 编辑器 veto 原生编辑的行为保留。不要将模型修改放回辅助功能查询。CLI 不存在此原生列表交互，不要求增加对应命令。

## 配置与约束

UTF-8、文件系统路径、数字 locale、线程生命周期和平台宏是重要约束。不要依赖用户语言解析配置数值，不能把本机绝对路径固化到预设或文档。

## 失败与取消

线程退出、信号/异常捕获和进程返回码需按平台区分，第三方调用不保证可恢复。资源路径定位失败时先核对数据目录与打包布局，不自动改用户配置。

## 修改与验证

修改 locale／文件／线程工具要检查中文及空格路径、取消与清理、GUI/CLI 共享调用；时间工具有独立测试，系统集成仍需目标平台。

现有测试/工具范围：`tests/libslic3r/test_timeutils.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

2026-10-06 macOS 回归已覆盖只读辅助功能查询、重开工程、重命名、打印开关、显式落床和撤销/重做，证据见[模型修复记录](../validation/2026-10-04-smart-print-engine.md)。未把此局部结果扩展为所有原生控件或跨平台验收。

## 关联模块

[构建、打包与平台集成](build.md)、[GUI 生命周期与主工作台](gui-shell.md)、[配置定义、预设继承与资源更新](presets.md)、[网络传输与打印主机](network-hosts.md)。
