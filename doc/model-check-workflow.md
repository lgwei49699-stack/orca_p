# STL / OBJ 检测与修复流程

更新：2026-09-13。**用户 23:06 重新构建后的完整修复专项已通过：149 个用例、6094 个断言。此前色区扩散、范围误报和测试编译问题已在本轮专项中通过回归；源码审核未发现阻止阶段性入库的问题，按用户要求先提交，再由用户本地验收当前应用版本的 CLI 新流程/几何/材质端到端行为。各轮失败与后续验证分开记录在第 7 节，不将阶段性入库或专项通过等同于 GUI、切片或打印验收。**

## 1. 确认后的流程

1. 用 Orca 共享导入层读取单个 STL / OBJ，关闭旧 STL ADMesh 自动修复。
2. 受控基础清理，保存 prepared.stl / prepared.obj 工作副本。
3. 重读该副本，执行与 demo 对齐的七项几何检测和三项补充检查。
4. 默认到此结束，返回现有 result.json，由调用方决定是否高级修复。
5. 显式追加 --repair-model cgal 时复用既有部分修复策略，不循环修复。
6. 有可保留高级修改：导出 result.stl / result.obj，重读实际文件并复检；高级失败或没有修改：保留 prepared。

新入口不加载打印机/工艺/耗材配置，不排版、不居中、不缩放、不切片，不连接 demo HTTP 服务或远程 Gateway。
新命令不替换普通 GUI、普通 CLI 导入/切片及不带 --check-model 的既有修复流程。本轮共享 STL 读取、部分修复和标注恢复层的缺陷修正也作用于既有调用方，具体范围及回归要求见第 7 节。

## 2. 命令与输出

在仓库根目录用本轮**新构建**的程序执行。下面是示例，不是已执行记录。

```bash
# 检测
./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --check-model --outputdir /absolute/check-output /absolute/input.stl

# 显式高级修复：输入上一步 JSON 给出的 working_model
./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --check-model --repair-model cgal --export-obj \
  --outputdir /absolute/repair-output /absolute/prepared.stl
```

也可首次就组合 --check-model --repair-model cgal，仍先清理、检测。
一次一个 STL / OBJ；--export-obj / --export-stl 二选一，默认沿用输入格式。
每次创建独立 model-check-… 子目录，不覆盖输入。JSON 位于输出目录的 result.json；并发任务须指定不同输出目录，避免报告覆盖。
不新增模型 ID、哈希绑定服务或版本数据库，输入路径、working_model 和阶段足够关联。

- check-only 发现问题可以退出 0，表示返回了结果，**不是模型无问题**。看 mesh_status / check_complete。
- 参数错误、导入/资源/IO 异常、高级修复失败或无可保留修改返回非零。新增检测失败码 -27；其他沿用既有错误码。
- 不接受 --slice、位置/旋转/缩放、材质绑定、配置加载、--auto-repair-model 等参数，避免改变检测对象。允许 --debug / --datadir。
- 有 MTL 时保留支持的面颜色；MTL 缺失沿用共享几何降级并报告 input_materials_ignored；损坏的已有 MTL、非法几何仍拒绝。
- 仅需几何可加 --repair-ignore-materials，本入口允许它配合 check-only。带色 OBJ 不静默导出 STL，须选 OBJ 或显式忽略材质。
- 不承诺 UV、贴图、顶点色或完整渲染材质往返。导入范围沿用主程序，不复制 demo 独立的 OBJ 多边形解析器。

### JSON 字段

沿用 return_code、error_string、output_files、model_repairs、repair_validation。
新增 model_checks 数组，当前一个元素：

| 字段 | 含义 |
|---|---|
| input_file | 本次输入的绝对路径 |
| basic_repair | 清理策略、实际移除数量、因保色/禁止删空而推迟的项 |
| prepared_model / prepared_output_files | 保留的基础工作副本及 MTL（如有） |
| working_model | 当前保留的模型；须结合阶段、错误和检测状态使用 |
| checked_stage | serialized_prepared 或 serialized_advanced |
| inspection | 当前发布模型的分类检测 |
| before_advanced / after_advanced | 高级修复前后检测，未执行的阶段不伪造结果 |
| check_complete | 七项及补充判断是否全部完成，与“是否有问题”独立 |
| operation_status | completed / failed，表示操作已结束；check_complete=false 不表示仍需等待 |
| mesh_status | passed / needs_repair / needs_review / unknown |
| next_action | none / advanced_repair / review_structure / retry_check |
| advanced_repair.status | not_requested / not_started / not_needed / repaired / partially_repaired / unresolved / failed |
| operation_error | 操作失败原因；已保存的基础副本仍可能存在 |
| final_output_files | 已导出并复检的最终文件，不把被拒绝候选列为最终产物 |

