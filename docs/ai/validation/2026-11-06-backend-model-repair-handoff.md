# 后端接入交接：模型检查、高级修复与 MeshLib 重建

## 接口基准与验收范围

本交接对照 Orca 发布提交 `0f7c72560f`（标签 `manual-build-linux-20261106-01`）。归档时，`master/758457092e` 与该基准之间的引擎变更为 CGAL 编译配置修正，本文涉及的 CLI 调用参数和阶段报告实现一致。

第 9 节记录 2026-11-06 读取后端工作区时的差距。接入方应先与自己的分支和部署包核对，再按第 10 节执行 Mac 本地联调及 Linux 环境验收。本文提供接口及验收方法，部署状态由后端验证记录确认。

## 1. 接入目标与职责

采用一条顺序流程：**快速检查 → 有明确缺陷才高级修复 → 验收并判断是否值得重建 → 最多重建一次 → 验收并交付结果**。检查通过就结束当前修复流程，不为健康模型重复修复。

| Orca 负责 | 后端负责 |
|---|---|
| 几何检查、CGAL 高级修复、MeshLib 重建 | 原始文件快照、任务编排、产物保存与返回 |
| 高级修复后的残留依据、重建适用性及资源估算 | 决定是否允许近似重建、是否允许按闭合实体封口 |
| 重建候选的完整网格验收、外形变化数据 | 外部进程截止、并发配额、产品级采用规则 |

本接口不分盘、不切片、不修改打印参数、不下发打印。网格验收通过与实际可打印、孔槽尺寸保真、打印质量通过是不同结论。

以下主流程针对**单个、毫米单位、无色 STL**。原有检查/高级修复也支持 OBJ，但 `--model-stage-report` 的交付链路仅支持 STL，独立 MeshLib 重建不接受彩色 STL/OBJ/3MF。需要保色的旧接口继续使用其原有合同，不要为了接入本流程静默丢弃材质。

## 2. basic / full 的准确含义

| 项目 | basic | full |
|---|---|---|
| 指定方式 | `--check-model --model-inspection-mode basic` | `--check-model --model-inspection-mode full`，也是默认值 |
| 检查 | 先检查五类轻量缺陷；均为阴性才继续查询非流形顶点、自交是否存在；发现明确问题可以停止后续统计 | 完整缺陷诊断及部件/包含关系分析，仍受检查预算限制 |
| 高级修复 | 同一套 CGAL 共享修复核心，但前后检查和结果处理采用 basic 范围 | 同一核心，前后使用完整诊断，适合重建证据评估 |
| MeshLib 重建 | 不存在 basic 重建模式 | 不存在 full 重建模式；重建候选统一完整验收 |

五类轻量项为开放边、非流形边、退化面、重复面和方向冲突。存在性查询返回 `count=1, count_is_lower_bound=true, complete=false`，表示**至少有一处，未枚举总数**。其他尚未执行项可能为 `count=null, status=not_requested`，不能补成 0。

首检的 `mesh_status=needs_repair` 且 `next_action=advanced_repair`，就是进入高级修复的有效依据。不能先用 `check_complete=false` 或 `inspection.status=incomplete` 一刀切拒绝它；但执行异常、报告失效或没有明确修复建议，也不能视为上述快检阳性。

本次推荐组合：**首检 basic；允许重建的高级修复阶段 full + repair-evidence；重建始终完整验收**。这是后端应接入的流程，不表示旧服务已经按阶段切换模式。

直接调用 `--reconstruct-model` 不接受 `--model-inspection-mode basic/full`，传入会被拒绝。同一输入和同一配置下，重建算法不因之前使用 basic/full 而切换；完整流程的结果差异可能来自前序修复产物或分流不同。

## 3. 调用命令与文件传递

以下为可替换路径的命令模板，**不是已执行记录**。后端使用参数数组启动进程，例如 Java `ProcessBuilder(List<String>)`；不要拼接用户文件名到 shell 命令。

