#!/usr/bin/env python3
"""Test fixture generation and OBJ acceptance checks; no Orca binary or build needed."""

from pathlib import Path
import tempfile
import unittest

from generate_repair_fixtures import FACES, VERTICES, generate
from test_cli_repair_acceptance import check_obj_topology


def obj(vertices=VERTICES, faces=FACES):
    return "\n".join(["v " + " ".join(map(str, v)) for v in vertices] +
                     ["f " + " ".join(str(index + 1) for index in face) for face in faces])


class RepairAcceptanceHelpers(unittest.TestCase):
    def test_closed_cube(self):
        self.assertEqual(check_obj_topology(obj()), (8, 12))

    def test_material_changes_do_not_require_split_vertices(self):
        colored = obj().replace("f ", "usemtl red\nf ", 1)
        self.assertEqual(check_obj_topology(colored), (8, 12))

    def test_coincident_independent_shells_stay_independent(self):
        faces = FACES + [tuple(i + 8 for i in f) for f in FACES]
        self.assertEqual(check_obj_topology(obj(VERTICES + VERTICES, faces)), (16, 24))

    def test_color_seams_are_not_silently_welded(self):
        faces = FACES[:6] + [tuple(i + 8 for i in f) for f in FACES[6:]]
        with self.assertRaisesRegex(AssertionError, "indexed edges"):
            check_obj_topology(obj(VERTICES + VERTICES, faces))

    def test_open_duplicate_and_reversed_faces(self):
        for faces in (FACES[:-1], FACES + [FACES[0]], [tuple(reversed(FACES[0]))] + FACES[1:]):
            with self.subTest(faces=faces), self.assertRaisesRegex(AssertionError, "indexed edges"):
                check_obj_topology(obj(faces=faces))

    def test_malformed_output(self):
        for text in ("", "v nan 0 0", obj(faces=[(0, 0, 1)]), obj(faces=[(0, 1, 99)]),
                     obj(faces=[]) + "\nf 1 2 3 4"):
            with self.subTest(text=text), self.assertRaises(AssertionError):
                check_obj_topology(text)

    def test_generated_fixtures_are_deterministic_and_single_triangle_is_readable(self):
        with tempfile.TemporaryDirectory(prefix="repair-fixture-check-") as directory:
            generated = Path(directory)
            generate(generated)
            source = Path(__file__).resolve().parents[1] / "tests/data/model_repair_acceptance"
            for path in generated.iterdir():
                self.assertEqual(path.read_bytes(), (source / path.name).read_bytes(), path.name)
            single = (generated / "10-only-triangle.stl").read_bytes()
            self.assertGreaterEqual(len(single), 212)
            self.assertEqual(single.count(b"  facet normal"), 1)
            self.assertEqual(single.count(b"      vertex"), 3)
            self.assertLess(len(single.splitlines()[0]), 80)


if __name__ == "__main__":
    unittest.main()
