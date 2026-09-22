#!/usr/bin/env python3
"""Test the raft evidence collector's fail-closed checks without running a slicer."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

from validate_raft_stability import NS, audit_gcode, box, distance_to_rectangle, fixture, ring, sha256


class RaftStabilityHelpers(unittest.TestCase):
    def test_closed_oriented_fixtures(self):
        for vertices, faces in (box(8, 8, 4), ring()):
            edges = Counter((f[i], f[(i + 1) % 3]) for f in faces for i in range(3))
            self.assertTrue(all(count == 1 and edges[b, a] == 1 for (a, b), count in edges.items()))
            volume = 0
            for face in faces:
                a, b, c = [vertices[i] for i in face]
                volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) +
                           a[1] * (b[2] * c[0] - b[0] * c[2]) +
                           a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
            self.assertGreater(volume, 0)

    def test_independent_objects_and_deterministic_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / "a.3mf", Path(directory) / "b.3mf"
            fixture(a, 50, "legs", 180)
            fixture(b, 50, "legs", 180)
            self.assertEqual(sha256(a), sha256(b))
            with zipfile.ZipFile(a) as archive:
                model = ET.fromstring(archive.read("3D/3dmodel.model"))
            objects = model.findall(f"{{{NS}}}resources/{{{NS}}}object")
            items = model.findall(f"{{{NS}}}build/{{{NS}}}item")
            self.assertEqual(len(objects), 50)
            self.assertEqual(len(items), 50)
            self.assertEqual(len({item.attrib["objectid"] for item in items}), 50)

    def test_rectangle_distance_includes_crossings_and_round_corners(self):
        rect = (10, 10, 20, 20)
        self.assertEqual(distance_to_rectangle((0, 15), (30, 15), rect), 0)
        self.assertEqual(distance_to_rectangle((9, 0), (9, 30), rect), 1)
        self.assertAlmostEqual(distance_to_rectangle((0, 9), (9, 9), rect), 2 ** .5)

    def audit(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.gcode"
            path.write_text(text)
            return audit_gcode(path, [0, 0, 100, 100], [[90, 0, 100, 20]])

    def test_width_boundary_exclusion_duplicates_and_relative_extrusion(self):
        result = self.audit("""G90
M83
G0 X1 Y30 Z0.3
;LAYER_CHANGE
;TYPE:Support
;WIDTH:0.6
G1 X99.9 E1
G0 X1 Y30
G1 X99.9 E1
G0 X80 Y10
G1 X89.8 E1
G0 X10 Y50
G1 X20 E-1
;TYPE:Custom
G1 X200 E2
""")
        self.assertEqual(result["extrusion_segments"], 3)
        self.assertEqual(result["outside_segments"], 2)
        self.assertEqual(result["excluded_segments"], 1)
        self.assertEqual(result["exact_raft_duplicate_segments"], 1)

    def test_unsupported_arc_missing_width_and_nan_fail(self):
        for line in ("G2 X20 Y20 I1 J1 E1", "G1 Xnan Y20 E1"):
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.audit("M83\n;LAYER_CHANGE\n;TYPE:Support\n;WIDTH:0.4\n" + line)
        with self.assertRaises(ValueError):
            self.audit("M83\n;LAYER_CHANGE\n;TYPE:Support\nG1 X20 E1")

    def test_absolute_extrusion_reset_and_relative_coordinates(self):
        result = self.audit("""G90
M82
G0 X30 Y30 Z0.3
;LAYER_CHANGE
;TYPE:Support
;WIDTH:0.4
G1 X40 E1
G92 E0
G91
G1 X10 E1
""")
        self.assertEqual(result["extrusion_segments"], 2)
        self.assertEqual(result["outside_segments"], 0)
        self.assertEqual(result["excluded_segments"], 0)


if __name__ == "__main__":
    unittest.main()
