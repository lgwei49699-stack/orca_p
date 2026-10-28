#!/usr/bin/env python3
"""Check the opt-in model-stage CLI contract against a freshly built executable.

Never builds, slices, prints, downloads SDKs, or uses the repository as the
process working directory. Every invocation gets isolated input/output/log
directories. Evidence is retained in a new directory; existing evidence cannot
be overwritten. The MeshLib runtime cases require an explicit --meshlib choice.
"""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path, PureWindowsPath
import platform
import subprocess
import sys
import tempfile
import time

from test_cli_model_check import triangle_count, validate_business_result, validate_issue_summary
from test_cli_model_reconstruction import FACES, VERTICES, binary_stl, digest, save_json


SCHEMA = "orca_model_stage_v1"
SIDECAR = "model-stage.json"
DISTANCES = ("bbox_deviation_mm", "sampled_source_to_candidate_mm", "sampled_candidate_to_source_mm")


def create_fixtures(directory):
    directory.mkdir()
    shifted = [(x + 3., y, z) for x, y, z in VERTICES]
    second = [(x + 6., y, z) for x, y, z in VERTICES]
    # Decimal bounds are intentionally not exactly representable as float.
    # Model's center -> float translation -> world-coordinate round trip can
    # move these coordinates by one ULP on every import. Stage inspection must
    # bind to the raw serialized STL, not to that re-centered Model mesh.
    fractional = [(-1.1 if x == 0. else 5.3, y, z) for x, y, z in VERTICES]
    numeric_vertices = [(x, y, z + (face_id + 1) * 1e-12 if z == 0 else z)
                        for face_id, face in enumerate(FACES) for x, y, z in (VERTICES[index] for index in face)]
    numeric_faces = [tuple(range(i, i + 3)) for i in range(0, len(numeric_vertices), 3)]
    colored_header = binary_stl(attribute=0xAB72)
    colored_header = b"COLOR=\x91\xdcP\xff".ljust(80, b" ") + colored_header[80:]
    nan_normal_ascii = "solid normals\n" + "".join(
        "facet normal NaN NaN NaN\nouter loop\n"
        + "".join("vertex " + " ".join(map(str, VERTICES[index])) + "\n" for index in face)
        + "endloop\nendfacet\n" for face in FACES) + "endsolid normals\n"
    fixtures = {
        "cube": ("cube.stl", binary_stl()),
        "open": ("open.stl", binary_stl(faces=FACES[:2] + FACES[4:])),
        "shifted": ("reference shifted.stl", binary_stl(vertices=shifted)),
        "two": ("two.stl", binary_stl(VERTICES + second,
                                      FACES + [tuple(index + 8 for index in face) for face in FACES])),
        "fractional": ("fractional-bounds.stl", binary_stl(vertices=fractional)),
        "colored": ("colored.stl", binary_stl(attribute=0x801F)),
        "colored_header": ("colored-header.stl", colored_header),
        "nan_normals": ("nan-normals.stl", nan_normal_ascii.encode("ascii")),
        "nan_vertex": ("nan-vertex.stl", nan_normal_ascii.replace("vertex 0.0", "vertex NaN", 1).encode("ascii")),
        "numeric_cracks": ("numeric-cracks.stl", binary_stl(vertices=numeric_vertices, faces=numeric_faces)),
        "truncated": ("truncated.stl", binary_stl()[:-1]),
        "unicode": ("中文 模型/方块.stl", binary_stl()),
        "obj": ("cube.obj", ("\n".join(
            ["v " + " ".join(map(str, vertex)) for vertex in VERTICES]
            + ["f " + " ".join(str(index + 1) for index in face) for face in FACES]) + "\n").encode("utf-8")),
    }
    paths = {}
    for key, (name, contents) in fixtures.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        paths[key] = path
    # Reuse the repository's deterministic intersecting-shell fixture, but run
    # only on a private copy so assertions can detect accidental input changes.
    existing = Path(__file__).resolve().parents[1] / "tests/data/model_repair_acceptance/08-intersecting-shells.stl"
    paths["intersecting"] = directory / "intersecting.stl"
    paths["intersecting"].write_bytes(existing.read_bytes())
    for key, name in (("degenerate", "05-degenerate-face"), ("isolated", "06-isolated-face"), ("thin", "07-thin-solid")):
        source = existing.parent / (name + ".stl")
        paths[key] = directory / source.name
        paths[key].write_bytes(source.read_bytes())
    return paths


