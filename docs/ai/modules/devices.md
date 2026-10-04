# 设备、AMS 与打印任务监控

<!-- module-review
{
  "id": "devices",
  "source_fingerprint": "sha256:27b5d883f9870477690a09c927b1d587f499521e159400e38cc394c5ea521610",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责设备、AMS 与打印任务监控。归属以[模块清单](../modules.json)的 `devices` 为准。

## 入口与调用

- [DeviceManager.cpp](../../../src/slic3r/GUI/DeviceManager.cpp)：`MachineObject::parse_json` 更新单机状态；`DeviceManager::set_selected_machine` 与设备列表接口管理当前监控对象。
- [SendToPrinter.cpp](../../../src/slic3r/GUI/SendToPrinter.cpp)：`SendToPrinterDialog::on_ok` 校验选中设备、准备盘 G-code／配置并创建 `SendJob`；取消贯穿导出与发送。
- [TaskManager.cpp](../../../src/slic3r/GUI/TaskManager.cpp)：`start_print` 建立任务组；`schedule` 调用 `NetworkAgent::start_print`，记录发送完成、失败或取消状态。
- [Monitor.cpp](../../../src/slic3r/GUI/Monitor.cpp)：`MonitorPanel::update_all` 读取选中设备及连接状态，再更新 Status、HMS、媒体等面板。
- [StatusPanel.cpp](../../../src/slic3r/GUI/StatusPanel.cpp)：`StatusPanel::update` 将设备状态分发到温控、AMS 和子任务显示；暂停／中止有各自事件入口。

## 核心流程

设备状态经 `MachineObject::parse_json` 更新；`MonitorPanel::update_all` 每次重新获取选中机器，校验归属及连接状态后才刷新 Status/HMS/媒体。`StatusPanel::update` 再更新温控、AMS 和任务信息。

发送入口有两条：[SendToPrinter](../../../src/slic3r/GUI/SendToPrinter.cpp) 的 `on_ok` 先调用 Plater 准备文件，非 LAN 设备还准备配置 3MF，再提交 `SendJob`；[SelectMachine](../../../src/slic3r/GUI/SelectMachine.cpp) 的 `on_ok_btn` 提交 `PrintJob`。两者都交给 worker，但不能用一个入口的成功替代另一个入口验证。

多机 `TaskManager::start_print` 只建立任务组；调度线程按并发数和间隔调用 NetworkAgent。`TS_SEND_COMPLETED` 表示发送完成，不等于设备打印完成。

## 配置与约束

设备 model_id/gfd_device_type、云返回类型、机型显示名称与耗材索引不能混用。设备状态更新应关联正确机器；界面按钮可见由能力／配置控制，不意味着调用已经授权。

## 失败与取消

未选中、设备不再归属当前列表、连接中和离线分别处理；Monitor 会清除已失效的选择。SendToPrinter 的取消回调中止 worker，导出进度回调同时传播取消；导出负返回值会提前结束，不继续发送。TaskManager 将发送失败与用户取消分开记录。

修改异步回调时重点核对设备 ID、窗口生命周期和线程归属，防止切换设备后旧结果更新新界面。HMS 与温度显示正确只是监控证据，不能推导打印成功。

## 修改与验证

改设备映射先核对 DeviceManager、资源机型声明及云端设备标识；EPONEPlus 的无空格协议标识与带空格显示名分别保留。改发送流程同时检查 SendJob／PrintJob、文件准备、LAN／云模式、AMS 映射及取消；改监控验证切换设备、断线重连、任务更换和旧回调。

暂无本模块专属自动化测试目标。静态审阅可以验证调用关系；状态刷新需 GUI，涉及真实发送／设备校准时按任务授权范围操作。遵循[验证规范](../validation.md)，本轮没有下发任务。

## 关联模块

[网络传输与打印主机](network-hosts.md)、[账号、云端导入与模型服务](cloud.md)、[打印机、耗材与工艺资源](resource-profiles.md)、[校准模型与向导](calibration.md)。
