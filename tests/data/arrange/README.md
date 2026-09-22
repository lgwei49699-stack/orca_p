# Stable arrange rotation fixture

`stable_rotation.polygons` contains seven synthetic convex footprints in millimetres,
representing split-model triangles, a parallelogram and a square. It is not an
exact mesh export of the user's seven STL files.

The first value is the object count. Each object starts with a name and vertex
count, followed by the local XY coordinates. Winding is counter-clockwise and
the closing vertex is implicit.

The regression test changes the incoming XY positions and Z rotations without
changing these footprints, then repeats arrangement ten times. It checks layout
stability, physical spacing, and bed containment. It does not assert that this
synthetic fixture or every arbitrary model can fit on one plate.

Manual acceptance for the original seven STL models: keep settings unchanged,
disable support/raft, and arrange ten times; target one plate on every run.
Repeat separately with Cura V1 raft, legacy raft, rotation disabled, axis
alignment, sequential printing, fixed objects and multi-colour wipe towers.
Preserve each mode's safety constraints and check the sliced toolpaths too.