每个任务保存不可变的 `original.stl`。每阶段使用独立输出目录，读取该阶段报告指向的文件，不猜测内部 `model-check-*` 子目录名。重建输出目录必须尚不存在，其父目录必须已经存在。

### 3.1 首检

```bash
ORCA_BIN=/opt/orca/orca-slicer
REPAIR_JOB=/srv/repair/job-unique-id

"$ORCA_BIN" --check-model "$REPAIR_JOB/original.stl" \
  --model-inspection-mode basic --export-stl \
  --model-stage-report --model-reference "$REPAIR_JOB/original.stl" \
  --outputdir "$REPAIR_JOB/check"
```

输出：`check/result.json`、`check/model-stage.json`，以及阶段报告 `artifact.path` 指向的 STL。该产物是原生导入准备后的网格，不能把检查结果描述为完全未经处理的原文件统计。

- `passed + check_complete=true`：本阶段检查通过，可结束修复流程并返回已核验产物。
- `scope=basic + mesh_status=needs_repair + next_action=advanced_repair`：进入下一步，允许已确认缺陷的计数未枚举完。
- 其余未知、执行失败或无有效判断：停止并返回原因，保留原件。

### 3.2 高级修复、完整验收及重建准入

```bash
"$ORCA_BIN" --check-model "$REPAIR_JOB/original.stl" \
  --repair-model cgal --model-inspection-mode full --export-stl \
  --repair-evidence \
  --reconstruction-voxel-mm 0.30 \
  --reconstruction-max-grid-cells 200000000 \
  --reconstruction-memory-mb 2048 --reconstruction-timeout 60 \
  --model-stage-report --model-reference "$REPAIR_JOB/original.stl" \
  --outputdir "$REPAIR_JOB/advanced"
```

这里使用同一原始快照作为高级修复输入，保持与首检的输入身份一致；重建输入则必须改为高级修复报告实际交付的产物。参考文件始终为 `original.stl`，用于跨阶段比较外形变化。

只有业务明确允许把输入按闭合实体封口时，才在上述高级命令中增加：

```text
--repair-evidence-close-boundaries 1
```

这个开关允许残留开边成为重建依据，**本身不执行重建**，也不保证后续保留所有设计开口。不要对开放曲面统一打开。

输出：`advanced/result.json`、`advanced/model-stage.json`、报告指向的 STL。`--repair-evidence` 只返回判断，不启动 SDK。资源预算参数在此是下一步重建的准入配置，**不会把本次 CGAL 高级修复限时为 60 秒**。

### 3.3 仅准入允许时重建一次

先解析并验证 `advanced/model-stage.json`，取得：

| 后端变量 | 来源 |
|---|---|
| `ADVANCED_STL` | `advanced/` 下的 `artifact.path`，校验 `artifact.sha256` |
| `VOXEL_MM` | `reconstruction.admission.voxel_mm` |
| `MEMORY_MB` | `reconstruction.admission.memory_limit_mb` |
| `RUN_SECONDS` | `min(admission.timeout_ms / 1000, 任务剩余秒数)`；不足 1 秒不启动 |

```bash
"$ORCA_BIN" --reconstruct-model "$ADVANCED_STL" \
  --reconstruction-engine meshlib \
  --reconstruction-voxel-mm "$VOXEL_MM" \
  --reconstruction-memory-mb "$MEMORY_MB" \
  --reconstruction-timeout "$RUN_SECONDS" \
  --accept-reconstruction-risk \
  --model-stage-report --model-reference "$REPAIR_JOB/original.stl" \
  --outputdir "$REPAIR_JOB/reconstruction"
```

输出：`reconstruction/reconstruction.json`、`reconstruction/model-stage.json`、成功时的 `candidate.stl`。失败时阶段报告或候选可能不存在；旧目录产物不能拿来补缺。

重建命令**不传** `--model-inspection-mode`、`--reconstruction-max-grid-cells`、`--repair-evidence-close-boundaries`、`--export-stl` 或切片配置。`--repair-evidence` 在独立重建中可选，仅附加已有结果的证据投影，不承担前一步的重建准入。

