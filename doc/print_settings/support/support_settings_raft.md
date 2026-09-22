# Raft

Raft is a base layer that is printed under the object to improve adhesion and prevent warping. It consists of multiple layers of material that create a stable foundation for the print.

## Layers

Object will be raised by this number of support layers.

## Contact Z distance

Z gap between object and raft. Ignored for soluble interface.

## Raft geometry trial: segmentation and smoothing

The advanced **Raft Geometry (Trial)** group is available in Cura-style mode.
`raft_segmented` is off by default; its reference size (`raft_chunk_size`) is
40 mm and its gap (`raft_chunk_gap`) is 0.8 mm. Independent outline smoothing
(`raft_smoothing`) defaults to 0, disabled. These are opt-in trial settings,
not new printer defaults or a guarantee of easier removal.

The first version processes compatible single-instance objects during by-layer
printing with one active filament and supports disabled. It also processes an
individual large model. Different geometry settings, ordinary/forced supports,
multi-instance sources and unsupported path/layer plans retain the existing
raft behavior. If an optional candidate cannot be used, the baseline is the
existing **merged** raft, not overlapping original paths.

The geometry sequence follows the confirmed X-PRINT 5.9.6 behavior: union the
nominal outlines, smooth, apply the phase inset, split each connected outline,
then generate walls and infill. Base, interface and surface contours are
processed independently; at the threshold, the base may split while upper
layers stay continuous. Smaller islands remain uncut, long shapes use strips
and wider shapes may use radial sections. The reference size is not a maximum
section size. Cuts are not redirected around model contacts and may leave
marks on the model bottom. Fragments without generated extrusion are omitted
from island metadata without canceling the other sections.

After segmentation, isolated base components with no bearing role can also be
omitted. All base layers are projected together and checked against upper raft
solids/toolpaths and the participating models' first layers. A component must
have separated from a larger uncut region that still has a useful component;
original small islands, useful narrow sections and attached borders remain.
Both extrusion and island metadata are updated. Automated regression coverage
for this cleanup has passed; improved peeling performance still needs physical
print validation.

Smoothing performs round closing, which can fill small raft holes, followed by
opening using Orca's resolved brim line width. With smoothing zero, closing is
disabled but opening still runs when segmentation is enabled. Middle and top
contours receive a half-width inset using their resolved phase flow. These
width mappings adapt the observed geometry sequence to Orca; they do not promise
identical X-PRINT toolpaths. Printable-area exclusions and final extrusion-width
clipping remain active. Growth into another object or planned raft retains the
baseline; empty or out-of-region path results also retain it. Existing mixed-raft
clearance checks still block export. Neither feature changes model geometry,
flow, air gap, layer heights or arrangement spacing.

中文入口：**工艺 → 筏层 → 筏层形状（试用）**。分块和平滑均默认关闭；
首次对照可用分块参考尺寸 40 mm、缝宽 0.8 mm，平滑单独从 1 mm 试起。
适用范围为有效 CuraV1、逐层打印、单耗材、关闭支撑的兼容单实例对象。
各阶段独立平滑、内缩和分块；低于阈值不分块，相连轮廓按合并后的尺寸判断。
平滑可能填平筏层小孔；开启分块时，即使平滑为 0 也会清理细边。
不再额外避让小脚或因个别无路径碎片取消全部分块。切缝可能经过模型接触面。
分块后会保守清理没有上方承托用途的孤立基底组件：联合检查所有基底层，
保留有用窄条、原独立小岛及相连外扩边，同时更新路径和输出岛记录；此修订已通过专项回归。
不适用的组仍保留原合并结果，平台边界和混摆冲突校验继续生效。
此功能改变形状，易拆程度和底面缝痕必须实机判断。

Implementation and validation history are tracked in
[筏层试印版进度](raft_geometry_trial_progress.md).
The new regression filter is `[SupportMaterial][Raft][Geometry]`, included in
`[SupportMaterial][Raft]`. The September 21 13:49 macOS arm64 / Release build
passed 23 geometry cases / 1,326 assertions and the full 70 raft cases / 5,501
assertions, including three `[Bearing]` cases for base cleanup. Geometry is part
of the full raft suite; these totals are not additive. Global parameter editing,
object splitting and project save/reopen were checked in the GUI; the remaining
single-object override display check was confirmed manually by the user.
This does not establish full CTest, cross-platform or physical peeling results.
Historical build failures, corrections and exact evidence are retained in the
progress document. Source changes require a matching new build before retesting.

## Printable bed boundary

Legacy and Cura-style raft outlines are clipped to the printer's printable area,
including excluded regions, after expansion. The extrusion width is also accounted
for at the boundary. For example, a centered 98 mm object on a 100 mm bed can keep
the available raft around its edges instead of generating a 104 mm raft when a
3 mm expansion is requested. The object geometry and the final out-of-bounds
validation are unchanged; a raft that is completely removed by clipping reports an error.
Raft layers close to the boundary retain their linear paths so simplification or
arc fitting cannot move a clipped edge outside the printable area again.

Moving an instance, changing the printable area or excluded regions, or changing
the plate origin regenerates the raft. Instances sharing one PrintObject use the
area printable at every instance position. Separately placed model objects do not
reuse one another's raft/support layers. Overlapping rafts from different objects
are not merged by this boundary operation.

筏层完成外扩后会裁剪到打印平台的可打印区域，并避开禁打区；贴边路径会考虑挤出线宽。
模型尺寸和最终越界检查保持原有规则。移动模型或改变平台边界后会重新生成筏层；
本功能不处理不同对象之间的筏层合并。

The regression filter is `[SupportMaterial][Raft][BedBoundary]` in the existing
`fff_print_tests` target. After correcting the test fixture, all 6 boundary cases
and all 29 raft cases passed on macOS arm64 / Release. Evidence is recorded below.

The user's arm64 Release build in
`planning/build/macos-build-arm64-20260919-112725-172178.log` stopped on two
compile errors in `SupportCommon.cpp`: an undefined `_L` and a protected
`PrintBase::throw_if_canceled()` call. These have been corrected to use `_u8L`
and the public `canceled()` query with `CanceledException`.

The subsequent `macos-build-arm64-20260919-112914-622329.log` records successful
compilation of `SupportCommon.cpp` and linking of libslic3r and the OrcaSlicer
executable. The full build still failed in `test_support_material.cpp`; the same
three test compile errors recur in `macos-build-arm64-20260919-114331-379978.log`.
The tests now refresh slicing parameters via public `Print::apply()` and obtain a
mutable object via `Print::get_object(size_t)`. The user's subsequent
`macos-build-arm64-20260919-114358-829263.log` records successful compilation and
linking of `fff_print_tests`, followed by localization and app package preparation.
The boundary regression tests had not yet been executed at that point.

The GUI log `planning/debug/orca-user-gui-run-20260919-115212-712100.log` records
ten successful slices between 11:52:45 and 11:56:01, with successful G-code exports
and `m_contained_in_bed 1`. Seven also report trimmed raft toolpath layers. The
three validation failures at 11:53:22, 11:53:44, and 11:53:54 all concern the
existing Cura-style normal-generator constraint: Base margin must be at least
Top margin + 0.5 mm. That validation predates bed clipping (commit `80c3c70478`);
it also applies when support is disabled but that generator is used for the raft.

Additional GUI verification on the same running arm64 Release application
reproduced the Base 1 / Top 1 mm error, then successfully sliced with Base 1.5 /
Top 1 mm. A centered 98 mm cube on the 100 mm bed with Base 10 / Middle 1 / Top
1 mm margins also sliced and exported successfully. All five raft layers were
checked from the generated linear G-code paths, including half the declared
extrusion width at each edge: their combined XY footprint remained within
0.001–99.999 mm. The original 90 mm cube and Base 10 mm setting were restored
and sliced again. Evidence is in `planning/debug/raft-bed-boundary-gui-20260919/`.
This verifies the exercised rectangular-bed GUI cases; it does not establish
automated regression, circular/excluded-region bed, multi-instance, or physical
printing acceptance.

