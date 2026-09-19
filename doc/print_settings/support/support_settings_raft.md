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