def check_stage_report(report, output, source, reference, stage):
    assert report["schema"] == SCHEMA, report
    assert report["status"] == "completed" and report["stage"] == stage, report
    assert report["input_sha256"] == digest(source), report
    assert report["reference_sha256"] == digest(reference), report
    artifact = report["artifact"]
    relative = artifact["path"]
    assert isinstance(relative, str) and relative, artifact
    # Reject both native and foreign absolute paths, including Windows drives
    # when testing on macOS/Linux. A parent traversal is never a valid contract.
    assert not Path(relative).is_absolute() and not PureWindowsPath(relative).is_absolute(), artifact
    assert not PureWindowsPath(relative).drive and ".." not in PureWindowsPath(relative).parts, artifact
    artifact_file = (output / relative).resolve(strict=True)
    assert artifact_file.is_relative_to(output.resolve()) and artifact_file.is_file(), artifact
    assert artifact_file.suffix.lower() == ".stl" and artifact_file.stat().st_size > 0, artifact
    assert triangle_count(artifact_file) > 0, artifact
    assert artifact["sha256"] == digest(artifact_file), artifact
    assert artifact["readback_verified"] is True, artifact

    inspection = report["inspection"]
    assert inspection["status"] in ("passed", "defects_detected", "incomplete"), inspection
    assert inspection["mesh_status"] in ("passed", "needs_repair", "unknown"), inspection
    assert isinstance(inspection["check_complete"], bool), inspection
    validate_issue_summary(inspection["issues"])
    if inspection["status"] == "passed":
        assert inspection["mesh_status"] == "passed" and inspection["check_complete"], inspection
        assert all(item == {"count": 0, "complete": True} for item in inspection["issues"].values()), inspection
    elif inspection["status"] == "defects_detected":
        assert any(item["count"] > 0 for item in inspection["issues"].values()), inspection
    assert isinstance(report["repair_status"], str) and report["repair_status"], report

    change = report["change"]
    assert change["status"] == "completed", change
    for field in DISTANCES:
        assert type(change[field]) in (int, float) and math.isfinite(change[field]) and change[field] >= 0, change
    for field in ("source_samples", "candidate_samples", "reference_components", "candidate_components"):
        assert type(change[field]) is int and change[field] > 0, change
    assert change["distance_is_upper_bound"] is False, change
    assert change["distance_method"] == "sampled_vertices_edge_midpoints_and_face_centroids", change
    assert change["component_method"] == "exact_position_welded_edge_connected_all_faces", change
    assert change["components_complete"] is True, change
    reconstruction = report["reconstruction"]
    assert reconstruction["assessment"] in ("not_needed", "evidence_present", "undetermined", "blocked"), reconstruction
    assert isinstance(reconstruction["reason"], str) and reconstruction["reason"], reconstruction
    assert reconstruction["applicability"] in ("not_evaluated", "eligible", "unsupported"), reconstruction
    return artifact_file