The final wording changes were rebuilt in
`planning/build/macos-build-arm64-20260919-133913-873442.log`: the application and
`fff_print_tests` were linked again, and the corresponding build task log records
exit code 0. The GUI log `planning/debug/orca-user-gui-run-20260919-134154-831433.log`
confirms the updated raft-specific validation message at 13:42:11. This GUI run
records parameter validation only, not a new successful slice. Automated raft
test execution was still pending at that point; the successful build does not replace it.

The user subsequently authorized raft test execution. Using that arm64 Release
test binary, `[SupportMaterial][Raft]~[BedBoundary]` passed all 23 test cases and
1,670 assertions (exit code 0). The new `[SupportMaterial][Raft][BedBoundary]`
subset failed all 6 test cases with 34 failed assertions (exit code 34), all at
the fixture's initial `Print::validate()`: its default relative-extrusion Marlin
configuration lacked the required per-layer `G92 E0`. None of those cases reached
raft generation, so this is neither boundary acceptance nor evidence of a
clipping failure. The fixture now supplies `before_layer_change_gcode = G92 E0`,
as the existing raft tests do; the validation assertion remains enabled and
production code is unchanged by this correction. Evidence and the tested source
snapshot are in `planning/raft/validation-20260919-135024/`. Those failed results
do not establish boundary acceptance; the subsequent verification does.

With user authorization, `fff_print_tests` was incrementally rebuilt in the same
`build/arm64` Release configuration (exit code 0), compiling the corrected fixture
and linking the test executable. `[SupportMaterial][Raft][BedBoundary]` then
passed all 6 test cases and 542 assertions; the complete
`[SupportMaterial][Raft]` filter passed all 29 test cases and 2,212 assertions.
Both executions returned 0. The complete total includes the boundary subset;
the two counts must not be added together. Commands, build/test logs, binary
SHA-256, HEAD and the tested source snapshot are preserved in
`planning/raft/validation-20260919-135444/`.

The boundary cases cover Legacy/Cura modes across Grid, Snug, Organic, Slim,
Strong and Hybrid styles, with and without phase walls; a 98 mm model on a
100 mm bed; circular beds and excluded regions with arc fitting on/off; other
plate origins; position and bed changes; cached-model-layer processing; multiple
instances and separately placed mesh copies; explicit failure when no raft area
remains; and the no-raft tree-support path. Together with the recorded GUI
verification, this completes the focused raft boundary acceptance on this
platform. Full CTest, other platforms and physical printing were not run.

## Overlapping rafts from independent objects

During print-by-layer slicing, independently imported/split objects now share
raft regions where their rafts overlap. Their meshes, placement, model layers,
contact gaps and arrange/plate-splitting distances remain unchanged. This pass
runs after support generation and before tool ordering and G-code export.

Cura raft layers with the same effective heights, material and extrusion recipe
are unioned and filled once. Automatic and explicit values are compared after
resolution; each object's expansion contributes its own outline to the union.
Different recipes (for example width, flow, direction, speed or material) retain
their settings and partition the overlapping area. Ownership follows a stable
geometric order, so changing the order of incoming OBJ files does not choose a
different recipe. No parameter-difference dialog or warning is added. Extrusion
width is included when clipping both the partition boundaries and the bed.

This is a minimal overlap treatment, not a new common raft-layer planner. If
physical layer schedules differ, their layers are not resampled: regions whose
physical Z intervals overlap are assigned to the lower layer first. Legacy,
mixed support/raft paths and caches without outlines retain their original paths
and only have overlaps trimmed. Multi-instance PrintObjects and print-by-object
mode keep their existing path; the new pass targets the independent OBJ objects
in the current workflow. Object contact gaps are not automatically reconciled.

Original per-object paths are restored before reprocessing and are the paths
written to slicing caches. Moving/removing a neighbour therefore cannot reuse its
former shared raft. Shared raft paths are excluded from individual-object cancel
labels; model paths retain their own labels. Cancelling one leg must not cancel
the raft supplying the remaining legs.

逐层打印时，多个独立对象的重叠筏层会统一处理：实际参数一致的区域求并集后生成一次；
线宽、流量、方向、速度或耗材不同的区域保留各自设置，交叠部分按固定几何顺序分配，
避免重复挤出，不增加参数差异提示。模型层高、气隙及分盘规则不变；平台裁剪继续生效。
这不是跨层高统一重建，也不包含同一对象多个实例的共享路径改造。

New regression cases use `[SupportMaterial][Raft][Merge]` in `fff_print_tests`:
four independent legs plus a separate tabletop across support styles; effective
Auto/explicit equivalence; differing margins/gaps/recipes/materials; repeated
slicing, moves and deletion; cache export/reload; excluded regions and translated
plates; G-code object-cancellation labels; and physical Z overlap in conflict
detection. GUI preview and physical printing remain separate verification.

### 2026-09-20 mixed raft clearance (automated raft regression passed)

`Print::validate_raft_clearance()` checks print-by-layer plates containing both
rafted and unrafted objects. It runs after path simplification and generated
brims/skirts, and again before G-code export creates a file. A single raft is
checked too: the check does not depend on a shared-raft group being formed.
It uses the final extrusion widths and each path's physical height, rather than
nominal raft margins or intersecting centre lines. Shared-path recipients and
merge backups are not counted as extra deposited material. Sparse raft gaps and
model holes remain free; touching Z intervals do not count as physical overlap.

The unrafted side includes model slices and emitted model paths, ordinary
support, generated model/support brims, and active per-object skirts. Model and
support paths are transformed for every instance; brim maps already contain the
plate offset and instance placement. Support paths that share a physical raft
layer are checked with that raft's low-layer paths. A collision raises the same
translated `SlicingError` for GUI and CLI, names both objects, and identifies the
unrafted object. It neither moves objects, clips away their required support,
enables a raft nor changes their process parameters. This check uses real Z
intervals and also runs when the older line checker is skipped for adaptive
layers or `--no-check`.

逐层打印时，开启／关闭筏层混摆增加实际占地校验；只有真实重叠才报错。
英文和简体中文提示同步维护。摆盘、筏层参数兼容、逐对象打印及多实例筏层
合并策略不变；擦料塔专项校验和自定义起始 G-code 不在本次改动范围内。

New tests use `[SupportMaterial][Raft][Clearance]`, included in the full
`[SupportMaterial][Raft]` filter. They cover a single raft versus an unrafted
model across Legacy/Cura and support styles; merged ownership; moving and
re-slicing after a rejection; width-only intersections; path heights differing
from the support layer height; model holes; empty/non-extruding paths; brim Z
and translated plates; a non-first unrafted instance; generated per-object
skirts; unchanged all-off/ByObject paths; cancellation; and rejection before
direct G-code export creates a file.

Validation: the user's actual build log is
`planning/build/macos-build-arm64-20260920-142033-391636.log` (the supplied
`142603-005329` filename was not present). It compiled the changed production
and test sources and linked OrcaSlicer and `fff_print_tests` successfully.
The checkout was `master` at `12a8ca0199` plus the uncommitted clearance patch,
using `build/arm64`, Ninja Multi-Config, Release and arm64. Source hashes matched
the pre-build static review; the test binary was newer than the changed sources.

- `[SupportMaterial][Raft][Clearance] --reporter compact`: **9 test cases,
  145 assertions passed**, exit code **0**.
- `[SupportMaterial][Raft] --reporter compact`: **47 test cases,
  4,171 assertions passed**, exit code **0**. This includes the clearance cases;
  do not add the two sets of totals.