体素、输入产物与准入保持一致；不要在 Java 再按最长边改体素，也不要失败后自动粗化、多轮重试。实际重建有自己的启动前检查，所以高级准入通过仍可能因重建进程资源或候选验收不通过而失败。

## 4. 报告读取与分流合同

后端统一用 `model-stage.json` 定位产物和读取阶段结果；检查/高级阶段另读 `result.json` 确认 CLI 结果，重建阶段另读 `reconstruction.json` 确认候选状态。文件内容和进程退出结果一起判断。

### 4.1 公共阶段报告

| 字段 | 调用方处理 |
|---|---|
| `schema` | 固定 `orca_model_stage_v1` |
| `status` | `completed` 表示报告所述阶段完成，不等于模型修好 |
| `stage` | 必须与调用一致：`check` / `advanced` / `reconstruction` |
| `input_sha256` | 与本阶段输入文件一致；重建阶段应等于上阶段产物 hash |
| `reference_sha256` | 三阶段均与原始快照一致 |
| `artifact.path`、`artifact.sha256` | 相对当前输出目录定位产物并校验；不能解析为目录外文件 |
| `inspection.status` | `passed`、`defects_detected`、`incomplete`；结合阶段解释 |
| `inspection.mesh_status` | `passed`、`needs_repair`、`unknown` |
| `inspection.check_complete` | 完整性，不可单独作为“是否应开始高级修复”的判断 |
| `inspection.issues` | 七类缺陷；保留 `null`、`complete`、下界标记的真实含义 |
| `inspection.scope` | basic 时明确为 `basic`；当前 full 报告省略此字段，不要求字面值 `full` |
| `inspection.next_action` | basic 阶段提供，首检据 `none/advanced_repair` 分流 |
| `repair_status` | 检查为 `not_requested`；高级为 `not_needed/repaired/partially_repaired/unresolved`；成功重建阶段报告为 `repaired` |
| `change` | 外形变化数据；basic 为 `not_requested/app_basic`，不能要求 basic 提供 full 外形指标 |
| `reconstruction` | 高级阶段的重建判断和准入结果；其他阶段不可复用为启动重建许可 |

`result.json` 的检查摘要在 `model_checks[0]`，当前 `schema_version=4`；请求证据时，重建准入路径为 `model_checks[0].boundary.reconstruction.admission`。它与阶段报告的 `reconstruction.admission` 对应。注意 basic 摘要范围字段叫 `inspection_scope`，阶段报告中叫 `inspection.scope`；本流程首检的 `next_action` 从阶段报告读取。

### 4.2 高级修复结果与重建许可

| 高级修复后的情况 | 后端行为 |
|---|---|
| 检查通过，`assessment=not_needed` | 接受通过验收的高级产物，结束 |
| `partially_repaired/unresolved`，且满足下述全部准入条件 | 保留高级产物，启动一次重建 |
| `assessment=undetermined` | 未取得充分重建依据，结束当前修复流程并如实保留残留状态 |
| `assessment=blocked` 或进程/报告失败 | 停止，不通过重建掩盖检查失败 |
| `admission.accepted=false` | 记录准入原因，不启动重建；资源拒绝不等于高级修复执行失败 |
| `admission` 缺失 | 当前流程协议不匹配，不默认为允许；也不将模型标为健康 |

自动进入重建必须同时满足：

1. 高级 CLI 正常退出，`result.json.return_code=0`，阶段报告有效且产物身份校验通过。
2. 业务显式允许近似重建，当前仍有残留问题。
3. `reconstruction.assessment == "evidence_present"`。
4. `reconstruction.applicability == "eligible"`。
5. `reconstruction.admission.accepted == true`。
6. `reason` 为以下已支持依据；涉及封口时还要与本任务授权一致。

