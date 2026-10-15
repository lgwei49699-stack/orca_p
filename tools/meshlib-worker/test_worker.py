#!/usr/bin/env python3
"""Exercise the local-evaluation worker protocol, never the user's original files."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def cube_stl():
    points = [(x, y, z) for z in (0.0, 2.0) for y in (0.0, 2.0) for x in (0.0, 2.0)]
    triangles = [
        (0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6),
        (0, 1, 4), (1, 5, 4), (2, 6, 3), (3, 6, 7),
        (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5),
    ]
    records = [struct.pack("<12fH", 0, 0, 0, *(c for v in face for c in points[v]), 0) for face in triangles]
    return b"MeshLib protocol regression".ljust(80, b"\0") + struct.pack("<I", len(triangles)) + b"".join(records)


@unittest.skipIf(os.name == "nt", "Windows workers require the Orca parent runner's memory-limited Job; use CLI regression there")
class WorkerProtocolTests(unittest.TestCase):
    worker = None

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="orca-meshlib-protocol-")
        self.addCleanup(self.temporary.cleanup)
        self.task = Path(self.temporary.name)
        self.input = self.task / "meshlib-input.stl"
        self.input.write_bytes(cube_stl())

    def command(self, **overrides):
        values = {
            "task-directory": str(self.task), "parent-pid": str(os.getpid()),
            "timeout-ms": "20000", "memory-limit-mb": "512", "voxel-mm": "0.2",
            "max-input-faces": "3000000", "max-output-faces": "1000000",
        }
        values.update({key.replace("_", "-"): str(value) for key, value in overrides.items()})
        command = [str(self.worker)]
        for key, value in values.items():
            command.extend(("--" + key, value))
        return command

    def invoke(self, **overrides):
        before = hashlib.sha256(self.input.read_bytes()).hexdigest() if self.input.exists() else None
        result = subprocess.run(self.command(**overrides), capture_output=True, text=True, timeout=25, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        if before:
            self.assertEqual(hashlib.sha256(self.input.read_bytes()).hexdigest(), before)
        report_path = self.task / "meshlib-report.json"
        self.assertLessEqual(report_path.stat().st_size, 256 * 1024)
        report = json.loads(report_path.read_text())
        self.assertEqual(report["schema"], "meshlib_worker_v1")
        self.assertEqual(report["sdk_version"], "3.1.3.429")
        self.assertFalse(report["candidate_validated"])
        self.assertFalse(report["print_ready"])
        return report

    def assert_failure(self, status, **overrides):
        report = self.invoke(**overrides)
        self.assertEqual(report["status"], status, report)
        self.assertFalse((self.task / "meshlib-candidate.mesh").exists())
        return report

    def test_cube_candidate_has_packed_finite_topology(self):
        report = self.invoke()
        self.assertEqual(report["status"], "candidate_generated", report)
        self.assertEqual(report["input"]["faces"], 12)
        self.assertEqual(report["input"]["loaded_faces"], 12)
        self.assertEqual(report["skipped_face_count"], 0)
        self.assertTrue(report["runtime_identity_verified"])
        self.assertEqual({library["name"] for library in report["runtime_libraries"]}, {"MRMesh", "MRVoxels"})
        self.assertTrue(all(len(library["sha256"]) == 64 for library in report["runtime_libraries"]))
        self.assertIn(report["selected_sign_mode"], range(5))
        self.assertNotEqual(report["selected_sign_mode_name"], "unreported")
        self.assertEqual(report["parameters"]["voxel_mm"], struct.unpack("<f", struct.pack("<f", 0.2))[0])
        self.assertEqual(report["parameters"]["tiny_edge_length_mm"], struct.unpack("<f", struct.pack("<f", 0.005))[0])
        self.assertEqual(report["parameters"]["fragment_removal"], "none")
        content = (self.task / "meshlib-candidate.mesh").read_bytes()
        magic, vertices, faces = struct.unpack_from("<8sQQ", content)
        self.assertEqual(magic, b"ORCARC01")
        self.assertEqual(len(content), 24 + 12 * (vertices + faces))
        self.assertEqual(report["output"], {"faces": faces, "vertices": vertices, "bytes": len(content)})
        coordinates = struct.unpack_from("<" + "f" * (3 * vertices), content, 24)
        self.assertTrue(all(math.isfinite(value) for value in coordinates))
        indices = struct.unpack_from("<" + "i" * (3 * faces), content, 24 + 12 * vertices)
        self.assertEqual(set(indices), set(range(vertices)))

    def test_missing_input(self):
        self.input.unlink()
        self.assert_failure("invalid_input")

    def test_input_symlink_rejected(self):
        real = self.task / "original.stl"
        self.input.rename(real)
        self.input.symlink_to(real)
        self.assert_failure("invalid_input")

    def test_truncated_stl(self):
        self.input.write_bytes(cube_stl()[:-1])
        self.assert_failure("invalid_input")

    def test_nonfinite_input(self):
        content = bytearray(cube_stl())
        struct.pack_into("<f", content, 84 + 12, float("nan"))
        self.input.write_bytes(content)
        self.assert_failure("invalid_input")

    def test_input_face_limit(self):
        self.assert_failure("resource_limit", max_input_faces=1)

    def test_output_face_limit(self):
        self.assert_failure("resource_limit", max_output_faces=1)

    def test_loader_may_not_drop_faces(self):
        content = bytearray(cube_stl())
        # Make one triangle have three identical coordinates. MeshLib's loader
        # normally skips it; accepting the reduced input would silently lose data.
        struct.pack_into("<9f", content, 84 + 12, *(0.0 for _ in range(9)))
        self.input.write_bytes(content)
        report = self.invoke()
        self.assertIn(report["status"], ("input_faces_skipped", "invalid_input"), report)
        self.assertFalse((self.task / "meshlib-candidate.mesh").exists())

    def test_existing_output_is_not_overwritten(self):
        target = self.task / "meshlib-candidate.mesh"
        target.write_bytes(b"preserve-existing-file")
        report = self.invoke()
        self.assertEqual(report["status"], "output_error", report)
        self.assertEqual(target.read_bytes(), b"preserve-existing-file")

    def test_deadline_cancels_before_candidate_publication(self):
        self.assert_failure("timed_out", timeout_ms=1)

    def test_wrong_parent_is_rejected_before_sdk_work(self):
        result = subprocess.run(self.command(parent_pid=os.getppid()), capture_output=True, text=True, timeout=5, check=False)
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertFalse((self.task / "meshlib-report.json").exists())
        self.assertFalse((self.task / "meshlib-candidate.mesh").exists())

    def test_invalid_voxel_does_not_start_repair(self):
        result = subprocess.run(self.command(voxel_mm="not-a-number"), capture_output=True, text=True, timeout=5, check=False)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(json.loads(result.stderr)["status"], "invalid_options")
        self.assertFalse((self.task / "meshlib-candidate.mesh").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("worker", type=Path, help="Absolute path to the separately built local-evaluation worker")
    arguments, remainder = parser.parse_known_args()
    WorkerProtocolTests.worker = arguments.worker.resolve(strict=True)
    unittest.main(argv=[__file__, *remainder])