- `git diff --check` and the simplified Chinese catalog's
  `msgfmt --check --check-format`: passed, exit code **0**.

Commands, logs, platform, source hashes, binary hash and exit codes are in
`planning/raft/clearance-validation-20260920-143113/`. No production or test
source changes were needed after this run. The build still reports toolchain
warnings, including linking a zstd dylib built for macOS 26.0 while targeting
11.3; passing on the current machine does not establish older macOS compatibility.
GUI and CLI command end-to-end error handling, physical printing, full CTest
and other platforms were not exercised in this run. The GUI records below
remain evidence for the previous build only.

The subsequent pre-commit review used the same source and binary hashes and
added four CLI comparisons, retained in
`planning/raft/clearance-review-20260920-merge-ready/review.json`:

- A raft overlapping an elevated unrafted object's support was rejected.
- Moving that support clear of the raft permitted a nonempty G-code export.
- Enabling a per-object skirt that reached the raft was rejected, although the
  model and support were clear. This did not reproduce the suspected omission
  when checking skirts on an object supported above the bed.
- Moving the unrafted object farther away, keeping support and skirt enabled,
  permitted a nonempty G-code export.

Both rejections reported CLI return code `-100` (process exit code `156`), named
both objects in the diagnostic log, and created no G-code. Both successful
exports returned `0` and had empty plate warning messages. These are synthetic
CLI smoke cases, not GUI or physical-print acceptance. No blocking issue was
found in this review; production and test sources remained unchanged.

### 2026-09-19 overlap validation

The user's arm64 Release build in
`planning/build/macos-build-arm64-20260919-153428-752403.log` completed all 629
steps, including the new merge implementation and `fff_print_tests`. On that
fresh binary, `[SupportMaterial][Raft][Merge]` failed 7 of its 8 test cases,
with 20 failed assertions (exit code 20). The existing
`[SupportMaterial][Raft]~[Merge]` subset passed all 29 cases and 2,212 assertions
(exit code 0). This is **not acceptance of the overlap feature**. Commands,
logs, source snapshot and binary hash are retained in
`planning/raft/merge-validation-20260919-153927/`.

Investigation identified two production defects: the ZigZag filler's bounding
box expects coordinates centered at the origin, whereas shared rafts were
generated directly in plate coordinates; and conflict detection compared paths
in XY even when their physical Z intervals did not overlap. The fixes center the
shared fill frame and translate the result back before bed clipping, and retain
physical Z intervals in conflict checks. Actual same-height intersections still
use the existing geometric checks. Two fixture defects were also corrected:
tree support layers above the raft were included in raft-only assertions, and
a percentage option was assigned as a float. A Z-overlap regression was added,
including a positive case that must still detect a genuine intersection.

The user then rebuilt arm64 Release. The matching local build log is
`planning/build/macos-build-arm64-20260919-160153-075775.log` (completed at
16:02:27; the separately mentioned `160227-507402.log` filename was not found).
It records compilation of the corrected merge, conflict-check and test sources,
followed by the `fff_print_tests` link. The fresh test binary passed
`[SupportMaterial][Raft][Merge]`: **9 cases / 1,814 assertions**, exit code 0.
The complete `[SupportMaterial][Raft]` regression then passed **38 cases / 4,026
assertions**, exit code 0. The full total includes the merge subset; do not add
the two counts together. Commands, logs, source snapshot, HEAD and binary SHA-256
are preserved in `planning/raft/merge-validation-20260919-160440/`.

This completes the focused automated raft regression for this arm64 Release
build, including the previous boundary tests and the new overlap cases. No
further production changes were made after that build during this verification.
Full CTest, other platforms and physical printing have not been verified for the
overlap feature. The subsequent GUI checks are recorded below.

### 2026-09-19 GUI overlap validation

Computer Use verification used the freshly built macOS arm64 Release app and
`planning/debug/orca-user-gui-run-20260919-160658-152072.log`. Five separate
printable objects (four 8 mm legs and a distant 15 mm tabletop) were loaded from
a core 3MF fixture containing five independent build items. This verifies the
independent-object slicing case, not the OBJ splitting/import UI. The test used
by-layer printing, support disabled, Cura raft margins 5/4/3 mm, stage counts
1/2/2, and automatic resolved heights 0.3/0.3/0.2 mm.

All three GUI scenarios sliced and exported successfully:

- Four close legs shared a continuous raft; the distant tabletop retained its
  own raft. Base, middle and top previews were inspected.
- Moving one leg from X=27 to X=60 and re-slicing produced a shared L-shaped
  raft for the remaining three legs and a separate raft for the moved leg.
- Restoring the leg and shifting all objects by X=-22 put the leftmost model
  edge at X=1 mm. The expanded shared raft was clipped at the left bed boundary
  in the base, middle and top previews. The saved preset's printable polygon
  is 0..101 mm in both axes, despite the bed texture saying 100 mm; this test
  concerns the left X=0 boundary.

The matching log entries report `[Raft][Merge] groups=1, layer_regions=5`,
completed conflict checks and slicing completion status 0. Auditing all three
GUI-exported G-code files confirmed five raft Z levels (0.3/0.6/0.9/1.1/1.3 mm),
no exactly duplicated extruding segments, and raft envelopes including half
the annotated line width within the configured bed. In the edge case the
minimum envelope X was 0.301 mm at the base and 0.001 mm in the other stages.
This export audit is not a general bead-intersection proof and excludes custom
start/end G-code; the geometric regression remains the automated suite above.

Projects, G-code, seven preview images, the read-only audit script and results,
log excerpts, and file hashes are in
`planning/raft/gui-merge-validation-20260919-1607/`. The GUI was restored to the
saved shared-raft baseline and re-sliced successfully. No production code was
changed, no preset was saved globally, and nothing was sent to a printer.
Different object recipes, other support generators and deletion/cache cases
were covered by automated tests, not repeated in this GUI session.

Two subsequent user-arranged scenes were also inspected in the same GUI build:

- With the 5/4/3 mm margins, arranging increased the adjacent leg gaps from
  3 mm to 10 mm. The base layer was continuous, while the top layer retained
  separate regions and narrow gaps. The pass unions each physical layer's
  intersecting regions; it does not fill empty space between disconnected
  regions. The project and exports are `04-arranged.3mf` / `04-arranged.gcode`;
  single-layer screenshots and export audit are retained alongside them.
- Adding a Benchy and changing margins to 3/2/1.5 mm left the boat raft separate
  from the nearby blocks, while some block rafts connected. All six objects had
  the same raft settings. Arrangement uses the full model projection plus an
  estimated raft allowance; raft generation with supports disabled uses the
  bottom contact outline. The boat's narrower bottom therefore leaves a gap
  that the boxes do not. The first-layer preview and G-code confirm this
  distinction; artifacts are `05-boat-arranged.3mf`, `05-boat-arranged.gcode`
  and `10-boat-arranged-base.jpg` in the same evidence directory.

Both scenes logged a completed merge and slicing status 0. The pre-commit review
confirmed that all 13 changed production/build/test files still match the
SHA-256 snapshot of the successful 16:04 automated validation; only documentation
was updated afterward. `git diff --check` passed. Within the independent-object,
print-by-layer scope above, no blocking issue was found. This does not extend
acceptance to multi-instance sharing, arbitrary layer-plan reconciliation,
other platforms or physical printing. The review itself did not rebuild,
repeat unchanged tests, or create a commit.

### 2026-09-21：混摆的摆盘占地预留（待新构建验证）

用户的三方块场景先分盘再切片仍冲突。旧摆盘仅给开启筏层对象预留名义外扩，
没有计入普通支撑生成筏层接触轮廓时的网格扩张，关闭侧的 Brim 也未完整预留。
关闭合并和分块后，单个筏层仍可复现同类冲突，因此本次修正摆盘的预估，
不改变筏层生成、合并、分块、平滑及最终实际路径冲突校验。
分析证据：`planning/raft/arrange-mixed-review-20260921-165615/review.md`。

