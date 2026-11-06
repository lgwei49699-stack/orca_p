# 重建准入：Orca 实现与 Slicer 接入交付

## 当前状态和顺序

用户确认：默认体素 **0.30 mm**、包围盒名义网格 **2 亿格**、启动前内存估算门槛 **2048 MiB**、运行预算 **60 秒**。预算可配置，不表示保证修好或准时协作退出。用户随后确认取消运行中内存采样/超限中断；实际峰值允许超过这个预估门槛，Orca 不再用它中途取消。

本次完成第 2 项：在 Orca 既有 `--repair-evidence` 中返回重建准入。工作区 `orca-clubbot-mac-perf-20261103`，分支 `codex/meshlib-mac-adaptive-sampling-20261104`，本批基于 `acd2b468bc`，审核完成后由用户授权提交并推送。具体提交号以 Git 历史和本地 `build/diagnostics/reconstruction-delivery-20261106/` 交付记录为准。未修改后端、未联调服务或服务器、未合并 2.3.2、未打包发布。

下一步按本文处理第 1 项后端接入，再进行第 3 项 Mac 本地真实链路验证，最后第 4 项发布打包/Linux 验证。不得单凭本文件把后端记为已接入。

## 判定规则

| 顺序 | 判断 | 处理 |
|---|---|---|
| 1 | 高级修复后完整网格已经通过 | `assessment=not_needed`，不重建 |
| 2 | 已有确切残留依据 | 沿用既有证据：修后表面确实穿透；调用者明确允许封口时的残留开边；或完整参考截面均无有效区域。一般自交总数、文件大小不能单独替代依据 |
| 3 | 当前产物适用 | 单个普通模型/实例、现有输入容量内、没有会丢失的材料/绘制/修改器等属性；MeshLib 可用 |
| 4 | 物理格数 | 按修后实际网格和本次体素计算 `max(1,ceil(X/v)) × max(1,ceil(Y/v)) × max(1,ceil(Z/v))`；`v` 与 SDK 一样转为 float。默认 **≤200000000** |
| 5 | 几何工作量内存估算 | 复用现有公式，包含输入网格、表面工作量、带 padding 的网格存储、距离工作区和默认 100 万输出面候选验收预留；估计额外工作量不超过 2048 MiB |
| 6 | 真正运行 | 后端只启动一次原生 MeshLib 重建，沿用相同模型、体素和预算；保留超时/取消和最终完整网格验收，不再监测运行内存 |

格数是包围盒离散规模，不是三角形数量、实体体积或实际分配的全部内存。这里不新增文件 MB/面数的经验拒绝阈值；既有 300 万输入面等引擎容量限制保留。也不按模型名分流、不试跑 SDK、不自动粗化、不多轮重建。

未来重建进程的估算不应加上当前高级修复进程的驻留量：两者不是同一个进程。实际重建在调用 SDK 前只读取一次自身驻留内存，加进原有准入估计；通过后不再采样内存、不因实际内存超过门槛取消。几何预估是启发式，不是实际内存上界。

检查未完成/无有效依据/不适用时，准入为 false；检查不完整仍沿用既有错误语义。**预算拒绝则是正常分流结果**，不会把已经完成的高级修复改成执行失败，也不会把残留缺陷标为已修好。

审核修正：资源准入通过后，如果阶段报告发布失败，最终业务结果撤销准入许可，返回 `accepted=false / reason=workflow_failure`，已计算的工作量仍保留。后端仍需确认 CLI 正常完成及原有依据/适用性条件，不能只读取估算数字。

## 请求与返回

参数仅在 `--check-model --repair-model cgal --repair-evidence` 组合中用于自动准入。无这组参数的普通切片、分盘、原检查/修复入口不增加准入扫描。独立 `--reconstruct-model` 原参数解析和 GUI 手动重建选择不变。

```sh
OrcaSlicer --check-model input.stl --repair-model cgal \
  --model-inspection-mode full --repair-evidence \
  --reconstruction-voxel-mm 0.30 \
  --reconstruction-max-grid-cells 200000000 \
  --reconstruction-memory-mb 2048 --reconstruction-timeout 60 \
  --export-stl --model-stage-report --model-reference original.stl \
  --outputdir advanced-output
```

`--repair-evidence-close-boundaries 1` 仅在业务明确允许把输入作为闭合实体封口时加；默认不启用。

| 参数 | 默认 | 输入范围/单位 |
|---|---:|---|
| `--reconstruction-voxel-mm` | 0.30 | 正、有限、float 可表示的 mm |
| `--reconstruction-max-grid-cells` | 200000000 | 1～2147483647；仅准入入口 |
| `--reconstruction-memory-mb` | 2048 | 128～4096 MiB |
| `--reconstruction-timeout` | 60 | 1～300 秒；报告以 ms 返回 |

`result.json` 的路径是 `model_checks[0].boundary.reconstruction.admission`；阶段报告 `model-stage.json` 的路径是 `reconstruction.admission`。两份报告相同。原 schema、assessment、applicability、reason 和退出语义保留，新增嵌套对象。