class Suite:
    def __init__(self, binary, output, timeout):
        self.binary, self.output, self.timeout = binary, output, timeout
        self.results = []

    def run(self, name, source, *, stage="check", flags=(), reference=None,
            sidecar=True, success=True, existing_sidecar=False, verify=None):
        case = self.output / name
        case.mkdir()
        cwd = case / "working-directory"
        cwd.mkdir()
        output = case / "output"
        # Standalone reconstruction owns creation of its fresh output directory.
        if stage != "reconstruction" or existing_sidecar:
            output.mkdir()
        sentinel = b'{"sentinel":"must remain byte-for-byte unchanged"}\n'
        if existing_sidecar:
            (output / SIDECAR).write_bytes(sentinel)
        reference_source = reference if reference is not None else source
        protected = {path: digest(path) for path in {source, reference_source} if path.is_file()}
        if stage == "reconstruction":
            command = [str(self.binary), "--reconstruct-model", str(source),
                       "--reconstruction-engine", "meshlib", "--reconstruction-voxel-mm", "0.0803",
                       "--accept-reconstruction-risk"]
        elif stage == "export":
            command = [str(self.binary), "--export-stl", str(source)]
        else:
            command = [str(self.binary), "--check-model", str(source)]
            if stage == "advanced":
                command.extend(("--repair-model", "cgal", "--repair-evidence"))
        command.extend(("--outputdir", str(output)))
        if sidecar:
            command.append("--model-stage-report")
        if reference is not None:
            command.extend(("--model-reference", str(reference)))
        command.extend(flags)
        save_json(case / "command.json", command)
        record = {"case": name, "command": command, "stage": stage, "passed": False}
        start = time.monotonic()
        try:
            with (case / "stdout.log").open("wb") as stdout, (case / "stderr.log").open("wb") as stderr:
                process = subprocess.run(command, cwd=cwd, stdout=stdout, stderr=stderr,
                                         timeout=self.timeout, check=False)
            record["exit_code"] = process.returncode
            # A crash cannot pass as an expected validation rejection.
            assert process.returncode >= 0, f"Process terminated by signal: {process.returncode}"
            assert not 0xC0000000 <= process.returncode <= 0xCFFFFFFF, (
                f"Process exited with a Windows exception code: {process.returncode:#x}")
            assert (process.returncode == 0) == success, (process.returncode, case)
            report = None
            if existing_sidecar:
                assert (output / SIDECAR).read_bytes() == sentinel, "Existing stage report was overwritten"
            elif success and sidecar:
                report = json.loads((output / SIDECAR).read_text(encoding="utf-8"))
                artifact = check_stage_report(report, output, source, reference_source, stage)
                record["artifact_sha256"] = digest(artifact)
                record["inspection"] = report["inspection"]
                record["repair_status"] = report["repair_status"]
                record["change"] = report["change"]
                if verify:
                    verify(report)
            else:
                assert not (output / SIDECAR).exists(), "A rejected/opt-out call published a stage report"
            assert not list(output.glob("model-stage-*.tmp")), "Stage report temporary file was not cleaned up"
            if success and stage in ("check", "advanced"):
                legacy = json.loads((output / "result.json").read_text(encoding="utf-8"))
                old = validate_business_result(legacy, evidence=stage == "advanced" or "--repair-evidence" in flags)
                assert legacy["return_code"] == 0 and legacy["error_string"] == "", legacy
                if report:
                    assert old["issues"] == report["inspection"]["issues"], (old, report)
                    assert old["mesh_status"] == report["inspection"]["mesh_status"], (old, report)
                    assert old["check_complete"] == report["inspection"]["check_complete"], (old, report)
                    assert old["repair_status"] == report["repair_status"], (old, report)
                    if "boundary" in old:
                        expected = old["boundary"]["reconstruction"]
                        assert all(report["reconstruction"][key] == expected[key]
                                   for key in ("assessment", "reason", "applicability")), (expected, report)
            if success and stage == "reconstruction":
                legacy = json.loads((output / "reconstruction.json").read_text(encoding="utf-8"))
                assert legacy["schema"] == "model_reconstruction_v1" and legacy["backend"] == "meshlib", legacy
                assert legacy["status"] == "candidate_ready" and legacy["mesh_validated"] is True, legacy
                assert legacy["export_round_trip"] == "passed", legacy
                assert legacy["artifacts"]["input_sha256"] == digest(source), legacy
                if report:
                    assert legacy["artifacts"]["candidate_sha256"] == report["artifact"]["sha256"], (legacy, report)
                    assert report["inspection"]["status"] == "passed", report
                assert not (output / "result.json").exists(), "Reconstruction polluted the legacy check report"
            assert not (cwd / "result.json").exists(), "CLI wrote result.json outside its output directory"
            record["passed"] = True
        except Exception as error:
            record["error"] = f"{type(error).__name__}: {error}"
        finally:
            for path, expected in protected.items():
                if not path.is_file() or digest(path) != expected:
                    record["passed"] = False
                    record["error"] = f"Input/reference changed: {path}"
            record["elapsed_seconds"] = round(time.monotonic() - start, 3)
            self.results.append(record)
            save_json(case / "test-result.json", record)
            print(f"{'PASS' if record['passed'] else 'FAIL'} {name}"
                  + (f": {record['error']}" if not record["passed"] else ""), flush=True)
        return record