`prepare_mixed_raft_clearance()` 在 GUI 自动摆盘／铺满热床、CLI 普通／组装盘摆盘中
统一处理可移动和固定对象，只在逐层打印且候选集合中同时存在有效 CuraV1 和
关闭筏层对象时加强占地预留。全部开启、全部关闭、纯 Legacy 及逐对象打印保持旧规则。

| 对象一侧 | 混摆时的预留 |
| --- | --- |
| CuraV1 | 原筏层／支撑余量，加普通支撑网格或紧贴轮廓的扩张估计、阶段最大半线宽及 0.05 mm 数值间隙 |
| 关闭筏层 | 自身已有安全／支撑余量，加外侧 Brim 和 Brim 间隙、线宽余量；有普通支撑时补其网格扩张，有逐对象裙边时补其距离和宽度 |
| 活动 Legacy | 切片时可能使用 Cura donor，分盘前按候选 donor 中较大的占地保守预留；保持原保存配置 |
| 擦料塔／禁打区 | 不计为关闭筏层的模型，沿用原障碍区规则 |

普通支撑生成器与摆盘共用 `SupportGridParams` 的网格尺寸、接触外扩和紧贴半径，
使用网格对角线为后续旋转预留空间。此处移动参数定义没有改变生成器的几何行为。
筏层阶段宽度继续通过 `resolve_cura_raft_plan_config()` 解析，包含“自动”线宽。
`minimum_inflation` 使较小的用户间距及适床缩小逻辑不能覆盖必要占地。

这仍是切片前的保守估计，不是已经生成的实际轮廓。混摆候选集合中开启筏层的对象
之间也可能排得更开，自动 Brim 采用生成器的 18 mm 上限，可能减少单盘容量。
本次没有引入两段式摆盘、改变 libnest2d 或在摆盘时提前完整切片。复杂支撑、
手动摆放以及后续参数变化仍由切片／导出阶段实际路径校验兜底。
铺满热床的混摆使用正常碰撞约束路径；实例较多时可能比快速格点路径慢，尚未测量耗时。
多盘位置回写依据实际使用的排列路径，避免超过 100 个候选后仍按格点坐标处理。

新增两个测试入口（用户重新编译相应目标后运行，当前尚未运行）：

- `libslic3r_tests '[Arrange][Raft][Clearance]' --reporter compact`：混摆范围、固定双方、较小手动间距、重复准备，以及原有策略不变。
- `fff_print_tests '[SupportMaterial][Raft][Clearance][Arrange]' --reporter compact`：12 个数据场景执行真实摆盘→切片→实际冲突校验；包括原三方块负例、Brim 开关、Grid／Snug／Organic／Slim、100／180 平台、旋转与线宽、固定双方、复制实例、活动 Legacy、真实悬挑支撑、逐对象裙边；基础场景还导出非空 G-code。

聚焦通过后再运行已有 `[Arrange][Raft]` 和完整 `[SupportMaterial][Raft]`，统计不重复相加。
本轮仅源码审阅、改动范围的 clang-format 和 `git diff --check`，未编译、未做新版本
运行／GUI／实机验证。下一步是用户构建 OrcaSlicer、libslic3r_tests、fff_print_tests，
再复测 GUI 分盘→切片及第二盘大于 100 个候选实例的铺满流程。

Mixed Cura V1 / raft-off arrangement now reserves contact-grid growth and the
unrafted object's own brim, support and per-object skirt footprint before packing.
Both movable and fixed instances participate; explicit small distances cannot
lower the reservation. Uniform jobs keep the previous policy. This conservative
estimate may reduce packing density and does not replace post-slice physical
clearance validation. Source changes and regression scenarios are pending a fresh
user build; no new runtime or print-quality acceptance is claimed.

18:14 构建反馈：实际日志 `planning/build/macos-build-arm64-20260921-181245-230990.log`
在 `ModelArrange.cpp` 报出私有配置解析函数不可访问，以及 const 对象不能调用未加
const 限定的配置查询两处错误。已把原有只读配置解析函数公开供摆盘复用，并将
`ModelObject::get_config_value()` 标记为 const；没有复制解析实现或改变配置语义。
本轮未编译／运行测试，仍需用户重新构建后执行上述专项。
静态证据：`planning/raft/arrange-mixed-fix-20260921/build-fix-static-review.json`。

18:29 再次构建反馈：`planning/build/macos-build-arm64-20260921-181414-067009.log`
已通过上述两处接口修改的编译，但新增 FFF 测试中的 Eigen 向量三元表达式发生
对齐类型转换歧义。已改成显式 Vec3d 初始化后按条件赋值，保留原固定对象位置断言；
本轮未修改生产源码。构建仍未完成，未运行测试，需重新运行原构建命令后验证。
静态证据：`planning/raft/arrange-mixed-fix-20260921/build-fix-2-static-review.json`。

18:46 用户新构建后验证：源码哈希与 build-fix-2 快照一致。摆盘聚焦 1 用例／27
断言、完整 `[Arrange][Raft]` 8／62 通过；真实摆盘→切片聚焦及完整 Raft 均失败，
同一 scenario=7（固定开启筏层的对象，移动其他对象）仍触发关闭侧冲突，退出码 1。
该失败尚未定位根因，当前不能宣称混摆修正已经验收；保留断言和原始失败日志。
证据：`planning/raft/arrange-validation-20260921-1845/validation.json`。

用户同版 GUI 日志 `183258-989344` 的 run=8/9 只有一个 98 mm 方块，
摆盘返回 unarranged=1，没有进入新增混摆分支。原有 Cura V1 最小外扩同时用于
对象避让和平台容纳，导致完整筏层放不下时把模型移至盘外；切片阶段的平台裁剪
仍存在。这两条规则需要区分：保留模型／真实支撑的合法占地和对象间避让，允许
纯筏层外扩在平台边缘裁剪，不能通过全局删除最小间距来修复。

支撑投影与邻近筏层接触不等于实际挤出冲突：同组有效筏层底部按物理 Z 合并或
裁剪去重，高于筏层的支撑路径保持原流程；未开筏层对象的支撑和 Brim 仍由实际
冲突校验拦截。此次 18:38:53／18:39:08 日志为底部合并一组、9 个层区域，分块因
不同层／工具计划回退。没有独立的“筏层到 Brim 净间隙”参数；Brim 宽度及其到
模型的间隙描述关闭侧占地，摆盘对象间距与双方占地预留共同决定最终净间隙。
本轮未修改生产或测试源码，未编译，未实机打印。

21:04 追加定位：用户 `204609-379627` GUI 日志的 run=4 在 20:53:31
再次以 movable=1/fixed=0、unarranged=1 拒绝单对象；本次配置原始范围为
0..101 mm，边距各 1 mm 后摆盘范围为 1..100 mm。完整筏层预留仍同时约束
平台容纳，尚未落实允许纯筏层外扩贴边裁剪的摆盘规则。前三次摆盘成功，
四次均 fixed=0，不能把这次 GUI 记录描述为固定对象冲突复现。

scenario=7 的含义是三个 10.1 mm 方块中固定一个开启筏层的方块，只移动另两个
（其中一个关闭筏层并带 2 mm Brim）。已有代码为筏层侧预留 7.60735 mm、
关闭侧预留 3.25 mm，固定对象也参与预留。LLDB 将现有测试日志级别设为 debug
后仍复现实际冲突，进程码 1：固定方块中心保持 (56.55,58.55)，关闭侧摆盘
输出从放置时约 (76.5373,78.4430) 变为末尾对齐后的 (53.7908,42.1419)。
底层 `nfpplacer.hpp::finalAlign` 的最终平移只排除虚拟对象，没有排除真实固定
对象；临时摆盘形状中的固定对象也被移动，而 `Arrange.cpp` 只回写移动对象。
因此固定对象的内部位置与实际模型位置失配，后续对齐／避让会使用错误参照。