以下是本地确定性夹具实际返回的准入内容，数值不是所有模型的固定值：

```json
{
  "accepted": true,
  "reason": "within_heuristic_budget",
  "voxel_mm": 0.3,
  "max_grid_cells": 200000000,
  "grid_cells": 136000,
  "memory_limit_mb": 2048,
  "timeout_ms": 60000,
  "estimated_incremental_bytes": 162654124,
  "estimate_is_upper_bound": false
}
```

已健康或没有依据的结果只需 `{ "accepted": false, "reason": "no_detected_mesh_defects" }` 等既有依据原因，不为没计算的项目补 0。其他准入拒绝原因包括 `backend_unavailable`、`grid_cell_budget_exceeded`、`estimated_memory_budget_exceeded` 及现有数值/输入限制原因。

## 第 1 项后端最小接入

只读核对到的本地后端目录是 `projects/threeDService/Print3DSliceService`，分支 `feature/f_模型修复独立接入_20261029`；其中有用户/前序未提交修改。这里只记录接点，不切分支、不修改或提交该目录。实施时先确认应修改的后端分支，不把它与 Orca 分支混用。

核心接点为 `print3d-slice/src/main/java/com/gfd/print/td/slice/service/ModelRepairWorkflow.java`：

1. 在 `command(...)` 的高级修复 + evidence 请求中传入上述四项有效参数。首检继续 basic，发现明确缺陷就进入高级修复；进入可能重建的高级修复后验收使用 **full**。不能因首检“至少发现一处、数量未数全”而禁止高级修复，也不能用 basic 的缺失诊断当成完整重建依据。
2. 高级修复后保留已有 assessment/applicability/封口策略检查，再要求 `admission.accepted == true`。false 就记录原因并保留当前产物，不启动 SDK。缺字段属于引擎协议不匹配，不视为允许，也不视为模型健康。
3. 当前 `reconstructionVoxelMm(...)` 会按最长边 /1024 自动增大体素；该次接入应移除这条自动改精度路径。以准入报告的 `voxel_mm`、`memory_limit_mb`、`timeout_ms/1000` 调用后续重建，使用同一份已验证高级修复产物。不要准入 0.30、实际又按另一精度运行。新 `max-grid-cells` **不传给独立重建命令**，它仅供前一步准入使用。
4. 重建阶段预算单独限制为至多 60 秒（配置可覆盖），并取任务剩余预算的较小值。2 GiB 只用于启动前内存估计，不新增后端高频内存监测。引擎 `--reconstruction-timeout` 是协作取消；复用后端 `execute(...)` 的外部 `waitFor` / `terminateAndReap`，维持进程退出前不释放执行配额。读取/导出与不支持中断的 SDK 阶段不保证在引擎预算内返回，不能省掉外部进程截止处理。实际预算减少时同样传给引擎。
5. 兼容原生重建导出协议：重建阶段 `artifact.verification=binary_stl_count_and_size`、`readback_verified=false` 是正常的新导出校验；检查/高级修复仍为 `physical_triangles`、`readback_verified=true`。所核对本地服务已有对应条件分支，但它是未提交工作区状态，不证明目标部署分支具备。继续校验路径、输入/参考/产物 hash、最终完整 inspection、成功状态，不因一个 verification 字段放行失败候选。
6. `reconstruction.json.resource_limits.budget_mode` 改为 `preflight_memory_cooperative_timeout`；`memory_at_admission_bytes` 是启动前一次测量，`memory_metric` 标明计量方式，旧 `observed_peak_memory_bytes` 删除，不能把初始值当成峰值。参数名 `--reconstruction-memory-mb` 保留，含义是预估门槛。
7. 重建成功后采用仍须满足原有结果验收。`accepted=true` 仅允许尝试一次，不能替代最终七项网格检查、闭合及正体积检查；它也不保证孔槽/内腔等设计意图全部保留。

无需在 Java 重写格数、内存公式，也无需增加独立预检接口或策略版本对象。

## 验证证据和边界

Mac arm64 Release / MeshLib 3.1.3.429，当前源码最小构建通过：

- 审核后应用 SHA256：`462881ef9a8782d9e071d1536bd3bb5fe6e58902485a8ceec0065d2edab20855`。
- 审核后测试程序 SHA256：`334d8f889a388e22ca7795656f2bd455ac4085fb82b72b30fc36b20a0042d49f`。
- 相关原生：**70 组、1538 断言通过**，涵盖阈值等号/超限、体素变化、表面内存超限、非法参数、检查/证据、计时取消及新增失败撤销准入。此前真实 SDK 停止/复用 **1 组157断言通过**；本次审核未再次修改 SDK 控制/重建代码，保留对应原构建证据。已移除的运行内存监测测试不再计入，新报告明确没有运行峰值字段。
- 检查 CLI：**122/122**；预算改变不改变修后文件字节及缺陷结果，健康不重建，缺必要组合/非法值拒绝。
- 阶段报告：**65/65**；新增预算拒绝真实用例，两份报告字段一致、产物 hash/readback 保持。
- 原重建 CLI：上一轮 **42/42**；启动前资源拒绝、实际生成、失败保留与新版资源报告语义通过。本次审核修正仅影响检查结果投影；未再次运行未改动的独立重建套件。
- 翻译 `msgfmt --check-format`、Git 空白检查及 35 模块文档检查通过。