needs_repair：七项至少一项有已确认异常，不保证能安全自动修复；补充检查不完整时也可报告。
needs_review：七项无异常、检查完整，但整体朝向不符合当前壳体约定，需确认设计。
unknown：未确认基础异常，但无法完成所需判断，绝不是通过。
passed 只表示当前检查范围通过，始终保留 slicing=not_requested、printability_verified=false。

原有 model_repairs 是**逐 volume 的修复引擎记录**，新 inspection 是**整个导出模型的分类复检**。
组合入口的 repair_validation.mesh 以整体复检为准，并要求本次操作无错误；不要只看单个 model_repairs[].mesh_validated。

## 3. 受控基础清理

白名单：精确共线/重复顶点的零面积记录，同几何且同绕序的重复面。
使用 CGAL 精确谓词，不以面积阈值删除细长面或小面。
不做近邻焊接、补洞、改顶点、反转朝向、拆合壳体、删除内部部件或有效平面；反向重合面不自动删除。

会清空 volume 的候选整项推迟。有面索引涂色/支撑等标注的 volume，推迟到高级修复的严格映射，不静默丢色。
非法索引/非有限坐标属于失败。清理在独立模型结构事务中提交，不是半清理状态。

即使移除数为 0 也保存副本：共享导入有既有坐标/整体负体积朝向规范化，彩色 OBJ 导出也可能进行保色所需的三角细分。
因此“受控”指关闭旧 STL 自动修复并限定新增清理，不等于原始文件字节级检测。
纯几何 OBJ 工作副本使用 float 往返精度，避免旧 %f 六位小数丢失细节。
复检在保存并重导入后运行，高级修复处理同一版本。

## 4. 与 demo 的检测口径

| 键 | 计数含义 / 展示 |
|---|---|
| boundary | 仅被一个非退化面使用的几何边数；不重复计为“破洞数” |
| nonmanifold_edge | 被三个及以上非退化面使用的边数 |
| nonmanifold_vertex | 顶点 link 不是单一路径或环的顶点数 |
| self_intersection | 相交三角形对数；含配对和真实交点/交线/重叠区域，不是独立缺陷数 |
| degenerate | 精确零面积面数 |
| duplicate | 不区分绕序的重合面分组数，不是全部面数或两两组合数 |
| orientation | 恰有两个非退化面，沿公共边方向相同的边数；与整体壳体朝向分开 |

三项补充检查独立，不累加到七项错误总数，不凭数量自动删模型：

- 分离部件按共享整条几何边连通，只有共点不合并。面积占比 ≤1% 仅为小部件候选。
- 内部包含须排除自交/接触/非流形等不满足前提的情况，再由完整闭壳空间查询确认。已确认内部部件不覆盖同壳相连的全部内部多余面。检查不完整时，0 只表示确认了 0 个。
- 整体朝向分开报告实际 inward/outward 和依据完整包含层级的期望朝向。外层向外、内层向内是奇偶约定，不推断设计意图。保留 blocked_by 和 expected_orientation_unknown。

算法移植自 demo native/main.cpp 的 inspect / inspect_components / intersection_geometry。
编入既有 libslic3r_cgal，沿用其浮点选项、CGAL/GMP/Boost；不升级版本、不链接 demo 私有兼容库。

### 对齐的边界

- 默认清理后检测，demo 默认检测原件；应拿 working_model 比较，而非比较不同阶段。
- demo OBJ 独立解析为 double，Orca 网格为 float；极窄/近共面的结果可能因解码精度不同而不同。
- 检测按精确坐标规范化几何顶点，不改变保存模型拓扑，不能替代 OBJ **索引边**闭合验收。
- CLI issues 是按 key 的对象，demo 是带 key 的数组；算法/数量对齐，不要求 JSON 字节一致。
- ID 从 0 开始，只属于本次规范化检测几何，不跨原件/准备件/修复件复用。
- 每类默认最多 200 条详情样本；details_truncated 不等于检测不完整，不影响完整计数。
- 最多 20,000 对自交；截断时 complete=false、count_is_lower_bound=true。部件超过 128 个跳过包含和整体方向。
- 文件上限 64 MiB、解码三角面上限 1,000,000、检测几何顶点上限 1,000,000。资源错误不返回通过。
- 未新增进程隔离或硬超时，服务调用方应限制大模型的时间/内存。历史真实 SIGBUS 样本仍须用主程序新构建复验，不能用 demo 的私有 GMP 兼容构建通过来代替。

