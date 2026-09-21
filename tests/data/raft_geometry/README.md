# Raft geometry fixtures

`reference-shapes.json` defines deterministic rectangular input contours for
`Raft geometry follows X-PRINT size and aspect-ratio probes` in the existing
`[SupportMaterial][Raft][Geometry]` filter. The test places each rectangle at
(20, 20), uses no raft expansion or phase inset, smoothing 0, cleanup width
0.4 mm and a 180 mm bed. Section counts check geometric classification, not
the number of sparse extrusion ribbons or physically removable pieces.

The size/aspect branches and main section counts were observed in the extracted
X-PRINT 5.9.6 engine (`XSlicer.exe` SHA-256
`3af94d2402b15fa6e9a324d2b7d1de6c1b322e57aa808c4d4ca97ac83460040d`).
Local analysis evidence is in
`planning/raft/xmaker-reverse-20260920/probes/manifest.json` and
`measurements.json`; that directory is not required to run the tests.
Those rectangular fixtures contain no third-party program or model data.

Separate C++ cases exercise the 40.5 mm phase-inset threshold, two small
disconnected/overlapping rectangles, a 4 mm hole and a deterministic off-centre
hole that produces narrow fragments. Exact extrusion geometry still requires
Orca integration tests; the reference measurements do not prove those pass.

`dragon-first-raft-outline.json` adds a 217-vertex nominal raft outline derived
from the user's dragon model, captured before segmentation on 2026-09-21. It
contains only 2D polygons, their holes and the actual 101 mm bed, with 1,000,000
integer units per millimetre. The capture SHA-256 is recorded in the file; the
original mesh and local capture directory are not needed by the test.

The corresponding Geometry test generates four walls from the prepared solid
sections and then applies the strict clip with short-fragment filtering OFF.
It requires every wall path to remain unchanged, so deleting broken segments
cannot hide a continuity regression. Separate synthetic cases cover newly cut
collapsed islands, an originally small island, a long narrow region, keeping
normal short extrusion, legacy clipping, attributes and cancellation. These
cases are part of the existing Geometry filter; fixture creation alone is not a
successful C++ test run.

`dragon-upper-raft-outlines.json` contains the four upper nominal raft layers
from the SAME capture (verified by SHA-256) as `dragon-first-raft-outline.json`.
The bearing tests combine these files to reproduce the isolated base strip at
X 22.4–49.2 / Y 50.9–52.7 mm. They require removing its paths AND island metadata,
while a model needing that exact strip must retain it. The integration fixture
injects the recorded outlines with valid source extrusion recipes; its small
cube objects are test harnesses, not reconstructions of the original dragon.
One/two base layers, zero/two middle layers, one/two source objects, repeated merging
and restoration are covered by generated combinations. Other cases protect
original small islands, attached base borders, upper-base connections, touching
demand and sparse-fill envelopes, and honour cancellation. The September 21
13:49 macOS arm64 / Release build passed Geometry (23 cases / 1,326 assertions)
and the full Raft suite (70 cases / 5,501 assertions), both exit 0. Geometry is
included in the full Raft total. These tests do not establish real
printing/peeling performance; source changes require a matching rebuild before
the results can be renewed.