审核后构建、70 组单元测试、122/65 CLI 回归、源码/程序身份与修改前失败日志在 `build/diagnostics/reconstruction-admission-review-20261106/`。上一轮真实 SDK/独立重建/翻译证据保存在 `build/diagnostics/reconstruction-preflight-only-20261106/`；原准入实施证据另在 `build/diagnostics/reconstruction-admission-20261106/`。阶段报告首次新增用例误用仅共面相交夹具，它没有“确切穿透”依据，正确返回 undetermined；改用既有确定性穿透夹具后通过，未修改生产证据规则来迎合测试。

100 个历史原件按实际尺寸和 0.30 mm 静态回算：1 亿格挡住 6 件，其中含一件 10.769 秒成功模型；2 亿格挡住 2 件，二者原本已资源拒绝，没有挡住此前 77 件成功结果。该项只验证**格数条件**，不是新的 100 件完整高级修复→准入→重建验收。三角面和模型 hash 已核对；没有重复重建这批模型。

原生重建上一批结果仍为 **77/100 完整网格通过**，成功中位 1.262 秒、最长 14.105 秒；全部调用最长 43.621 秒是失败件。官方整批 84/100 与我们预算/计时口径不同。当前第 2 项未修改 SDK 算法或输出网格；不能把资源准入理解成修复率提高。此前已知开放穹顶 0.10 mm 拒收等能力边界仍在，本轮没有修改或宣称解决。

GUI 自动准入不适用：GUI 由用户明确发起重建，选择精度后仍走共享的启动前内存估算、时间/取消控制、候选验收和采用/撤销流程。没有新增 GUI 自动跳过或默认值变更；GUI 设置提示已同步说明内存仅在启动前估算。本轮未重跑 GUI 交互；上一批 BODY/dragon 的采用、保留、撤销、运行取消证据见[重建对照记录](2026-11-05-mac-rebuild-comparison.md)。后端接入、Mac 真实服务链路、Linux、部署和实物打印均尚未执行。

## 后端接入后的本地验收清单

- 快检发现一处缺陷即进入高级；完整健康首检直接结束。
- 高级修复后健康不重建；无确切依据/不适用明确保留结果。
- 确切穿透且预算允许，只启动一次，命令与准入参数一致。
- 超格数、超估算内存均在重建进程启动前停止；保留高级产物。
- 超运行预算退出后保留前一有效产物，不重复重建、不泄漏执行配额。
- 新导出验证字段通过；坏 hash/不完整检查/失败候选仍拒绝。
- BODY、radio、dragon 及一个预算拒绝模型跑真实本地链路，记录阶段耗时和产物；不先到服务器重复摸索。

## GUI 本轮交接（待界面手测）

本轮共享控制器和 GUI 文案均已编译，CLI/真实 SDK 测试通过，不等于 GUI 交互已验证。可执行文件：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-clubbot-mac-perf-20261103/build/arm64/src/Release/OrcaSlicer`，SHA256 见上文。尚未制作新的 GUI 分发包；不要用旧 OrcaBodyVerify 的结果代替本轮程序。

输入：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-clubbot-mac-perf-20261103/tests/data/model_repair_acceptance/00-healthy-cube.stl`，SHA256 `8618fc713878420a4e66bf21b8c4ee271ccc91eaf19b0cfbd5b821f8564b1065`。新工程、只导入这一个对象，保留原尺寸/姿态，体素 0.30 mm；机型/喷嘴/耗材/工艺沿用当前选择，这些设置不参与重建。

| 操作 | 预期 | 当前状态 |
|---|---|---|
| 在准备页选中方块，右键进入“重建模型”；先用英文，再用包含最新中文资源的包检查同一提示 | 时间预算、启动前内存估算门槛可见；说明重建中不监测内存，文字无裁切；体素值不变 | 英文/中文源串和 MO 精确查译通过；界面排版待手测 |
| 生成候选，选择“保留原模型”；再次生成、采用后撤销 | 不因移除内存采样丢失候选验收、保留/撤销功能；原件不被后台自动覆盖 | 对应 CLI/共享核心通过；GUI 交互待手测 |
| 启动重建后立即取消，随后再次生成 | 等待 SDK 返回后取消，原件保留，可再次使用；不得声称能瞬间强制中止不可取消阶段 | 真实 SDK 取消/超时/再次使用157断言通过；GUI 按钮状态待手测 |

回传本次程序/资源身份、操作时间段和设置页截图；如果测试采用/撤销，另保存前后 STL 供三角面核对。中文语言包本轮已在 `build/diagnostics/reconstruction-preflight-only-20261106/zh_CN.mo` 编译并查译，GUI 分发包须包含对应新资源。上述人工清单保留为 GUI 验收边界，不作为继续添加修复算法的理由。