## 5. 部分修复与失败保留

例如自交从 200 对降到 10 对：既有策略接受了有效修改，则保留输出，状态 partially_repaired；整体仍 needs_repair，repair_validation.mesh=not_passed。
不要求全部问题清零才保留，也不只凭“总问题数下降”批准危险候选。

- 不放宽已有 CGAL 候选安全门禁和严格保色检查。
- 基础与高级是两个事务；高级失败不丢基础副本。
- 高级没有任何可保留修改：非零、unresolved，保留 prepared。
- 已接受修改导出后仍有问题/无法判定：保留修改和前后报告，标部分修复，供人工判断。
- 导出或重读复检抛异常：不发布候选为最终模型，working_model 保持基础副本。即使目录中物理存在候选文件，也不得绕过报告自行采用。
- 不自动多轮循环；后续显式处理当前 working_model。已干净时基础应幂等，高级应 not_needed。
- 同一 volume 内混有接触/嵌套结构时，部分模式按包围盒接触/重叠的传递关系分组；组间能确认空间分离才独立处理。组内保留原共享顶点、位置和绕序，不逐壳转成正体积。严格模式及一般拆分保持既有策略。
- 此分组路径只有接受了实际修复/基础清理才保留新结构；只是分组、没有进展时恢复原结构并报告 unresolved。候选处理仍经过既有拓扑/自交/保色门禁，取消、工作线程异常等仍回退当前高级事务。
- 仍需后续切片/逐层预览验证，当前不保证打印。

## 6. 实现和验收入口

- [ModelInspection](../src/libslic3r/ModelInspection.cpp)：几何检测、精确清理计划，编入 libslic3r_cgal。
- [ModelPreparation](../src/libslic3r/ModelPreparation.cpp)：基础清理事务、标注和删空保护。
- [ModelCheckWorkflow](../src/libslic3r/ModelCheckWorkflow.cpp)：导入、保存、检测、既有高级修复、导出复检；无 GUI/Gateway 依赖。
- [CLI](../src/OrcaSlicer.cpp)：参数边界与现有 JSON 输出。
- [单元用例](../tests/libslic3r/test_model_inspection.cpp)：[ModelRepair][Inspection] / [ModelRepair][Preparation]，已包含在完整专项。
- [新 CLI 验收](../scripts/test_cli_model_check.py)：STL/OBJ、新流程、部分修复、缺 MTL、失败保留和参数保护，可选与 demo 对比。

用户构建 OrcaSlicer **和 libslic3r_tests** 后，按 [AGENTS.md](../AGENTS.md) 执行原有完整专项与 CLI 几何/材质验收，再执行：

```bash
python3 scripts/test_cli_model_check.py \
  ./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
# 可选追加 --demo-inspector /absolute/path/to/existing/mesh_inspector
```

新脚本不编译、不切片，保存独立临时证据目录、命令、日志、JSON、输入不变检查和总结。
可选 demo 比较使用相同已准备的二进制 STL，避免 OBJ 解码精度差异；不传参数就明确标记对比未执行。
真实复杂模型的本轮检测结果见第 7 节；实际 GUI/颜色往返、跨平台与切片仍需后续验证。

## 7. 历史进度关联

- 前一阶段提交 f762350c97：GUI/CLI 一致性、部分修复、OBJ 保色与缺 MTL 等，不能证明本轮新入口通过。
- 旧本地方案：planning/repair/repair-test-models/mesh-inspector/APP_MODEL_CHECK_REPAIR_PIPELINE_ANALYSIS.md。
- demo 历史记录：同目录 README.md、REPAIR_PROGRESS.md、BOUNDARY_AND_HOLE_GUIDE.md。
- 前一轮验证：planning/repair/cli-gui-recheck-20260913-gx8pFm/verification-summary.json，只证明当时 checkout。
- planning/ 被 Git 忽略，仅在本机可用；本文在受版本控制的 doc/，作为本工作流正式文档入口，本地 demo 文档反向链接到此。
- 本轮编码后的首次运行验证：见下方 2026-09-13 记录。此前静态检查不作为运行通过证明。

后续在本节记录实际提交、构建时间、二进制与新日志；不能把测试代码存在、manifest 预期或旧日志当作本轮通过证明。

### 2026-09-13 17:29 构建后的验证（未全部通过）