对照仅在第八次 `fill_config`（scenario=7）的调试进程内将 `do_final_align`
置 false，其余配置、预留、断言和源码不变；12 场景所在的用例通过 138 断言，
测试进程码 0。该对照支持修正末尾对齐，而不是继续扩大间距；建议在应用层对
存在真实固定对象的摆盘禁用整体对齐，后续须验证固定双方、多盘和 GUI 选中摆盘。
这里记录的是诊断结果，未修改生产／测试源码，不代表修正已交付或正式回归通过。
证据在 `planning/raft/arrange-diagnosis-20260921-2100/diagnosis.json`。

### 2026-09-21 平台边缘与固定对象修正（待新构建）

本轮在 `codex/raft-geometry-review` 的现有混摆改动上落实两项修正：

1. **允许纯筏层外扩贴边裁剪**：先按完整预留正常摆盘；仍未排入的 CuraV1 对象，
   若没有开启支撑、强制支撑层、支撑绘制／强制体或裙边，再在空矩形盘尝试居中
   放置。模型投影必须完整位于扣除平台边距后的范围；对象间 inflation 和
   minimum_inflation 保持不变，外扩筏层由既有切片边界裁剪处理。
2. **保留固定对象的坐标参照**：只要存在真实固定对象，共享摆盘入口就禁用末尾
   整体对齐，非默认 align_center 也不能重新开启它。平台边缘外扩的固定对象不再
   被直接丢弃；结果仍按实际固定对象轮廓复核，防止底层重试临时清除障碍后输出
   冲突位置。检测使用与底层一致的 jtSquare 转角和固定对象数值容差，避免斜角
   假冲突；移动轮廓缓存一次，补放过程检查取消。

第一项是保守的补放策略：每个补放盘只放一个对象，有真实固定对象或擦料塔的盘
不作为空盘，禁放区继续避让，模型本体超限／高度超限不放行。圆形或非矩形平台、
Legacy、逐对象打印、带支撑或裙边仍保留原容纳规则。它解决近满幅模型因纯筏层
外扩被拒绝的问题，不追求把小对象填进贴边筏层的剩余空隙，可能多用一盘。
若最终固定对象复核发现冲突，则该对象按未排入处理，不能将冲突结果标成成功。
未放宽切片阶段的筏层／未开筏层对象实际冲突校验，也不新增界面参数。

已新增回归定义：

- `libslic3r_tests '[Arrange][Raft][BedClip],[Arrange][Raft][Fixed]' --reporter compact`：
  100／180 mm 平台、98／178 mm 模型、多个大模型分盘、固定占盘、模型本体超限、
  支撑／裙边限制、Legacy／逐对象保护、禁放区、取消、固定对象及自定义对齐中心。
- `fff_print_tests '[SupportMaterial][Raft][Clearance][Arrange],[SupportMaterial][Raft][BedClip][Arrange]' --reporter compact`：
  保留原真实冲突断言，混摆从 12 增至 13 个数据场景；新增两种平台 × 分块开关的
  摆盘→实际切片→按挤出线宽检查筏层不越界，不能只以模型坐标或切片完成判定通过。
- 新构建后的完整回归仍用 `[Arrange][Raft]` 和 `[SupportMaterial][Raft]`，聚焦数字
  包含于完整数字，不重复相加；GUI 需再验证原 98 mm 模型与选中部分对象摆盘。

**当前仅完成源码和测试定义，未编译／未运行上述用例。** 用户需重新构建应用及
`libslic3r_tests`、`fff_print_tests`；旧二进制和调试器内对照不是本轮验收结果。
静态快照：`planning/raft/arrange-bed-fixed-fix-20260921/static-review.json`。

### 2026-09-21 21:45 用户构建后的验证与新边界反馈

上述“待新构建”状态的相应自动化范围现已完成：构建日志
`planning/build/macos-build-arm64-20260921-212842-083419.log` 包含核心改动与测试的
编译／链接；源码哈希匹配静态快照，打包与链接应用一致。未由 AI 编译。

| 验证范围 | 用例／断言 | 退出码 |
|---|---:|---:|
| 摆盘边界与固定对象聚焦 | 4／69 | 0 |
| 真实混摆及边界摆盘→切片聚焦 | 2／217 | 0 |
| 完整 `[Arrange][Raft]` | 12／131 | 0 |
| 完整 `[SupportMaterial][Raft]` | 72／5,718 | 0 |

聚焦包含于完整统计，不相加。固定开启／关闭筏层对象、自定义对齐中心及原
scenario=7 均使用正常新构建程序通过，无调试器干预。这属于自动化验证，
尚未操作 GUI 验证选中部分对象／铺满热床，也不代表实机或完整 CTest 通过。
部分聚焦日志记录测试资源 filament_info.json 缺失，未覆盖耗材温度兼容性。

用户同次 GUI 日志 `213208-698364` 的 run=6/7 在关闭 Cura 后未排入，run=8
开启 Cura 后进入补放并成功；均 fixed=0。使用同版 CLI，在 0..101 mm 平台、
各边距 1 mm、关闭支撑／Brim／裙边的合成方块上复现如下：

| 模型尺寸 | 筏层 | 摆盘结果 |
|---|---|---|
| 98.9 × 98.9 × 1 mm | 关闭 | bed=0，中心约 (50.5,50.5) |
| 99 × 99 × 1 mm | 关闭 | bed=-1，移至盘外，导出位置 (99,-20.2) |
| 99 × 99 × 1 mm | CuraV1 | 补放到 bed=0，中心 (50.5,50.5) |

这是普通摆盘的临界遗漏：可用宽度 99 mm，普通对象仍有单侧 1 mm 的自动预留。
`update_selected_items_inflation()` 仅在 `bed_size - model_size - 5` 两轴都大于
零时缩小预留；恰好 99 mm 时跳过（这里的 5 是整数坐标单位，不是 5 mm），
因而按 101 mm 占地判断。之前新增的补放仅用于 CuraV1，关闭筏层后不会触发。
应在后续修正纯模型的临界容纳，不能连 Brim／支撑等真实占地一起放宽。

**该关闭筏层临界问题本轮仅定位，尚未修改生产或测试源码。** 上表三次 CLI
都返回 0，不能以此宣称全部放置成功；诊断使用真实摆盘结果和坐标。完整证据
在 `planning/raft/arrange-validation-20260921-2145/validation.json`，三组对照在
其 `raft-off-boundary/diagnosis.json`。

### 2026-09-21 关闭筏层的临界容纳修正（方案一，待新构建）

用户选择先修正本来能放入的模型，不新增“放不下也居中留盘”的参数。本轮扩展
已有空盘补放：正常摆盘未排入、有效配置明确关闭筏层／支撑／强制及手动支撑／
Brim／裙边的 FFF 模型，可单独居中放入一个空矩形盘。资格按对象覆盖后的配置
判断，自动／绘制 Brim 即使配置宽度为 0 也不作为无附着模型放行。

补放仍要求整个模型投影位于扣除平台边距后的范围，并检查高度、禁放区和取消。
有真实固定对象或擦料塔的盘不作为空盘；多个实例各自独占空盘。对象间的
inflation／minimum_inflation 不变，只允许纯模型的间距预留超出盘边；没有新增
模型路径裁剪。CuraV1 的已有筏层裁剪保持原规则，开启 Legacy、带附着物、逐对象
打印以及非矩形平台仍不进入这条新路径。当前盘摆盘若仍需多盘，继续保留原位置。