def expect_shape(report, *, status=None, repair=None, reference_components=None,
                 candidate_components=None, distances=None):
    if status is not None:
        assert report["inspection"]["status"] == status, report
    if repair is not None:
        assert report["repair_status"] == repair, report
    change = report["change"]
    if reference_components is not None:
        assert change["components_complete"] and change["reference_components"] == reference_components, change
    if candidate_components is not None:
        assert change["components_complete"] and change["candidate_components"] == candidate_components, change
    if distances is not None:
        for field, expected in zip(DISTANCES, distances):
            assert math.isclose(change[field], expected, rel_tol=1e-6, abs_tol=1e-6), (field, expected, change)


def expect_repaired_hole(report):
    expect_shape(report, status="passed", repair="repaired", reference_components=1, candidate_components=1)
    proof = report["change"]["hole_fill"]
    assert proof["status"] == "verified", proof
    assert proof["method"] == "boundary_disks_preserve_source", proof
    assert proof["source_faces"] == proof["preserved_faces"] == 10, proof
    assert proof["added_faces"] > 0 and proof["patches"] == 1 and proof["boundary_edges"] == 4, proof
    # The reference is the raw input, not the repaired output. The newly added
    # face must appear in candidate -> reference distance, even with unchanged
    # bounds. This prevents an accidentally self-compared "zero change" report.
    change = report["change"]
    assert change["sampled_candidate_to_source_mm"] > 0.1, change


def expect_zero_area_cleanup(report):
    expect_shape(report, status="passed", reference_components=1, candidate_components=1)
    change = report["change"]
    assert change["bbox_deviation_mm"] == 10, change
    assert change["reference_excluded_degenerate_faces"] == 2, change
    assert change["degenerate_cleanup_preserves_surface"] is True, change
    assert change["sampled_source_to_candidate_mm"] == change["sampled_candidate_to_source_mm"] == 0, change


def expect_preserved_thin_solid(report):
    expect_shape(report, status="passed", reference_components=2, candidate_components=2)
    assert "degenerate_cleanup_preserves_surface" not in report["change"], report["change"]


def expect_isolated_surface_change(report):
    expect_shape(report, status="passed", reference_components=2, candidate_components=1)
    assert "degenerate_cleanup_preserves_surface" not in report["change"], report["change"]
    assert report["change"]["sampled_source_to_candidate_mm"] > 0.25, report["change"]
    assert report["change"]["numerical_cleanup_preserves_surface"] is False, report["change"]


def expect_numeric_cleanup(report):
    change = report["change"]
    assert report["inspection"]["status"] == "passed", report
    assert change["reference_components"] > change["candidate_components"] == 1, change
    assert change["numerical_cleanup_preserves_surface"] is True, change
    assert change["sampled_source_to_candidate_mm"] < 1e-6, change


def expect_reconstructed_shifted_reference(report):
    expect_shape(report, status="passed", reference_components=1, candidate_components=1)
    # Approximate reconstruction slightly changes the cube, but cannot erase
    # the deliberate 3 mm reference translation. Hash identity alone is not a
    # sufficient assertion that the comparison actually used that reference.
    for field in DISTANCES:
        assert 2.9 <= report["change"][field] <= 3.1, report["change"]