| `reconstruction.reason` | 含义 |
|---|---|
| `proper_crossing_after_repair` | 已证明修后表面存在真实穿透，不是仅凭一般自交计数 |
| `open_boundaries_under_closed_solid_policy` | 明确允许按闭合实体封口，且修后仍有开放边界 |
| `no_effective_regions` | 完整参考 Z 截面未形成有效区域，且满足引擎的有效证据条件；不等于实际工艺试切片结果 |

**不要再要求高级残留网格的 `check_complete` 一定为 true 才允许重建。**已知几何缺陷可能阻断包含关系分析；是否仍有足够完整有效的依据由上述证据合同判断。重建后的最终候选则必须完整通过，二者不是同一门槛。

准入示例（数值仅用于说明字段，非性能承诺）：

```json
{
  "assessment": "evidence_present",
  "applicability": "eligible",
  "reason": "proper_crossing_after_repair",
  "admission": {
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
}
```

顶层 `reason` 说明为什么值得尝试；嵌套 `admission.reason` 说明资源准入。不得混用。拒绝报告中未计算的数值可能不存在，不能补 0。

## 5. 重建结果怎么判定

执行一次 MeshLib 原生 `rebuildMesh`；候选只有检出精确自交时才追加一次 SDK 局部 CutAndFill，然后再验收。不是多轮精度搜索，也不使用 CGAL 重建。

正常交付要求：

- 进程退出 0；`reconstruction.json.schema=model_reconstruction_v1`、`backend=meshlib`、`status=candidate_ready`、`mesh_validated=true`。
- `model-stage.json` 为有效 `reconstruction` 阶段，产物/hash/参考身份一致。
- 最终 `inspection.status=passed`、`mesh_status=passed`、`check_complete=true`，七项 `issues.*.complete=true` 且 `count=0`。
- 引擎共享核心已额外检查闭合、方向、正体积及局部坐标/物理坐标转换后的网格。
- 满足后端既有产品采用规则后，再交付新文件；失败保留原件和此前明确保留的产物，不能把未完成候选当成功结果。

七项为 `boundary`、`nonmanifold_edge`、`nonmanifold_vertex`、`self_intersection`、`degenerate`、`duplicate`、`orientation`。

### 导出兼容必须同步

| 阶段 | `artifact.verification` | `artifact.readback_verified` |
|---|---|---|
| 检查 / 高级修复 | `physical_triangles` | `true` |
| 本版 MeshLib 重建 | `binary_stl_count_and_size` | `false` |

重建候选已经完整检查，写出二进制 STL 后校验三角面数量、文件长度和 SHA256；不再把整个 STL 重新读入一遍。这是当前正常交付，**不能要求所有阶段 `readback_verified=true`**。只对重建阶段接受新的校验方式，不扩展到检查/高级修复；路径、文件 hash、网格验收仍保留。

### 外形和设计保留的边界

- `change.bbox_deviation_mm`：相对原始快照的包围盒变化。
- `change.sampled_source_to_candidate_mm` / `sampled_candidate_to_source_mm`：双向采样距离；`distance_is_upper_bound=false`，不是严格最大误差。
- `change.reference_components` / `candidate_components`：部件数量变化。重建可能合并相交壳体，**不应强制数量完全相同**；相同数量也不证明孔槽/内腔保留。
- `--reconstruction-max-deviation` 是独立重建的偏差**告警**阈值，默认 0.25 mm，不是引擎拒绝阈值，也不是体素尺寸。本示例沿用默认值；如后端需要覆盖，作为重建配置显式传入。
- 本地后端已有外形采用门槛：重建按包围盒偏差判定，双向采样距离作为记录；检查/高级修复还有对应保形证明及部件规则。这些是后端政策，不要与 Orca 的网格通过混为一谈，本交接不新增或重设计这套门槛。

即使候选通过，重建报告仍会保留 `shape_confirmation=not_confirmed`、`applied=false`、`trial_slice=not_run`、`print_ready=false`、`requires_manual_confirmation=true`。它表示 CLI 交付近似候选，未替产品确认外形或执行打印。服务端若按既有明确授权政策采用候选，应记录服务端采用行为，不修改引擎报告冒充已确认、已试切片。