因此，对日志里的 101 mm 平台、各边距 1 mm，99 mm 纯模型应进入补放；100 mm
仍不满足 99 mm 可用范围。100／180 mm 平台对应临界模型为 98／178 mm，未更改
打印机尺寸或边距参数，也不把真正超尺寸的模型强制标成放置成功。

新增回归定义包括：

- 100／101／180 mm 平台的满可用尺寸模型，多实例分盘、固定占盘，以及自动／
  显式对象间距不变。
- 100 mm 超可用范围、限高、支撑／强制／手动支撑、Brim／自动 Brim、裙边、
  禁放区、取消、非 FFF 和逐对象打印负例；对象级关闭覆盖全局开启的正例。
- 98／99／178 mm 无筏层模型实际摆盘后切片，检查无支撑层、非空模型挤出及
  按线宽计算的挤出范围仍在平台内。

新增用例沿用 `[Arrange][Raft][BedClip]`、`[SupportMaterial][Raft][BedClip][Arrange]`，
包含于原完整 `[Arrange][Raft]` 与 `[SupportMaterial][Raft]`。此前“Legacy 不补放”
负例已明确 `raft_layers=3`，区分开启旧筏层与 Legacy 模式下关闭筏层。

**本轮未编译、未运行新 C++／CLI／GUI 验证。** 需要用户重建 OrcaSlicer、
`libslic3r_tests` 和 `fff_print_tests` 后运行专项，并复跑上一节的 98.9／99 mm
关闭筏层及 99 mm Cura 对照，另加 100 mm 负例。仅以真实盘号、模型坐标、切片
结果及路径边界判定，不以 CLI 进程码 0 单独判为摆盘成功。
静态快照：`planning/raft/arrange-unrafted-fit-20260921/static-review.json`。

### 2026-09-22 新构建复查：Brim 临界反馈与测试夹具修正

构建日志 `planning/build/macos-build-arm64-20260922-085446-447483.log` 完成 macOS
arm64 / Release 应用及两测试目标。执行前 15 个源码／测试文件哈希匹配上一节
静态快照，打包与链接应用哈希一致，AI 未编译。

用户 GUI 日志 `085825-833287` 有 11 次分盘：8 次全部排入，3 次单对象未排入。
前三次双对象摆盘正常，CuraV1 空盘补放成功；09:08:39／09:09:09 关闭筏层、Brim
后进入 `unrafted model bed fit candidate` 并成功。09:08:20 在缩放操作后未排入；
09:08:51／09:09:02 是修改 Brim 后未排入。4 次切片结束并加载预览，未出现此前
参数页“当前选择为空”的错误；所有分盘 fixed=0，不覆盖固定对象 GUI 验证。

同版 CLI 使用已有合成方块及实际机器／工艺／耗材配置复测：

| 模型 | 筏层／Brim | 摆盘结果 |
|---|---|---|
| 98.9 mm | 均关闭 | 正常入盘 |
| 99 mm | 均关闭 | 空盘补放成功，中心 (50.5,50.5) |
| 99 mm | CuraV1 | 筏层贴边补放成功，中心 (50.5,50.5) |
| 100 mm | 均关闭 | 未排入，保留 99 mm 可用范围限制 |
| 99 mm | 无筏层，内外 Brim 2 mm | 未排入，与 GUI 一致 |

本次机器配置为 **101 × 101 mm**，各边距 1 mm 后可用范围为 99 × 99 mm。
后两例虽然 `bed_idx=-1`、坐标位于盘外，CLI 仍返回 0，输出的 plate_index 也为 0；
这是已有结果判定缺口，不能只看返回码／plate_index 宣称摆盘成功，本轮未修改。

**Brim 分盘与切片的边界规则尚未对齐。** 新补放只接纳关闭所有附着物的纯模型，
开启 Brim 后回到普通膨胀容纳判断。切片端 `Brim.cpp` 已把 Brim 区域与平台外区域
做差，所以先关闭 Brim 分盘、再开启 Brim 可以切片；原设定的 2 mm 宽度不一定
完整保留。这条限制来自摆盘资格，不是筏层分块／合并。

当前 GUI 工程副本的对象覆盖为 `raft_mode=legacy`、`brim_type=outer_and_inner`，
全局的 CuraV1／no_brim 不是该对象有效值。09:09:16 的 G-code 有 16 段 Brim 挤出，
Z=0.24 mm、线宽 0.5 mm。中心线路径范围 [0.224,100.776] mm，预览判定在床内；
按标称线宽计算的包络为 [-0.026,101.026] mm，各边约超出配置平台 0.026 mm。
原因是区域先裁到平台，再按半个 `flow.spacing()` 内缩中心线；spacing 小于 width，
没有达到半线宽内缩的保证。这是路径几何核查，不是实机测量。
后续应先补 Brim 的线宽边界保证，再针对可裁剪 Brim 放开空盘补放；保持模型本体、
高度、禁放区和对象间预留及混摆冲突校验。本轮仅定位，未修改生产代码。

**本次自动化专项未通过，不能宣称整轮验收完成：**

| 测试 | 本次失败 | 退出码 |
|---|---:|---:|
| `[Arrange][Raft]` | 3 用例／18 断言 | 18 |
| `[SupportMaterial][Raft]` | 1 用例／3 断言 | 3 |

失败集中于新增无筏层测试：`full_print_config()` 不带机器的 `printer_technology`，
新资格判断要求明确 FFF，因此合成配置没有进入补放。真实 GUI／CLI 的机器配置
带 FFF，同版正例已验证生效。已在两个新增测试配置显式补齐 FFF，保留非 FFF
负例和原断言，未放宽生产门禁。修正后的测试未编译／运行；需用户更新
`libslic3r_tests` 与 `fff_print_tests` 后复跑上述完整过滤器，旧记录不能替代。

其余日志包括启动时预设别名重复、用户耗材父预设／旧工程备份缺失，以及设备页
video 初始化异常；未见这些错误中止本次分盘／切片，不等于相应功能已验收。
未做实机、完整 CTest、其他平台或新的固定对象 GUI 验证。证据、二进制哈希、
CLI 对照、GUI 副本和失败日志：`planning/raft/arrange-brim-review-20260922/review.json`。

### 2026-09-22 Brim 临界容纳与线宽边界修正（待新构建验证）

本轮接续上一节定位，修正指定宽度 Brim 开启后不能空盘补放的问题，以及中心线在
平台内、标称线宽仍略微越界的问题。源码和回归定义已更新，尚未编译或运行验证。

**分盘规则：** 逐层打印、关闭筏层／支撑／裙边的 FFF 对象，使用外侧、内侧或
内外侧 Brim 时，正常摆盘失败后可尝试在空矩形盘居中放置。读取对象覆盖后的
有效参数，模型投影仍必须完整位于扣除平台边距后的范围内，且满足限高。
每个补放对象独占一盘；固定对象或擦料塔占用的盘不算空盘。自动 Brim、自动／
绘制耳朵、带支撑／裙边、逐对象打印及非矩形平台不新增该补放资格。

正常对象间的间距算法和混摆冲突校验保持原样。仅空盘补放的避障检查额外预留
外侧 Brim 宽度、Brim 到模型的间隙及半线宽；内侧 Brim 不增加外侧预留。
该预留与原 inflation 取较大值，避免为补放越过禁放区。对于实际配置
101 × 101 mm、各边距 1 mm 的机器，99 mm 模型加指定宽度 Brim 应能补放；
100 mm 模型仍超出 99 mm 可用范围，不因 Brim 裁剪而放行。此为新代码预期，
需新构建复测，不是已通过的结果。

**切片规则：** 模型 Brim 和支撑 Brim 共用以下边界处理（包括自动／耳朵模式
实际生成的 Brim）；这些模式的摆盘资格仍分别按上一段限制：