- checkout：`codex/model-repair-cli-gui-20260913`，基线 `f762350c97`，叠加本轮未提交检测流程代码。
- 用户提供的 `macos-build-arm64-20260913-172936-922849.log` 本机不存在；实际核验日志为 `planning/build/macos-build-arm64-20260913-172527-909058.log`，包含新检测模块及测试的编译、链接完成记录。
- macOS arm64 / Release；应用 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 更新时间 17:29:20，测试 `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` 为 17:29:21，均晚于本轮源码。本次只运行测试，没有重新编译。
- 本机证据根目录：`planning/repair/model-check-verification-20260913-J1jboA/`；结论及临时详细产物索引见其中 `VERIFICATION.md`。

| 验证 | 实测结果 |
|---|---|
| 既有 CLI 几何/导出验收 | 50 项通过，退出 0 |
| 既有 CLI 材质策略验收 | 30 项通过，退出 0 |
| 完整修复专项 | 142 个匹配用例，1 个用例/断言失败，退出 1 |
| 新检测/基础清理聚焦测试 | 11 个用例：10 通过、1 失败；72 个断言中 1 失败 |
| 新工作流 CLI 验收 | 前 23 项通过，`partial` 失败；后续 `partial-recheck` 未执行 |
| 与 demo 同阶段比较 | 6 个合成夹具及 7 个既有小模型：七项计数/完成状态、部件与包含/方向相关字段一致 |
| 历史大 OBJ、同名 STL、复杂自交 STL | 三次均正常返回 JSON、退出 0；均为 `needs_repair`，不是网格通过；输入及关联 MTL 哈希未变 |

**待处理的三处问题：**

1. 测试夹具缓存不一致：`test_model_inspection.cpp` 的带标注重复面用例直接向 `mesh.its.indices` 添加面，却没有通过 `TriangleMesh` 构造/更新统计；`facets_count()` 仍读到缓存 12，预期为 13。基础清理已报告未修改、因标注推迟，不是已经证明业务逻辑误删了一面。应修正夹具构造，并补直接几何/标注不变断言；不能仅删除断言放行。
2. 部分高级修复不足：同一网格中空间分离的“可补洞立方体 + 非流形结构”，结果仍为 18 面、4 条开放边，未保存任何高级修改。现有保守拆分路径没有把该立方体独立修复；不能声称已覆盖“可修部分保留”的全部场景，也不能取消接触/嵌套壳安全保护来强行通过。基础副本保留正常。
3. 状态映射不一致：上述引擎报告 `unresolved`、`can_continue=true`，外层却因 `repair_model()` 返回 false 提前标为 `advanced_repair.status=failed`，未进入无可保留修改的 `unresolved` 分支。需要统一“无进展”与真正事务失败的返回语义。

真实模型补充说明：`8d739b06… (1).obj` 与同名 STL 的自交达到 20,000 对限制，正确返回 `complete=false`、`count_is_lower_bound=true`；这只证明有界检测未崩溃，不证明完成所有相交计算或永久解决历史 SIGBUS。`f79ce30d…stl` 检出 589 对自交、2 个非流形顶点；其自交检查完整，但补充壳体判断不完整。三次均未进行高级修复或切片。

本次未修改业务实现或测试预期，未提交；只补充了验收证据和本文记录。实际 GUI、完整 CTest、其他平台、打印机参数下的切片/G-code/打印验证未执行。上述阻塞项修复后需重新构建并重跑，不复用本轮日志作为修复后通过证明。

### 17:29 验收后的三项代码修正

1. 修正新基础清理测试的夹具构造，修改索引后重新构造 `TriangleMesh` 更新统计；保留 13 面断言，增加原网格指针、顶点、索引、标注数据和 ID 不变检查。
2. 在共享 `ModelRepair` 增加仅部分模式启用的安全分组后备路径。空间接触/重叠/嵌套的 patch 保留同组，直接按原始面索引提取，避免将非流形共边拆成两条独立边。新 volume 复用源变换、材质配置但关闭重新居中；有实际进展才替换原 volume，无进展丢弃临时分组。旧核心严格模式与一般模型拆分不变。
3. `ModelCheckWorkflow` 根据结构化 `can_continue`、`committed`、`unresolved` 解释修复返回值；引擎无进展的 false 不再自动等同 fatal failed，取消/异常/保色失败仍为失败。

回归补充：原混合模型的 STL/OBJ、两种输入顺序、输出重检及再次高级修复不虚报进展；核心测试覆盖带标注部分修复、变换和共享顶点保留、嵌套空腔绕序、无进展、候选拒绝、异常与取消的事务保护。CLI 无进展明确断言 `unresolved`，单平面危险候选明确断言 `failed`。仍沿用既有修复专项与新 CLI 验收命令，无需修改构建菜单。

