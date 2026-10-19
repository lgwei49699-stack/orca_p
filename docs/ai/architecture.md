# 架构与跨模块流程

## 系统分层

`src/OrcaSlicer.cpp` 统一接入 GUI/CLI；`src/slic3r` 管交互、设备和系统适配；`src/libslic3r` 保存模型、配置、几何、切片和输出核心。`resources` 是运行输入，`localization` 是翻译源，`tests` 是回归源码，`deps/deps_src` 是第三方边界。按职责的入口链接见 [模块索引](README.md)。

## GUI 编辑到切片

GUI_App → MainFrame/Plater → Model/对象与盘 → PresetBundle::full_config → BackgroundSlicingProcess::apply → Print::apply → Print::process → GCode 导出/处理 → GUI_Preview/GCodeViewer。

- [场景编辑](modules/scene-editing.md) 修改对象/体积/实例/绘制；[参数面板](modules/gui-parameters.md) 区分全局和对象覆盖；[工程状态](modules/project-state.md) 记录脏状态与撤销。
- [任务层](modules/jobs.md) 分开工作线程 process 和主线程 finalize；不能在后台直接写控件，取消结果不得冒充成功。
- [切片](modules/slicing.md) 维护步骤依赖与缓存；[墙](modules/walls.md)、[填充](modules/fill.md)、[支撑](modules/support.md) 各自产生带角色/线宽/厚度的挤出数据。
- [筏层](modules/raft.md) 和 [Brim](modules/adhesion.md) 的处理能力以本分支正文为准，不由 GUI 标签推定。G-code 需要保留适当顺序、必要空移和安全边界。

## CLI 与文件流

CLI::run → 参数/动作解析 → Model 格式加载 → 配置合成 → 可选变换/修复/分盘 → Print::apply/process → 导出及结果报告。GUI 与 CLI 共享核心；现有入口的配置来源和交互保护需分别核对。后续同一业务改动必须同步接入两端，在相同有效输入下保持规则、结果及失败语义一致，具体按 [验证规范](validation.md) 对照；不能把历史差异直接视为允许的新差异。

[格式](modules/formats.md) 区分 STL/OBJ 网格、3MF 工程和已切片包；[模型修复](modules/model-repair.md) 区分导入基础整理、高级事务修复和专用检测工作流。文件写入成功、JSON return_code、路径几何及设备打印成功需要分开记录。

## 配置数据流

资源品牌清单/父子 JSON → [预设加载](modules/presets.md) → 系统/用户/工程选择 → 有效机器/工艺/耗材配置 → 对象/体积/层范围覆盖 → Print 参数。

不存在可以套用到所有选项的“0=继承”规则；类型、单位和覆盖看 PrintConfig 与使用点。AppConfig 的应用偏好不等于 PrintConfig 的切片参数；[向导](modules/guide.md) 的目录解析也不等于当前已编辑预设。

## 分盘与设备流

[分盘](modules/arrangement.md) 从实例占地、颜色、固定对象、平台与间距构造候选，成功后应用变换及盘归属；切片使用盘局部坐标。不能只因模型包围盒容纳就认为附着料线也容纳。

[设备](modules/devices.md) 选择、[网络主机](modules/network-hosts.md) 上传和[云端](modules/cloud.md) 鉴权/导入是不同入口。设备类型、显示名、耗材映射和云预设 ID 各有合同；本地切片成功不自动授权真实设备打印。

## 扩散风险与验证

公共几何、配置定义、Plater 和 Print 是多模块集成点；清单用 watch 把其变化送到相关模块复核。SLA 与 FFF 的树/底座流程独立；SLA 测试目录当前未在顶层 tests 默认启用。参考 [验证规范](validation.md) 与 [分支边界](branch-scope.md)，不要将存在源码/测试误写成已运行通过。