1. 使用当前 `Print` 的平台轮廓减禁放区，替代全局 `Model::getBedPolygon()`
   和固定 30 mm 的平台外围带；几何在盘局部坐标中处理。
2. 先把 Brim 区域裁到可打印区域，再按实际半线宽加 0.001 mm 留出中心线边界。
   首圈轮廓向内调整后再生成其余圈，保留贴边闭合外圈，避免只删除越界线段。
3. 轮廓简化和连接后再次检查中心线；跨越凹边或禁放区的部分才裁剪。最后添加
   盘原点偏移一次，不修改模型自身路径，也不增加 GUI／CLI 参数。
4. 修改平台尺寸／禁放区或盘原点时使 Brim 缓存失效；生成过程中补取消检查。

贴边的 Brim 可能窄于配置宽度，这是允许裁剪的结果；模型本体不被裁小。
额外求交、内缩与最终路径检查仅发生于 Brim 生成阶段；本轮没有耗时实测，
不能据此宣称切片速度无变化。

**待运行回归：** 扩展原临界容纳用例，覆盖 100／101／180 mm 平台与四种
Brim 状态、对象覆盖、多实例各自分盘、固定占盘和原硬限制；新增 Brim 预留
避开禁放区的摆盘负例。FFF 检查实际线宽包络在床内、贴边外圈仍存在且闭合、
圆形／凹形床、禁放区、偏移盘、平台配置变化后缓存重切。上一节缺少 FFF 的
两个夹具修正也包含在待验证范围内。

用户重建 OrcaSlicer、`libslic3r_tests`、`fff_print_tests` 后，按既有长期授权
复跑 `[Arrange][Raft]` 与 `[SupportMaterial][Raft]`；新增用例均在这些完整过滤器
内。随后以同版 CLI 复核 99 mm 的无 Brim／有 Brim、100 mm 超限负例，以及
GUI 的分盘→切片→导出 G-code 线宽包络。CLI 未排入仍可能返回 0 的既有结果
判定缺口单独保留，不把返回码作为摆盘成功的唯一条件。

本轮只做源码审阅、改动范围格式化和 `git diff --check`，未编译、未运行旧程序
证明新源码，未做 GUI、实机、完整 CTest 或其他平台验证。静态源码哈希和本轮
增量差异：`planning/raft/brim-bed-fit-fix-20260922/static-review.json`。

### 2026-09-22 09:56 Brim 构建错误修正（待重新构建）

实际日志为 `planning/build/macos-build-arm64-20260922-095629-881871.log`；消息中的
`095652-075806.log` 本地不存在。此轮编译在 `Brim.cpp:1637` 停止，唯一报出的
编译错误是新增自由函数直接调用受保护的 `PrintBase::make_try_cancel()`。
修改前 17 个源码／测试哈希与上一节静态快照一致。

已复用 `make_brim()` 收到的取消回调，经模型 Brim／支撑 Brim 的路径生成函数
传递到轮廓循环。保留循环取消检查，不改变接口可见性、边界几何或测试断言。
调用点核对、改动范围格式化和静态检查完成；未自行编译或运行测试。

用户重新运行原构建命令成功后，再验证 OrcaSlicer、`libslic3r_tests` 和
`fff_print_tests` 均已更新，按上一节过滤器及 CLI 边界对照复测。本次构建失败，
不能运行旧二进制作为修正后通过证明。证据及新哈希：
`planning/raft/brim-bed-fit-fix-20260922/build-fix-0956/static-review.json`。

### 2026-09-22 自动 Brim 空盘补放及贴边外圈修正（待新构建验证）

用户反馈“指定宽度能放入，自动模式仍被移到盘外”。原因是上一版补放资格只
接纳外侧／内侧／内外侧 Brim，明确排除了自动模式；自动计算可能得到零宽度，
但不能在摆盘前据此把所有自动 Brim 都视为无 Brim。

本次改动接入已有补放入口，不新增参数。逐层打印、关闭筏层／支撑／裙边的
FFF 对象，自动 Brim 正常摆盘失败后可尝试空矩形盘居中补放；对象覆盖参数同样
生效。模型投影须完整放入扣除平台边距后的范围，且满足限高，每个补放对象
独占一盘。固定对象／擦料塔占用盘、禁放区以及自动／绘制耳朵的原限制保留。

自动模式的补放避障预留取 **18 mm＋Brim 间隙＋保守半线宽**，与原 inflation
取最大值；18 mm 与切片生成器共享常量，生成器本身的宽度算法和上限未改变。
这笔预留只用于补放候选的禁放区等避障，不从热床四周再扣 18 mm，也不写回
正常对象间距或用户配置。实际 Brim 仍由切片计算并在平台边界按线宽裁剪，
可以为零，也可能比配置或计算宽度窄。存在禁放区时该上限可能拒绝实际窄 Brim
能放入的位置，属于当前保守策略；不额外预切片以追求更紧凑的摆放。

**修改前基线：** 用户实际构建日志为
`planning/build/macos-build-arm64-20260922-101444-400373.log`，完成应用及两个
测试目标；运行前 17 个源码／测试哈希与上一轮静态快照一致，链接／打包应用
哈希一致。本机 macOS arm64 / Release 实测：

- `[Arrange][Raft]`：16 用例／1,170 断言，退出码 0，约 0.13 秒。
- `[SupportMaterial][Raft]`：失败 1 用例／3 断言，退出码 3，约 29.28 秒。
  同一 Brim 临界用例在 100／101／180 平台的顶侧线宽包络分别只到
  99.749／100.749／179.749 mm，没有满足保留贴边外圈的断言。

源码中生成轮廓与最终开路径裁剪共用同一边界；闭合外圈的边恰落在裁剪边界时，
开路径裁剪存在丢边问题。本次把生成边界内缩设为实际半线宽＋0.002 mm，最终
检查保持半线宽＋0.001 mm，使原始外圈位于检查范围内部；简化或连接产生的
真实越界仍裁剪。原边界断言保留并加强为同一闭合外圈到达四侧，不通过放宽
检查掩盖缺边。此修正尚待新构建复测。

**新增／扩展的回归：** 自动模式覆盖 100／101／180 平台、多实例独占空盘、
固定占盘、手动间距、配置宽度 0／2 的对象覆盖，以及距模型超过配置 Brim
宽度但落在自动上限内的禁放区。原超宽、限高、支撑／裙边、逐对象打印和取消
负例扩展到自动模式。实际切片用宽而低的模型验证自动宽度为零，用细高模型
验证自动生成 Brim，检查模型入盘及实际 Brim 线宽包络不越界。

新补丁仅完成源码审阅、范围格式化和 `git diff --check`；未自行编译，没有用
修改前二进制证明修改后通过。用户重建 OrcaSlicer、`libslic3r_tests` 和
`fff_print_tests` 后复跑 `[Arrange][Raft]`／`[SupportMaterial][Raft]`，再核对
GUI／CLI 自动 Brim 临界分盘→切片→导出。修改前基线、源码快照、命令、
退出码和增量差异保存在 `planning/raft/auto-brim-bed-fit-20260922/`。

### 2026-09-22 11:13 新构建测试与用户 GUI 日志复核

用户构建 `planning/build/macos-build-arm64-20260922-105213-941900.log` 成功，
包含本次 ModelArrange、Brim、应用和两个相关测试目标。分支
`codex/raft-geometry-review`，基于 `4c45e87b30bcdaddac8c10dee188fdeaf3d1bbad`
及当前未提交补丁；18 个源码／测试哈希与上一节静态快照一致，链接和打包应用
哈希一致。本轮没有自行编译或修改生产／测试源码。

| 本次实际执行 | 用例 | 断言 | 退出码 | 耗时 |
| --- | ---: | ---: | ---: | ---: |
| `[Arrange][Raft] --reporter compact` | 16 | 1,467 | 0 | 0.96 秒 |
| `[SupportMaterial][Raft] --reporter compact` | 76 | 6,007 | 0 | 28.74 秒 |

