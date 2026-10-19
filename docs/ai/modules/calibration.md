# 校准模型与向导

<!-- module-review
{
  "id": "calibration",
  "source_fingerprint": "sha256:49f3c2862b1f3b3c190d6063d1d45f91f7b55be14fa7ba4817f8ae15028db06d",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责校准模型与向导。归属以[模块清单](../modules.json)的 `calibration` 为准。

## 入口与调用

- [calib.cpp](../../../src/libslic3r/calib.cpp)：`CalibPressureAdvance` 计算校准速度与挤出量，并组织压力提前校准路径。
- [Calibration.cpp](../../../src/slic3r/GUI/Calibration.cpp)：`CalibrationDialog::on_start_calibration` 从设备校准选项发起校准，`update_cali` 刷新进度。
- [CalibrationWizard.cpp](../../../src/slic3r/GUI/CalibrationWizard.cpp)：`CalibrationWizard` 串联步骤、设备状态和结果保存，`save_preset` 写入校准后的预设。
- [CalibUtils.cpp](../../../src/slic3r/Utils/CalibUtils.cpp)：`CalibUtils` 校验名称／K 值／流量输入，组织校准请求及结果查询。

## 核心流程

校准入口按温度、流量、压力提前等类型生成模型或请求设备校准；CalibrationWizard 多页收集设备和耗材选择，CalibUtils::process_and_store_3mf 将模型和完整配置切片打包，结果再用于保存预设。

## 配置与约束

校准专用参数与常规工艺分开，不能把试验塔的分层指令默认写回所有工艺。设备能力决定自动校准是否可用，离线生成与在线校准也不同。

## 失败与取消

取消向导、设备离线、错误耗材或结果不合法不应保存半套参数。模型生成成功不等于已取得可靠测量结果，实测数值仍由用户确认。

## 修改与验证

改校准需核对生成几何、分层参数、输出文件和预设保存；复核 resources/calib 输入和按钮范围，本轮仅源码和资源关系核对。

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[设备、AMS 与打印任务监控](devices.md)、[配置定义、预设继承与资源更新](presets.md)、[FFF 切片步骤与层状态](slicing.md)、[G-code 生成与后处理](gcode-export.md)。
