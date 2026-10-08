# 应用入口与 CLI

<!-- module-review
{
  "id": "cli",
  "source_fingerprint": "sha256:7e3ce60a4abd6e6f0ca80d1fb041c5aea35fd5808a41cbc308daa924f627eb0b",
  "reason": "保留独立重建预算/协议改动；CLI全部切片新增跳过空盘和全禁用盘，显式指定禁用盘仍报错，启用越界模型不跳过。通过PartPlateList共享导出快照统一多盘切片包选择；普通解析、check-model v4、旧repair JSON未变。"
}
-->

## 职责与边界

本模块负责应用入口与 CLI。归属以[模块清单](../modules.json)的 `cli` 为准。

## 入口与调用

- [OrcaSlicer.cpp](../../../src/OrcaSlicer.cpp)：main → CLI::run；动作、模型加载与导出路由。
- [OrcaSlicer.hpp](../../../src/OrcaSlicer.hpp)：CLI 动作与配置成员的接口边界。

## 核心流程

`main()` 进入 `CLI::run()`，解析动作、变换和配置，加载模型后按请求分盘、切片或导出；需要交互界面时转入 GUI 启动。模型导出走 `CLI::export_models()`，工程导出走 `export_project()`，不是同一种输出。无 GUI 的业务依然复用 Model、Print 和文件格式层。

独立 `--reconstruct-model` 与内部 `--reconstruction-worker` 在 `CLI::run` 的普通解析、预设和 GUI 启动之前分流到 ModelReconstruction。前者要求一个无色 STL、尚不存在的 `--outputdir` 和显式 `--accept-reconstruction-risk`；后者只用于受控任务目录中的 worker 协议。GUI 与公共 CLI 共享候选核心，不通过旧 `--repair-model cgal` 或 `--check-model` 间接执行重建。

## 配置与约束

命令行加载的机器／耗材／工艺配置与输入工程的配置合成为有效配置，再应用动作及对象覆盖。输入顺序、单位变换、多盘及颜色分组都会影响输出，不能只比较命令末尾几个参数。日志、进程退出码与结果 JSON 分别核对。

`--slice 0` 全部切片跳过空盘及全部实例明确关闭打印的盘，不因这些盘在 apply 后没有打印对象而中断任务。显式指定该盘仍执行原有校验；启用但越界的模型不因此跳过。`--min-save --export-3mf` 使用共享导出快照，只写选定的可打印盘的 G-code/切片信息，未选中盘仅留编号空槽兼容既有读取器；参与打印但未切片、缺失 G-code 或没有目标盘仍失败。普通带模型工程允许包含未切片盘，不套用切片包限制。

重建首版仅为模型候选入口，拒绝额外输入、普通切片配置／变换和未白名单参数；STL 数值按毫米解释，不猜单位或采用原修复的 geometry-only 导入降级。公开参数为 `--reconstruction-alpha`、`--reconstruction-offset`、`--reconstruction-max-deviation`（毫米）及 `--reconstruction-timeout`（秒）。其中 max-deviation 保留参数名但改为偏差告警阈值，不再因超过它拒绝已通过完整网格检查的候选；帮助和 `deviation_policy=warning_only` 明确这一点，与 GUI 告警文案一致。最多三档尝试共享总预算，报告包含每轮及最终 alpha/offset。默认几何／资源策略见[模型修复](model-repair.md)。由于首版尚未自动试切片，不能把“同一预设名”或 CLI 候选生成当成与 GUI 同有效配置切片对照已完成。

## 失败与取消

重建输入上限为 300 万面且 STL 文件最多 256 MiB，超过任一限制即受控拒绝；输出仍最多 100 万面。未传 `--reconstruction-timeout` 时共享核心按模型面数选择 60/180/300 秒，传入的 1–300 秒覆盖自动值。内存预算共享 GUI 的分档及物理 RAM 约束，不由 STL 字节大小代替面数决定；`resource_limits` 记录实际生效值，worker 超时/内存不足也保留该记录。具体分档见[模型修复](model-repair.md)，帮助同步说明限制。

输入解析、参数组合、IO 和切片校验失败有不同出口；分盘未排入与进程成功不能画等号。应同时核对盘归属、警告和实际产物。用户没有要求时不要启动真实设备任务，也不要把运行生成的 result.json 入库。

重建不覆盖源 STL／已有目录，不生成旧 `result.json`，仅在本次创建的输出目录保存 `candidate.stl` 与 `reconstruction.json`。拒绝／失败不给出可用候选；读取和报告中明确区分候选成功、形状未确认、`trial_slice=not_run` 和 `print_ready=false`。公共命令退出 0 只能表示候选工作流成功，不表示可打印；专用 worker 退出状态和业务拒绝报告分开解释。

默认 `parameters.close_open_boundaries=true`，与 GUI 的“包裹前尝试闭合开放边界”相同；`--reconstruction-no-close-open-boundaries` 显式关闭。该参数完整传递至 worker，旧内部请求缺字段时取默认 true。封闭副本必须通过完整检查才能用于包裹；有意开口可能被填上，仍要求风险确认。报告新增 `engine.boundary_closure` 与 `engine.metrics.open_surface_envelope_warning`，分别解释预闭合结果和原汤包裹的薄壳风险；不能仅因命令退出 0 推断墙/填充可生成。不改 `--check-model` v4 或旧 repair JSON。

## 修改与验证

修改动作路由优先检查 run/export_models 和结果序列化；验证无参数启动、模型导出、分盘→切片和不合法参数。先查看本分支可用的 CLI 脚本，测试写入隔离输出目录。共享功能同时核对 GUI 的调用及生效配置，验证实际产物与错误语义一致；对应 GUI 步骤按[交接规范](../gui-validation.md) 提供，不能只凭 CLI 退出 0 结束验收。

现有测试/工具范围：`tests/fff_print/test_gcode.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

独立重建另用 `scripts/test_cli_model_reconstruction.py --binary <当前应用可执行文件> --output-dir <全新证据目录>`，校验二进制／输入 hash、独立报告、候选 STL、输入与既有输出不变；脚本不构建、不试切片、不打印。运行身份与结果在[主记录](../validation/2026-10-04-smart-print-engine.md)，GUI 路径和具体对照在[交接清单](../validation/2026-10-05-reconstruction-gui.md)。

## 分支现状

master 包含独立 `--check-model` 工作流、受约束的高级修复与对象分盘接入；检测只允许单个未变换 STL/OBJ，不能与任意切片变换混用。

## 关联模块

[模型／工程格式、颜色与缩略图](formats.md)、[配置定义、预设继承与资源更新](presets.md)、[自动分盘与打印板](arrangement.md)、[FFF 切片步骤与层状态](slicing.md)、[模型检测、修复与保色](model-repair.md)。