以上覆盖上一节自动 Brim 补丁、贴边闭合外圈修正和新增自动零／非零宽度用例；
不再是仅修改前基线通过。分盘测试仍有测试环境 filament_info.json 资源解析
日志，断言通过不代表耗材温度资源验证通过。

用户运行日志 `planning/debug/orca-user-gui-run-20260922-105558-340587.log`：

- 7 次分盘中，10:56:10 首次未排入，随后 6 次成功。用户确认首次失败后
  “修改了其他参数”，但没有给出具体项；日志未保存足以重建失败时有效参数的
  完整快照。因此首次失败尚未归因，不能判断为预期拒绝，也不能宣称已修复。
- 10:58:39 命中 Brim 贴边补放，记录 `brim_clip_margin_mm=18.25`、
  `unarranged=0`，说明自动 Brim 的新入口实际执行。10:58:56 命中筏层贴边
  补放，同样全部排入。
- 7 次切片均完成校验、预览 G-code 生成与缓存文件替换，切片校验错误／警告为空。
  这不是用户显式导出到目标路径或下发打印的证明。
- 最后一次筏层贴边处理涉及 5 层；每层 `cuts=4, islands=8`，几何增强
  `status=applied, smoothing=3`，没有丢弃碎片或基底区域。此次 `groups=1`
  是一个对象的处理组，不能据此声称这份日志验证了多对象合并。
- 捕获最后一次 GUI G-code，核查前 5 层共 3,058 条挤出直线段；按标称半线宽
  计算的包络全部位于当前配置的 `[0,101]×[0,101] mm` 平台内。基底包络为
  `[0.002,100.998]×[0.002,100.998] mm`，无容差放宽。该检查不证明实物
  挤出边界、跨缝承托质量，也不替代三维碰撞检查。
- 启动阶段仍有重复预设迁移、缺少预设父项、禁放区三角化等日志，以及更新
  请求 404；未阻止本次立方体切片，不把整份日志称为“零错误”。

原始测试命令、输出、源码与二进制哈希、GUI 日志快照、G-code 和边界检查脚本
保存在 `planning/raft/auto-brim-validation-20260922-111336/`。
本轮没有重新操作 GUI，没有新跑 CLI 端到端、完整 CTest、其他平台或实机打印。

### 2026-09-22 后续筏层优化评估（仅分析，尚未编码）

当前平台裁剪、开启／关闭筏层混摆的实际路径冲突校验、兼容单实例对象合并、
分块和平滑已有实现与回归。优先保证软件稳定，不因比较 Xmaker 而替换全部
筏层生成器，不把模型师负责的打印质量结论当作软件测试结论。

| 顺序 | 事项及现状 | 建议及复杂度 |
| --- | --- | --- |
| 优先补证据 | 失败摆盘缺少具体拒绝原因及完整候选尺寸／有效参数；筏层只有阶段日志，没有分阶段耗时。 | 小改动：记录关键参数、拒绝原因及合并／平滑／分块／路径重建耗时；做大量对象、180 mm 平台、缓存重切和取消对照。先测量，再决定是否需要性能优化，不新增用户提示。 |
| 按支撑模型需求 | `RaftMerge.cpp` 的 `apply_raft_geometry()` 在成员 `has_support()` 时跳过整个相连组的分块和平滑；`has_support()` 包括仅开启支撑但没有实际生成支撑的情况。基础筏层和已有合并仍保留。 | 中到较大：先区分实际筏层与普通支撑路径的来源及所有权，再允许能够证明安全的输入。不可直接删除开关判断，也不必一开始做完整独立生成器。分别验证普通／树状支撑、无实际支撑、跨缝支撑及缓存恢复。 |
| 按复制实例需求 | `merge_overlapping_rafts()` 排除 `PrintObject` 中实例数不为 1 的对象，所以共用一份路径的多实例不仅跳过跨对象合并，也跳过该入口中的分块和平滑；独立拆分 OBJ 不受此限制。 | 中到较大：实例展开、坐标和路径所有权、G-code 输出次数及重切恢复一起设计。若日常以独立对象为主，可后置；不能只删实例数判断。 |
| 暂缓 | 一个相连组内的分块／平滑配置不一致时，该组保留原共享筏层，跳过几何增强；不是整盘取消合并。 | 真有逐对象差异需求时再做分组／交界规则，复杂度中等且涉及重复路径风险。当前保守行为有专项覆盖，可以保持。 |

本次最后一轮的阶段规划日志到几何应用完成约 1.21 秒，但期间包含路径生成
和安全检查；没有同模型开关对照，不能认定这是分块或平滑单项新增耗时，也
不能推断大量对象时的速度。当前没有证据要求立即做大范围性能改造。

独立于筏层功能，先前发现的 CLI 未排入仍可能返回 0 的结果协议缺口仍需单独
安排，尤其服务端使用时；本轮 GUI 成功和单元测试通过不能关闭该问题。

### 2026-09-22 入库审查结论与 CLI 补充验证

复查当前相对 `4c45e87b30` 的完整补丁，范围为混摆物理预留、固定对象避让、
空矩形盘单对象补放、Brim 平台／禁放区裁剪及相关缓存失效。没有发现本次
新增的阻塞入库问题，可以作为这些局部修复入库。该结论不扩大为整个 CLI
服务、其他平台或所有用户操作已经验收。

18 个源码／测试文件及应用、两个测试程序哈希与上一轮证据完全一致，故沿用
同版分盘 16 用例／1,467 断言、筏层 76 用例／6,007 断言通过记录，本轮没有
重复执行或把相同断言重复计数。静态差异检查通过。

新增同版 CLI 端到端 7 个场景：

- 99 mm 无筏层、Cura 筏层、固定 Brim、自动 Brim 零宽度、自动旋转窄模型、
  禁旋转且实际生成自动 Brim 的窄高模型，共 6 个正向场景成功排入。均检查了
  实际 `bed_idx=0`、返回码、非空 G-code 和空警告，不仅检查 JSON 中的盘号。
- 直线挤出路径按标称半线宽核查，以上模型／筏层／Brim 路径在 101×101 mm
  物理床内；不包括自定义起止 G-code，不证明实物挤出尺寸。固定 Brim 的
  包络为 `[0.002,100.998]² mm`；自动非零 Brim 的 X 两端同样贴边且没有丢失。
- 100 mm 模型无法放进扣除边距后的 99 mm 区域，核心返回 `bed=-1`，符合边距
  限制；但多盘 CLI 仍返回 `return_code=0`、盘号 0、三角面数 0，并生成 G-code。
  这是上一轮已记录的结果协议问题，不能记为负向端到端验收通过。此次 G-code
  被居中到物理床内，模型外墙包络为 `[0.5,100.5]² mm`；JSON 模型位置信息与
  后续导出坐标也不能混作同一次成功摆盘结果。没有证据称此例已实际越出物理床。

上述 CLI 负例在既有日志 `arrange-brim-review-20260922/cli/summary.json` 中
已有同样返回模式，本次生产差异没有修改这段失败结果处理。建议本补丁独立
入库，CLI 分盘失败的返回／导出策略作为后续服务端修复，不宣称已经解决。
GUI 首次分盘失败的具体参数尚未归因，也保留记录。

提交范围应包含新增 `src/libslic3r/Support/SupportGridParams.hpp`，否则从
干净检出无法编译；不纳入根目录运行产物 `result.json`。新 CLI 命令、夹具、
日志、结果 JSON、G-code、包络脚本及审查记录位于
`planning/raft/merge-review-20260922/`。本轮只审查和补充验证／文档，未修改
生产／测试源码、未自行编译、未提交。
