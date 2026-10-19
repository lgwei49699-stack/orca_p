# Exact soup topology regression

`bracket.stl` is the user-provided original bracket from 2026-09-29, converted
from binary to ASCII STL for a reviewable fixture. All 862 triangles, their
vertex order and every float coordinate are preserved; stored normals are zero.
The source is `(圆角)永久方案托架20260918.stl`, not OrcaSlicer 2.4.2's `fix.stl`.

- Original SHA-256: `5df90e7a3122aae5dbb09d5897b074ebc4db2f3197aa0c839741e4e66eebbca8`
- Fixture SHA-256: `876f07a004353c9f5b101b6fb7a162468586d763ef343f85e1fe20a2dec3f5de`
- Same-coordinate diagnostic baseline: 5 boundary edges, 10 non-manifold edges,
  10 non-manifold vertices, 28 intersection pairs, 3 zero-area triangles,
  0 duplicate groups, 49 local winding conflicts. These counts are not the
  GUI's directed-edge quick check.

The three zero-area triangles are a collinear chain along the top edge. The
positive-area internal partitions comprise source face IDs 0, 1, 30, 31, 571,
572 (zero-based). IDs are evidence for this fixture, NOT repair selection rules.
The generic repair must find planar patches along three-face seams, certify
that their open interiors are inside the retained solid, and subdivide exact
boundary T-junctions without moving points or filling artificial holes.

Expected regression result: 856 triangles, all seven checks complete and zero,
unchanged bounds and source vertex positions, real through-hole at Y=30,
Z≈45 still open across X=0..5, and adjacent plate material retained. Repeated
repair must preserve the same oriented triangle geometry. Synthetic cases cover
an internal diagonal partition, a boundary T-junction, changed winding/order,
an external disk in a tube opening, and a disk whose centroids are inside the
solid but whose interior crosses a hole. GUI-policy cases include normal volume
centering and strict preservation/rollback of painted faces.

Validation baseline (2026-09-29): after the user's successful build, the
SoupTopology filter passed 4 cases / 591 assertions, included in the complete
repair suite's 183 cases / 14,333 assertions. Standalone CLI export produced
856 faces and its re-import required no further repair. Evidence is recorded
in planning/repair/precommit-20260929-MFW3EM/. This predates the separate GUI
repair-backup change. After the user's 15:50 rebuild, the backup filter passed
5 cases / 138 assertions and the full suite passed 188 cases / 14,471 assertions;
see planning/repair/backup-retest-20260929-cjlj46/validation.json.
Neither these geometry checks nor the independent prototype prove GUI backup
recovery, Undo/Redo or actual printing.