编码时仅完成静态审阅、格式/补丁检查和 Python 语法检查，未编译、未运行；用户随后重新构建，实际复测结果如下。上面的 17:29 测试数字不代表此补丁已通过。

### 2026-09-13 17:55 构建后的复测（仍有两处失败）

- checkout 仍为 `codex/model-repair-cli-gui-20260913`、基线 `f762350c97` 加未提交代码。
- 用户给出的 `macos-build-arm64-20260913-175528-334948.log` 本机不存在；实际构建日志为 `planning/build/macos-build-arm64-20260913-175246-892124.log`，包含本轮实现及测试编译、两个程序链接完成记录。
- macOS arm64 / Release；应用更新时间 17:55:21，`libslic3r_tests` 为 17:55:12，均晚于相关源码。本次没有重新编译。
- 本机证据：`planning/repair/model-check-recheck-20260913-yqR7CB/VERIFICATION.md`；完整日志和补充对照脚本位于同目录。

| 验证 | 实测结果 |
|---|---|
| 既有 CLI 几何/导出 | 50 项通过，退出 0；未验证切片 |
| 既有 CLI 材质策略 | 30 项通过，退出 0 |
| 完整修复专项 | 145 个用例：144 通过、1 失败；4264 个断言中 2 个失败，退出 2 |
| 新工作流 CLI | 前 27 项通过，首个部分修复 STL 在导入阶段失败；脚本退出 1，后续用例未执行 |
| 与 demo 比较 | 本次脚本已完成 6 个合成 STL 的同阶段七项及部件相关字段比较；7 个既有小模型、3 个大模型未在此次复跑 |
| 补充部分修复对照 | 两种顺序 × OBJ/首面法向量对照 STL 的修复、复检、再次修复，共 12 次命令符合预期；另两次零法向量 STL 均重现导入失败 |

已验证的改进：13 面夹具缓存/只读保护用例通过；无进展返回 `unresolved`，不再误报 `failed`；无颜色混合模型可保留补洞结果，面数由 18 变为 20、开放边由 4 变为 0，非流形结构仍保留并报告。输出再次检测仍为 `needs_repair`，再次高级修复无进展、不提交、不虚报成功。嵌套空腔绕序及异常/取消等新增事务用例通过。

**仍需处理：**

1. **带标注分组修复失败。** 新用例 `Partial repair groups touching patches apart from a repairable shell` 的两次修复成功断言失败（`test_model_repair.cpp` 的 `REQUIRE(repair_model(...))`）。无标注路径通过；LLDB 捕获实际失败原因：`Repair failed: painted facet annotations could not be mapped onto the repaired mesh.`。当前严格保色/标注保护拒绝本次高级事务，不能宣称分组部分修复已覆盖保色情况；也不能通过允许清空标注绕过。具体映射原因仍需进一步修正和回归。
2. **合法二进制 STL 被误判为文本格式。** 18 面、984 字节的二进制夹具尺寸与头部计数严格匹配，前 128 个面数据字节均不超过 127。现有 ADMesh 格式嗅探以该区间是否出现大于 127 的字节区分格式，误入 ASCII 分支，最终导入失败（JSON `-27`，高级阶段 `not_started`）。两种面顺序都重现；只把首面零法向量改为其几何法向量 `(0, 0, -1)`，顶点、面顺序及其他数据不变，两种顺序即能完成部分修复。此对照用于定位导入缺陷，不是修改测试输入后宣布原验收通过；不能要求用户先改模型，也不能仅调整夹具规避。

本轮仅运行验证、调试定位并记录结果，没有修改业务代码/原验收预期，没有编译或提交。GUI、完整 CTest、其他平台、实际切片/G-code/打印未验证。原有 80 项 CLI 通过不等于新流程整体通过；以上两处需修正后重新构建并重跑。

### 17:55 验收后的两项修正（当时待重新构建，未提交）

1. **STL 在共享读取入口优先识别二进制。** `TriangleMesh::ReadSTLFile` 按实际文件长度、头部面数和每面 50 字节优先确认二进制，包括零法向量、1/2 面短文件、`solid` 开头的二进制头和既有自定义头长度。确认后按小端读取原始面，保留顶点、绕序和属性字节，接回原 `from_stl` 的焊接/可选导入修复及报告；未确认二进制仍用原 ASCII/兼容读取。拒绝非有限顶点，支持取消，失败不发布模型。实现仅在自有 `src/`，没有修改 ADMesh/CGAL 版本或第三方快照，也没有写入或预改用户输入文件。GUI/CLI 通过同一入口生效。
2. **分组时按原始面编号精确复制标注。** 在空间分组结果中携带原面映射，逐面复制完整标注细分位流并重建索引/状态。分组不改变三角面几何，不再调用近似涂色重映射；真正的 CGAL 拓扑变化仍使用既有几何映射，修复前后的严格空间覆盖检查、部分保留、取消/异常事务保护不变。同步覆盖颜色、支撑、接缝、绒毛四类标注，不复制不支持保留的外观面标注。