## 6. 资源、时间和退出处理

### 6.1 已确认的服务端默认配置

| 参数 | 高级准入中传入 | 独立重建中传入 | 约束 |
|---|---:|---:|---|
| 体素 mm | 0.30 | 采用准入值 | 正、有限、可表示的 float；不自动改精度 |
| 最大名义格数 | 200000000 | 不传此参数 | 准入入口范围 1～2147483647 |
| 内存估算门槛 MiB | 2048 | 采用准入值 | 整数 128～4096；2048 MiB = 2 GiB |
| 重建预算秒数 | 60 | 采用准入预算与剩余时间的较小值 | 1～300；不是整个修复任务或高级修复的限时 |

2 亿格按修后包围盒估算：`max(1,ceil(X/v)) × max(1,ceil(Y/v)) × max(1,ceil(Z/v))`，不是三角网格数或实体体积。内存估算同时考虑输入、表面工作量、网格存储、距离工作区及候选验收预留，后端不重复实现公式。

准入先估计未来重建的额外工作量；实际重建启动时还会加上自身进程的一次驻留内存读数。因此准入通过不是实际运行成功保证。当前没有运行中内存采样/超限中断，也没有硬 2 GiB 内存上限。

典型拒绝原因：`backend_unavailable`、`grid_cell_budget_exceeded`、`estimated_memory_budget_exceeded`，以及输入/数值限制。保留结构化原因，不按英文错误文案分流。

独立重建仍可能提示“超过 1 亿格”的历史警告，它不是本流程 2 亿格准入上限；**不能因 stderr 有 warning 就判失败**。同样不能因没有 warning 就跳过准入。

必须显式传服务预算：独立重建不传内存/时间时，仍按输入规模选择 120/180/300 秒、1024/2048/4096 MiB 等共享自动预算，不会自动采用本流程 60 秒/2048 MiB。

### 6.2 超时和进程释放

`--reconstruction-timeout` 为协作停止，部分 SDK/验收阶段可能延迟响应；初始文件读取、最终导出等也不保证都在该预算内。后端继续用现有外部进程超时机制：到期停止并回收进程，确认退出后释放执行配额；不要把 60 秒描述为 Orca 内部所有阶段的硬截止。

后端外部预算与传入引擎的运行预算一致或有明确受控的清理窗口，不能传入 60 秒却仍等待默认 300 秒。记录引擎时长与任务总时长；超时、取消或进程崩溃不触发换体素重试。

输入容量保留：重建最多 300 万输入面、256 MiB STL，最多 100 万输出面。这些是引擎容量上限，不是“此规模一定可在 60 秒完成”的承诺。多任务总内存和并发由后端现有队列/配额管理。

### 6.3 退出码与成功语义

| 场景 | 处理 |
|---|---|
| 检查退出 0，但检出缺陷 | 检查正常完成；按明确建议进入高级修复，不当作模型通过 |
| 高级 + evidence 退出 0，但 `unresolved/partially_repaired` | 流程正常完成但仍有缺陷；读取准入，不自动标“修好” |
| 高级资源准入拒绝，流程退出 0 | 不启动重建，保留高级结果和残留状态 |
| 检查/高级进程非零或 `return_code` 非零 | 不进入下一修复阶段；记录原因、保留已有可用成果，不采用失败阶段残留文件 |
| 重建退出 0 | 仍需核对候选报告及产物；不等于外形/打印通过 |
| 独立重建退出 2，或被超时/信号终止 | 重建失败/拒绝；候选与阶段报告可能不存在，不重试 |

普通旧式 `--repair-model cgal` 没有本流程的完整阶段协议；旧的非证据组合对无进展也可能返回非零。不要把这些旧返回值与本交接命令混用，更不要全局把非零结果当可继续。

## 7. 后端最小编排示意

以下是逻辑示意；参数、类型、产物身份先按前述合同校验，不照抄成多套检测算法。

