# Raft

Raft is a base layer that is printed under the object to improve adhesion and prevent warping. It consists of multiple layers of material that create a stable foundation for the print.

## Layers

Object will be raised by this number of support layers.

## Contact Z distance

Z gap between object and raft. Ignored for soluble interface.

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