进一步定位证据来自对 **17:55 旧程序** 的只读调试（不是新补丁通过证明）：`painting_provenance_debug.log` 显示，原面 MMU 标注仅 4 位，分组几何重映射后变为 8040 位并涉及相邻面；该阶段原始覆盖检查通过，随后的再次重映射覆盖检查失败。因此采用原面来源复制，避免在几何未变的分组阶段引入细分噪声。调试脚本/日志位于前述本机证据目录。

回归补充：

- `[stl][Import][Binary]`：零法向量、短文件、普通/`solid` 头、80/128 字节头、原始面坐标/绕序不变、可选旧导入修复、非法顶点/截断/伪造计数/空文件/取消不发布模型。已包含于既有 `[stl]` 完整专项，不新增构建菜单。
- 原分组测试保留修复成功断言，增加失败分支参数/消息，检查三类未修部分标注的逐面位流完全一致、细分子树保留、修复部分颜色面积/位置及不串标注；未改成允许清空标注，也未删除失败用例。
- `scripts/test_cli_model_check.py` 保留原零法向量 STL 输入，新增两种面顺序的双色 OBJ/MTL 部分修复、面积/颜色集合、输出复检和再次修复无进展检查；输入 OBJ 和 MTL 哈希不变。

本次只完成源码审阅、改动区域格式化、`git diff --check` 和 Python 语法检查；没有编译或运行修改后的 C++/CLI 验收，也没有提交。下一步由用户重新构建 **OrcaSlicer 和 libslic3r_tests**，再执行既有完整修复专项、CLI 几何/材质验收和新工作流脚本；通过后再按既有提交格式入库。GUI/实际切片仍单独验收，不把入库当成打印验证完成。

### 2026-09-13 22:41 构建后的用户复测（仍有色区边界失败）

- checkout：`codex/model-repair-cli-gui-20260913`，基线 `f762350c97` 加未提交代码；macOS arm64 / Release。
- 实际应用构建日志为 `planning/build/macos-build-arm64-20260913-224121-574280.log`（用户提到的 `224142-395528` 文件本机未找到）。应用与测试均已链接；应用修改时间 22:41:35、`libslic3r_tests` 为 22:41:36，晚于本轮相关源码。
- 测试目标构建日志 `planning/build/macos-build-arm64-20260913-224209-881685.log` 表明目标已是最新；`cmake --build ... --target ...` 只编译测试，并不运行测试。
- 用户随后于 22:44 运行完整过滤器 `'[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]' --reporter compact`。原始反馈保存在本机 Codex 附件 `f261e6f3-aac8-4588-b7ec-91f05312b1c6/pasted-text.txt`，不是仓库内可分发的日志。
- 末尾为 **`Failed 1 test case, failed 12 assertions.`**；未附总通过数及退出码，不推测补填。12 处均来自 `Partial repair groups touching patches apart from a repairable shell`：两种分组顺序 × 两种带标注场景，颜色面积由 50 扩大到约 50.389862，包围盒最小/最大坐标也超出原色区。
- 该用例的修复成功、未修部分原始标注等断言已不再报告失败；本次输出也没有 STL 导入用例失败。但 **色区不能据此验收通过**，CLI 新流程/材质验收尚未在这次反馈中提供结果。日志中的非法输入、拒绝导出等多为负向用例；另有测试备份目录位于只读根目录的报错，不能将这些文本与断言失败混为一谈。

### 22:44 验收后的色区边界修正（待重新构建，未提交）

根因位于共享 `TriangleSelector::remap_painting`：候选相交判断包含共边/共点，且调用画笔选择后继续沿邻面传播。仅边界接触的面也被作为局部覆盖目标细分着色；邻面传播还会绕过逐面平面距离检查。几何没有移动，颜色覆盖却扩大，因此单纯放大测试容差或只检查“颜色还存在”都不合适。

本轮处理：