```text
保存 original 快照与 hash
check = 执行首检(basic, stage-report)
若进程或报告失败：结束，保留 original
若 check 完成且通过：返回 check 产物
若不是明确 basic 阳性 + advanced_repair 建议：结束，记录原因

advanced = 执行高级修复(full, evidence, stage-report, 服务预算)
若进程或报告失败：结束，保留此前可用产物
保留有效的 advanced 产物及其残留缺陷状态
若 advanced 完成且通过：按产品采用规则返回
若没有业务重建授权：结束
若 evidence_present、eligible、admission.accepted 未同时满足：结束
若封口依据与本任务允许封口设置不一致：结束

rebuild = 使用 advanced 产物与 admission 配置重建一次
若进程、完整网格验收或产物校验失败：结束，保留此前可用产物
按既有产品采用规则返回新候选；记录近似重建与外形变化
```

执行状态、网格结果、采用状态分开记录。建议复用已有任务字段保存每阶段命令有效参数、退出原因、时长、input/reference/artifact hash、inspection、repair_status、reconstruction 及 change，不新增一套策略版本或在线多轮优化系统。

## 8. GUI 与服务端的边界

GUI 手动重建和 CLI 共用 MeshLib 候选生成及最终网格验收核心。GUI 可由用户选择体素精度并确认采用/撤销；服务端按本交接的固定配置和准入编排。

GUI 没有这条后端自动三阶段调用链，资源默认值也不保证与服务端相同。同一模型、变换和有效配置才适合比较算法结果；“GUI 成功”不能代替后台 60 秒预算的验收。本文是后端接口文档，本次不新增 GUI 功能或 GUI 验证结论。

## 9. 后端接入差距快照（2026-11-06）

2026-11-06 只读核对的后端：`Print3DSliceService`，分支 `feature/f_模型修复独立接入_20261029`、HEAD `ea34e78` **加已有未提交工作区改动**。它不是已部署版本证明。用户已明确由后端另行提交，本轮未改后端。

接点：`print3d-slice/src/main/java/com/gfd/print/td/slice/service/ModelRepairWorkflow.java`、同模块 `config/ModelRepairConfig.java`。

| 项目 | 2026-11-06 核对到的状态 | 接入时需核对的事项 |
|---|---|---|
| basic 阳性进入高级 | 工作区已有 `startAdvanced` 例外 | 保留并补验存在性下界样例，不能退回统一拒绝 incomplete |
| 分阶段模式 | 目前 CHECK/ADVANCED 共用 policy 的 inspectionMode | 允许重建时：首检 basic，高级强制 full；同时调整日志和报告 scope 校验 |
| 高级准入参数 | 当前高级命令未透传四项预算 | 传体素、2 亿格、2048 MiB、60 秒；从任务有效配置读取 |
| 重建许可 | 已检查 assessment/applicability/reason，未消费 admission | 追加 `admission.accepted=true`，缺失按协议不匹配处理 |
| 实际体素 | `reconstructionVoxelMm(...)` 有最长边/1024 自动调整 | 改为直接使用本次 admission 值；不保留另一套自动改精度策略 |
| 资源配置 | Java 默认内存 1024 MiB；通用进程超时 300 秒 | 对齐已确认服务默认；重建单独预算并保留任务剩余时间限制，不改变高级阶段为 60 秒 |
| 内存说明 | 注释仍写整个进程 RSS budget | 改成启动前估算门槛，不宣传运行峰值限制 |
| STL 导出校验 | 工作区已有仅重建接受 `binary_stl_count_and_size` 的兼容 | 同相关测试一起由后端提交；确认目标分支/部署包包含它 |
| 外部进程终止 | 已有 `execute/terminateAndReap` | 复用，确保进程结束才释放配额，不另造进程框架 |
| 外形采用 | 已有独立判定 | 保留现行产品规则；网格通过、外形接受、打印验证分别记录 |

不需要修改 App DTO 才能完成引擎调用改造；是否向 App 展示近似重建说明，按已有产品接口/授权落实。不能仅升级 Orca 包就宣称后端准入逻辑已经生效。

## 10. 本地联调与交付验收

