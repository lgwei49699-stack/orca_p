# G-code 解析、统计与预览

<!-- module-review
{
  "id": "gcode-preview",
  "source_fingerprint": "sha256:2c69c9964b2e0338af0ee10e222eb9d7ad4ab438b79144680e889a94b21cc883",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责G-code 解析、统计与预览。归属以[模块清单](../modules.json)的 `gcode-preview` 为准。

## 入口与调用

- [GCodeReader.cpp](../../../src/libslic3r/GCodeReader.cpp)：`GCodeReader::parse_file` 逐行解析并维护坐标状态，通过回调交给调用方。
- [GCodeProcessor.cpp](../../../src/libslic3r/GCode/GCodeProcessor.cpp)：来源/配置初始化及路径、耗材和时间统计。
- [GCodeViewer.cpp](../../../src/slic3r/GUI/GCodeViewer.cpp)：`GCodeViewer` 把处理后的 MoveVertex 分类为可绘制路径与 GPU 缓冲区，管理可见范围。
- [GUI_Preview.cpp](../../../src/slic3r/GUI/GUI_Preview.cpp)：`Preview`／`View3D` 连接工作台、画布和预览控件；切换盘及层范围时刷新显示。

## 核心流程

GCodeReader 解析行，GCodeProcessor 根据来源、配置和指令更新运动、角色、耗材与时间统计；GCodeViewer/GUI_Preview 将结果变成图层滑块和渲染缓存。普通文件通用预览与提取完整 Orca 内嵌配置是不同能力。

## 配置与约束

来源标记、配置块、机型、喷嘴、耗材直径及硬度影响校验和统计。硬度显式值优先；喷嘴硬度 0 时按类型取值，类型未知才跳过；耗材要求 0 也跳过。不能为消除警告修改耗材参数。

当前分支可检索的配置键：`nozzle_hrc`、`nozzle_type`、`required_nozzle_HRC`、`filament_diameter`。定义与默认值集中于 PrintConfig；实际生效仍要读取对象覆盖后的值。

## 失败与取消

损坏配置、未知来源、缺少耗材映射和不合法 T 指令要分别排查。预估时间取决于机器限制，不等于实测打印时间；警告和后台切片错误的级别不同。

## 修改与验证

改解析应覆盖分块导出、非本产品来源、普通 G-code、已切片 3MF、空配置和缩略图。检查统计与模型路径，界面能显示不能替代配置成功加载。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支不直接拥有用户版共用的品牌兼容判断改造；遇到 WiseBeginner 或旧中文品牌文件，要区分通用预览成功与内嵌配置读取成功，按实际来源规则检查。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[G-code 生成与后处理](gcode-export.md)、[配置定义、预设继承与资源更新](presets.md)、[渲染、控件与平台 UI](render-ui.md)。