1. 重映射候选使用主投影平面上的双精度分离边判断，只允许正面积重叠；共边/共点不转移颜色，不新增小面积过滤阈值。
2. 仅关闭重映射专用 `TriangleCursor` 的邻面传播，仍由 AABB 枚举所有目标面并逐面检查朝向、平面距离和面积重叠；交互画笔行为不改。GUI/CLI、模型拆分和切割恢复标注仍调用同一共享实现，后者须随完整专项一起回归。
3. 新增 `[SavedPainting][Boundary]`：共边/共点不扩色、已有邻面颜色不被覆盖、反向面、三个投影方向、目标坐标偏移、不同标注状态和三轮重复映射；另检验没有邻接索引的四块重新三角化目标仍能全部获得原色区。保留原部分修复测试的面积/位置断言，不放宽保色、取消或异常保护。

本轮仅做源码审阅、局部格式化及 `git diff --check`，未编译、未运行新 C++ 用例或 CLI 验收，未提交。新增用例已在原 `[SavedPainting]` 过滤器内，无需新增构建目标/菜单。需要用户重新构建应用及 `libslic3r_tests`，再运行原完整专项和 CLI 验收；新测试通过前不能宣称色区问题已经验证解决。该修正也不宣称任意跨面细分涂色都能无误差恢复，真正改变色区边界的重映射仍须严格验收。

### 2026-09-13 22:57 构建失败及补正（尚未运行测试）

- 实际日志为 `planning/build/macos-build-arm64-20260913-225745-872766.log`，用户提到的 `225757-523526` 文件本机未找到。构建在编译 `test_saved_painting.cpp` 时停止：两处 `surface_area(...)` 调用未声明，经 Catch2 宏展开报 6 条错误；不是 CMake 配置警告或依赖版本造成的失败。
- 原因是新增测试误用了另一个用例内部的局部 lambda 名称。现已在本测试文件匿名命名空间补齐 `painted_surface_area`，用双精度累加各三角面的叉积面积，并显式包含新增测试所需的标准头文件。面积预期 50、容差和位置断言均不变，未修改生产修复逻辑。
- 日志虽有应用/部分测试目标链接记录，但 `libslic3r_tests` 没有完成本次链接，整次构建不能算成功，也不能运行旧测试程序作为验证。补正后仅做格式化和 `git diff --check`，未重新编译、未运行测试、未提交；需用户重新构建成功后执行原专项。

### 2026-09-13 23:01 用户复测及色区范围检查补正

- 用户给出的 `230101-991202`、`230117-476969` 日志名称本机未找到。实际应用任务记录为 `planning/cache/task-logs/20260913-230047-96-wUrmTLLBCXoXVsXkl8A.log`，其中构建日志写到 `planning/build/macos-build-arm64-20260913-225757-523526.log`，包含新测试编译和链接完成记录，任务退出 0。测试目标任务 `planning/cache/task-logs/20260913-230115-roYRtewl2f-E0afCvQHipg.log` 也退出 0，实际日志为 `planning/build/macos-build-arm64-20260913-230112-071120.log`，已是最新。
- `libslic3r_tests` 修改时间 23:00:54，晚于测试源码的 23:00:26。用户于 23:01:32 运行原完整专项，新面积辅助函数编译问题已解决。
- 原始测试反馈：本机 Codex 附件 `b46204a5-5620-4011-9543-033771b6971f/pasted-text.txt`；末尾为 **`Failed 2 test cases, failed 152 assertions.`**，未提供总通过统计或测试进程退出码。
- 152 处均为包围盒比较：原部分修复用例 8 处（两种顺序 × 两种标注场景 × 最小/最大坐标），新边界用例 144 处（参数组合 × 三轮重复映射的最小坐标）。不是 152 个独立缺陷。面积扩大断言已不再失败；新用例逐面标注索引/位流完全相等的断言也没有失败，重新三角化用例没有报告失败。

源码核查确认范围断言的统计对象错误：`TriangleSelector::get_facets_strict` 只筛选所需状态的面索引，但顶点数组保留选择器中所有仍有引用的顶点，包括其他色区；`bounding_box(indexed_triangle_set)` 则遍历整个顶点数组。新边界夹具中，未着色邻面顶点使数组的最小坐标变小；原部分修复夹具在修复前还带有远处未解决部件的顶点，而修复后只带分组后的顶点。直接比较两个完整数组的包围盒，并不能验证同一色区是否移动。

本轮仅调整两个测试文件：对 `get_facets_strict` 提取的临时副本调用既有 `its_compactify_vertices`，去掉没有被色区三角面引用的顶点，再做原面积/包围盒比较。该操作不删除三角面、不移动坐标、不焊接、不修改原模型或标注；实际扩色/位移仍会触发断言。保留原预期、容差及逐面位流一致性检查，并追加单面色区副本恰有三个被引用顶点的检查。生产修复逻辑本轮不变。