先 Mac 本地，再由部署负责人验证 Linux。以下是后端待执行清单，本次文档编写未重跑引擎、未连接服务或服务器：

| 场景 | 应观察的调用与结果 |
|---|---|
| 健康模型 | 只首检；检查通过，产物身份正确 |
| basic 首次发现自交/非流形顶点，计数为下界 | 进入一次高级修复；不报“检查不完整所以不能修复” |
| 高级已修好 | full 通过，`not_needed`；不重建 |
| 高级正常无进展，但有明确依据 | 可退出 0且仍 unresolved；只在准入允许时重建 |
| 高级部分修复 | 保留部分产物；重建输入 hash 对应这份产物，不能换回原件 |
| 仅开边，封口许可关闭/开启 | 对比是否取得该项证据；关闭时不能仅凭开边启动重建 |
| 2 亿格或预估内存超限 | 高级正常结束并拒绝重建；没有启动 MeshLib 进程 |
| admission 缺失、MeshLib 不可用、检查/证据失败 | 明确停止原因，不默认为可以重建 |
| 重建成功 | 新导出校验方式可接受；完整七项通过、hash 正确、候选单独保存 |
| 重建失败、超时、取消 | 不重复试跑；保留先前成果，进程退出后配额释放 |
| 带孔槽/内腔代表模型 | 保存 change 数据，按现有采用规则核对；网格通过不冒称结构完全保留 |
| 并发两个任务 | 输出目录独立、报告不串任务、配额与终止行为正确 |

输入样本复用现有真实模型及确定性夹具，无需为了本次接入再扩大模型库。已有 Orca 验证证据见[准入交付记录](2026-11-06-reconstruction-admission-handoff.md)和[发布集成记录](2026-11-06-release-integration.md)；这些不能替代上述后端联调。

部署验收记录应包含引擎 commit/tag、二进制 hash、后端 commit/实际配置、模型 hash、每阶段有效命令与结果、耗时和最终选择的文件。构建成功不等于服务器业务链路已验证。

## 11. 源码与进一步说明

本文以所列 release HEAD 源码为准，以下路径可直接定位：

- [CLI 路由与参数校验](../../../src/OrcaSlicer.cpp)：`--check-model` 与独立重建分流。
- [检查/高级工作流](../../../src/libslic3r/ModelCheckWorkflow.cpp)：模式、输出、证据及准入发布。
- [重建证据](../../../src/libslic3r/ModelRepairEvidence.cpp)：assessment/applicability/reason 的实际判定。
- [阶段报告](../../../src/libslic3r/ModelStageReport.cpp)：字段、产物校验与外形变化数据。
- [重建入口与共享候选](../../../src/libslic3r/ModelReconstruction.cpp)：CLI 参数、预算、候选状态与导出。
- [网格验收](../../../src/libslic3r/MeshReconstruction.cpp)、[MeshLib 适配](../../../src/meshlib/MeshLibAdapter.cpp)：完整网格检查及一次原生重建。
- [模型修复模块](../modules/model-repair.md)、[CLI 模块](../modules/cli.md)、[历史检查协议专题](../../../doc/model-check-workflow.md)。旧专题含历史流水，当前后端主流程优先按本交接及当前源码执行。

本次源码定位使用模块文档与直接阅读：CodeGraph MCP 未暴露，本机 CLI 路径存在，但发布工作区没有 `.codegraph` 索引；未初始化或借用其他工作区索引。

### 2026-11-06 文档验证记录

- `python3 scripts/check_ai_module_docs.py --all`：35 个模块，0 问题。
- `git diff --check`：通过。
- 3 段 Bash 模板经 `bash -n` 语法检查通过，1 段 JSON 示例解析通过，12 个正文相对链接均存在；模板未实际调用引擎。
- CLI 和模型修复模块入口已同步链接本文，源码指纹未变。
- 本轮只修改文档；未构建/运行引擎，未修改后端，未执行 GUI 或部署环境验证；第 10 节保留为后端待验收项。