def expect_fractional_export(report):
    expect_shape(report, status="passed", repair="not_needed", reference_components=1,
                 candidate_components=1, distances=(0., 0., 0.))
    # An explicit export enters the final serialize/reinspect branch even when
    # advanced repair has nothing to change. Do not accidentally test the
    # baseline branch for a third time.
    assert Path(report["artifact"]["path"]).name == "result.stl", report["artifact"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Freshly built Orca executable")
    parser.add_argument("--output-dir", type=Path, help="New evidence directory; parent must already exist")
    parser.add_argument("--timeout", type=int, default=180, help="Per-invocation wall limit in seconds")
    parser.add_argument("--meshlib", choices=("enabled", "unavailable", "skip"), default="skip",
                        help="Explicitly test SDK runtime success, SDK-disabled rejection, or record a skip")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    binary = args.binary.resolve(strict=True)
    if not binary.is_file():
        parser.error("--binary must identify an executable file")
    if args.output_dir:
        output = args.output_dir.resolve()
        if not output.parent.is_dir() or output.exists():
            parser.error("--output-dir must not exist and its parent must exist")
        output.mkdir()
    else:
        output = Path(tempfile.mkdtemp(prefix="orca-model-stage-"))
    binary_hash = digest(binary)
    fixtures = create_fixtures(output / "fixtures")
    original_hashes = {str(path.relative_to(output)): digest(path) for path in fixtures.values()}
    save_json(output / "fixture-manifest.json", original_hashes)
    print(f"Evidence: {output}", flush=True)
    suite = Suite(binary, output, args.timeout)
    cube, missing_top = fixtures["cube"], fixtures["open"]

    suite.run("legacy-check-no-report", cube, sidecar=False)
    suite.run("explicit-report-off", cube, sidecar=False, flags=("--model-stage-report", "0"))
    suite.run("legacy-advanced-no-report", missing_top, stage="advanced", sidecar=False)
    suite.run("check-healthy", cube, verify=lambda r: expect_shape(
        r, status="passed", repair="not_requested", reference_components=1,
        candidate_components=1, distances=(0., 0., 0.)))
    suite.run("check-defects", missing_top, verify=lambda r: expect_shape(r, status="defects_detected"))
    suite.run("check-zero-area-cleanup", fixtures["degenerate"], verify=expect_zero_area_cleanup)
    suite.run("check-numeric-crack-cleanup", fixtures["numeric_cracks"], verify=expect_numeric_cleanup)
    suite.run("check-preserves-thin-solid", fixtures["thin"], verify=expect_preserved_thin_solid)
    suite.run("check-nonzero-isolated-face-not-exempt", fixtures["isolated"], verify=expect_isolated_surface_change)
    suite.run("advanced-healthy", cube, stage="advanced", verify=lambda r: expect_shape(
        r, status="passed", repair="not_needed", reference_components=1, candidate_components=1))
    suite.run("advanced-hole", missing_top, stage="advanced", verify=expect_repaired_hole)
    suite.run("check-fractional-readback", fixtures["fractional"], verify=lambda r: expect_shape(
        r, status="passed", repair="not_requested", reference_components=1,
        candidate_components=1, distances=(0., 0., 0.)))
    suite.run("advanced-fractional-readback", fixtures["fractional"], stage="advanced",
              verify=lambda r: expect_shape(r, status="passed", repair="not_needed",
                                           reference_components=1, candidate_components=1))
    suite.run("advanced-fractional-export-readback", fixtures["fractional"], stage="advanced",
              flags=("--export-stl",), verify=expect_fractional_export)
    suite.run("advanced-intersections", fixtures["intersecting"], stage="advanced",
              verify=lambda r: expect_shape(r, status="defects_detected"))
    suite.run("check-unicode-path", fixtures["unicode"], verify=lambda r: expect_shape(r, status="passed"))
    suite.run("explicit-identical-reference", cube, reference=fixtures["unicode"],
              verify=lambda r: expect_shape(r, distances=(0., 0., 0.)))
    suite.run("translated-reference", cube, reference=fixtures["shifted"], verify=lambda r: expect_shape(
        r, reference_components=1, candidate_components=1, distances=(3., 3., 3.)))
    suite.run("advanced-translated-reference", cube, stage="advanced", reference=fixtures["shifted"],
              verify=lambda r: expect_shape(r, distances=(3., 3., 3.)))
    suite.run("component-change-is-reported-not-selected", fixtures["two"], reference=cube,
              verify=lambda r: expect_shape(r, reference_components=1, candidate_components=2))
    suite.run("missing-reference", cube, reference=output / "fixtures/no-such-file.stl", success=False)
    suite.run("reference-without-report", cube, reference=cube, sidecar=False, success=False)
    suite.run("reference-with-report-off", cube, reference=cube, sidecar=False,
              flags=("--model-stage-report", "0"), success=False)
    suite.run("non-stl-input", fixtures["obj"], success=False)
    suite.run("non-stl-reference", cube, reference=fixtures["obj"], success=False)
    suite.run("colored-input", fixtures["colored"], verify=lambda r: expect_shape(r, status="passed"))
    suite.run("colored-header-input", fixtures["colored_header"], verify=lambda r: expect_shape(r, status="passed"))
    suite.run("colored-reference", cube, reference=fixtures["colored"],
              verify=lambda r: expect_shape(r, status="passed", distances=(0., 0., 0.)))
    suite.run("ascii-nan-normals", fixtures["nan_normals"], verify=lambda r: expect_shape(
        r, status="passed", distances=(0., 0., 0.)))
    suite.run("ascii-nan-normals-advanced", fixtures["nan_normals"], stage="advanced",
              verify=lambda r: expect_shape(r, status="passed", distances=(0., 0., 0.)))
    suite.run("ascii-nan-vertex-rejected", fixtures["nan_vertex"], success=False)
    # Report geometry accepts STL attributes, while standalone reconstruction
    # still refuses to discard a colored input's metadata.
    if args.meshlib == "enabled":
        suite.run("colored-reconstruction-still-rejected", fixtures["colored"], stage="reconstruction", success=False)
        suite.run("ascii-nan-normal-reconstruction-still-rejected", fixtures["nan_normals"], stage="reconstruction", success=False)
    suite.run("truncated-input", fixtures["truncated"], success=False)
    suite.run("truncated-reference", cube, reference=fixtures["truncated"], success=False)
    suite.run("invalid-export-combination", cube, flags=("--export-obj",), success=False)
    suite.run("unsupported-stage", cube, stage="export", success=False)
    suite.run("existing-sidecar-preserved", cube, existing_sidecar=True, success=False)
    suite.run("advanced-existing-sidecar-preserved", cube, stage="advanced", existing_sidecar=True, success=False)

    if args.meshlib == "enabled":
        suite.run("meshlib-stage-report", cube, stage="reconstruction")
        suite.run("meshlib-no-stage-report", cube, stage="reconstruction", sidecar=False)
        suite.run("meshlib-original-reference", cube, stage="reconstruction", reference=fixtures["shifted"],
                  verify=expect_reconstructed_shifted_reference)
        suite.run("meshlib-existing-sidecar-preserved", cube, stage="reconstruction",
                  existing_sidecar=True, success=False)
        suite.run("meshlib-missing-reference", cube, stage="reconstruction",
                  reference=output / "fixtures/missing.stl", success=False)
    elif args.meshlib == "unavailable":
        suite.run("meshlib-unavailable-no-stage-report", cube, stage="reconstruction", success=False)

    integrity = digest(binary) == binary_hash and all(
        (output / name).is_file() and digest(output / name) == expected for name, expected in original_hashes.items())
    failed = sum(not result["passed"] for result in suite.results)
    summary = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "binary": str(binary), "binary_sha256": binary_hash, "platform": platform.platform(),
        "meshlib_mode": args.meshlib, "meshlib_runtime_verified": args.meshlib == "enabled" and failed == 0,
        "meshlib_skipped": args.meshlib == "skip", "source_integrity_passed": integrity,
        "tests": len(suite.results), "passed": len(suite.results) - failed, "failed": failed,
        "cases": suite.results,
        "not_verified": ["GUI", "slicing", "physical printing", "commercial licensing", "other-platform runtime"],
    }
    save_json(output / "summary.json", summary)
    print(f"{summary['passed']}/{summary['tests']} passed; integrity={integrity}; MeshLib={args.meshlib}", flush=True)
    return 0 if not failed and integrity else 1


if __name__ == "__main__":
    sys.exit(main())