仅完成源码审阅、局部格式化和 `git diff --check`，没有编译或运行修改后的测试，也没有提交。下一次只需重新构建 `libslic3r_tests` 并执行原完整过滤器（无需再次完整打包应用、无需重编 `fff_print_tests`）；新结果通过后，再验收 CLI 检测/修复/导出流程。不能把这次范围误报修正直接等同于全部测试或打印验证通过。

### 2026-09-13 23:06 用户完整修复专项通过

- checkout：`codex/model-repair-cli-gui-20260913`，基线 `f762350c97` 加本轮未提交代码；macOS arm64 / Release，构建目录 `build/arm64`。
- 用户执行 `cmake --build ... --target libslic3r_tests --parallel 2 && libslic3r_tests '[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]' --reporter compact`；反馈包含两个修改后测试文件的编译、测试程序链接及运行输出。
- 测试二进制修改时间 **23:06:09**，两个测试源码均为 **23:04:28**，运行开始 **23:06:10**；这次使用的是修正后的程序，不是旧二进制。
- 完整结果：**`Passed all 149 test cases with 6094 assertions.`**，没有失败断言或空过滤器。附件没有单独打印测试退出码，因此保留 Catch2 的完整通过结论，不补写未提供的数值退出码。
- 本机原始反馈：Codex 附件 `3b47f51d-1823-455e-93b3-a1dec482c0ef/pasted-text.txt`。此前的 1/12、2/152 失败记录保留作为历史，不再作为当前专项状态。

本次专项覆盖新检测/基础清理、部分修复、二进制 STL 导入、OBJ/标注及共享切割恢复相关测试。原色区面积/位置断言、共边共点不扩色、重复映射、不同朝向与重新三角化等新增用例现已通过；范围检查仍针对被面实际引用的顶点，未放宽容差。中途的非法输入/拒绝危险修复等日志主要来自负向用例；只读备份目录报错和编译器/链接器警告也仍在，不能将专项通过描述成所有运行日志均无异常或已验证旧版 macOS 兼容性。

本次由用户构建并运行，AI 仅核验反馈、文件时间及记录结果，没有额外编译或执行测试；记录此结果时尚未提交。剩余当前应用版本的 CLI 几何/导出验收、材质策略验收和新 `--check-model` 端到端脚本（见第 6 节与 AGENTS.md）；此前 17:55 的 80 项 CLI 结果不能替代。本次未验证实际 GUI、完整 CTest、其他平台或真实切片/G-code/打印。后续入库与验收安排见下节。

### 23:06 专项通过后的入库审核与本地验收交接

用户要求先审核，无阻塞问题则提交，然后由用户本地测试。本轮按此顺序作阶段性入库，不把尚未执行的端到端验收写成已通过。

- **审核范围：** 新增独立检测/基础准备/序列化复检流程、CLI JSON 与参数门禁、共享 STL 导入、分组部分修复及四类标注来源复制、共享涂色重映射、中英文提示、回归测试和正式说明。仅包含本轮 25 个源码/测试/文档文件，不提交本地 demo、构建产物、运行日志或知识图谱派生文件。
- **重点结论：** 新检测入口不会隐式进入切片；基础清理和高级修复分别保留工作副本；失败和无进展不会冒充完全修复；部分结果仍明确报告未解决问题；取消、异常和严格保色失败的事务保护保留。分组不重新居中，共享标注恢复的改动也会影响 GUI/切割等既有调用方，并非只影响新 CLI。
- **审核结果：** 当前源码与最新专项证据未发现阻止阶段性入库的问题。使用影响图辅助确定共享调用范围，再以当前源码核对；图谱早于新检测模块，不能作为新模块正确性的证明。本轮仅做静态审核、Python 语法及差异检查，没有重新编译或运行测试。
- **已有证据：** macOS arm64 / Release 的完整修复专项 149 个用例、6094 个断言通过，详见上一节。保留之前失败记录及修正过程，不隐藏失败历史，也不借用旧 CLI 结果证明新代码通过。
- **用户本地下一步：** 用当前源码构建的 OrcaSlicer 依次执行第 6 节的 CLI 几何/导出、材质策略和新检测工作流验收；重点确认零法向量 STL、双色 OBJ/MTL、部分结果重导入和再次修复无进展。GUI 导入/高级修复/撤销重做/保色导出需另行复测。若本地失败，保留脚本证据目录和 `result.json`，继续修正，暂不作为发布或打印验证通过。
