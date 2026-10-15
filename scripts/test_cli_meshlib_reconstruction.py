#!/usr/bin/env python3
"""Exercise the optional MeshLib reconstruction CLI with small isolated fixtures.

Requires an already-built Orca executable and a MeshLib sidecar. Does not build,
slice, print, download an SDK, modify an input, or overwrite an output directory.
The report distinguishes an actual supervised timeout from an earlier rejection;
a test-harness deadline is always a test failure.
"""

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from test_cli_model_reconstruction import (
    FACES, VERTICES, ascii_stl, binary_stl, check_candidate_file,
    check_report_contract, digest, directory_snapshot, extract_report, save_json,
)


EXPECTED_SDK_VERSION = "3.1.3.429"
VOXEL_MM = "0.0803"


def create_small_fixtures(directory):
    """Do not call the Alpha Wrap suite's multi-million-face fixture factory."""
    directory.mkdir()
    shifted = [(x + 4., y, z) for x, y, z in VERTICES]
    two = binary_stl(VERTICES + shifted, FACES + [tuple(i + 8 for i in face) for face in FACES])
    folded_vertices = [(float(ring), y, z) for ring in range(4)
                       for y, z in ((0., 0.), (1., 0.), (1., 1.), (0., 1.))]
    folded_vertices[6] = (0.5, 0.5, -0.2)
    folded_faces = [(0, 2, 1), (0, 3, 2)]
    for ring in range(3):
        for corner in range(4):
            a, b = ring * 4 + corner, ring * 4 + (corner + 1) % 4
            folded_faces.extend(((a, b, b + 4), (a, b + 4, a + 4)))
    folded_faces.extend(((12, 13, 14), (12, 14, 15)))
    payloads = {
        "cube": ("cube.stl", binary_stl()),
        "ascii": ("cube_ascii.stl", ascii_stl()),
        "unicode": ("中文 模型/方块.stl", binary_stl()),
        "open": ("open_cube.stl", binary_stl(faces=FACES[2:])),
        "two_solids": ("two_solids.stl", two),
        "folded_tube": ("folded_tube.stl", binary_stl(folded_vertices, folded_faces)),
        "colored": ("colored.stl", binary_stl(attribute=0x801F)),
        "truncated": ("truncated.stl", binary_stl()[:-1]),
    }
    result = {}
    for key, (name, contents) in payloads.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        result[key] = path
    return result


def is_sha256(value):
    return (isinstance(value, str) and len(value) == 64
            and all(character in "0123456789abcdef" for character in value))


def check_meshlib_candidate(report, source, output, worker, sdk_version):
    """Check independent Orca validation, not just the SDK's success flag."""
    assert report.get("backend") == "meshlib", report
    assert report.get("sdk_version") == sdk_version, report
    assert report.get("mesh_validated") is True, report
    assert report.get("candidate_file") == "candidate.stl", report
    assert report.get("export_round_trip") == "passed", report
    candidate = check_candidate_file(output / "candidate.stl")
    engine = report["engine"]
    assert isinstance(engine, dict), engine
    assert isinstance(engine.get("sidecar"), dict) and engine["sidecar"], "Missing original MeshLib sidecar report"
    sidecar = engine["sidecar"]
    assert sidecar.get("runtime_identity_verified") is True, sidecar
    libraries = sidecar.get("runtime_libraries")
    assert isinstance(libraries, list) and len(libraries) == 2, libraries
    assert {library.get("name") for library in libraries} == {"MRMesh", "MRVoxels"}, libraries
    for library in libraries:
        assert isinstance(library.get("path"), str) and library["path"], library
        assert is_sha256(library.get("sha256")), library
    inspection = engine["final_mesh_check"]["inspection"]
    assert inspection["mesh_status"] == "passed" and inspection["check_complete"] is True, inspection
    issues = inspection["issues"]
    assert set(issues) == {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection",
                          "degenerate", "duplicate", "orientation"}, issues
    assert all(issue["count"] == 0 and issue["complete"] is True for issue in issues.values()), issues
    assert report["resource_limits"]["max_input_faces"] == 3000000, report
    assert report["resource_limits"]["max_output_faces"] == 1000000, report
    artifacts = report["artifacts"]
    assert is_sha256(artifacts["input_sha256"]) and artifacts["input_sha256"] == digest(source), artifacts
    assert is_sha256(artifacts["candidate_sha256"]) and artifacts["candidate_sha256"] == candidate["sha256"], artifacts
    assert is_sha256(artifacts["worker_sha256"]) and artifacts["worker_sha256"] == digest(worker), artifacts
    assert isinstance(report.get("metrics"), dict) and report["metrics"], report
    return candidate


class Suite:
    def __init__(self, binary, worker, output, timeout, sdk_version):
        self.binary = binary
        self.worker = worker
        self.output = output
        self.timeout = timeout
        self.sdk_version = sdk_version
        self.results = []

    def run(self, name, source, *, expectation="rejected", engine="meshlib", voxel=VOXEL_MM,
            worker="explicit", flags=(), risk=True, existing_output=None, statuses=None,
            meshlib_contract=True):
        case = self.output / name
        case.mkdir()
        cwd = case / "working-directory"
        cwd.mkdir()
        output = existing_output if existing_output is not None else case / "candidate-output"
        output_before = directory_snapshot(output) if output.exists() else None
        source_before = digest(source)
        command = [str(self.binary), "--reconstruct-model", str(source), "--outputdir", str(output)]
        if engine is not None:
            command.extend(("--reconstruction-engine", engine))
        if voxel is not None:
            command.extend(("--reconstruction-voxel-mm", str(voxel)))
        actual_worker = self.worker if worker == "explicit" else worker
        if actual_worker is not None:
            command.extend(("--reconstruction-worker-path", str(actual_worker)))
        if risk:
            command.append("--accept-reconstruction-risk")
        command.extend(flags)
        save_json(case / "command.json", command)
        record = {"name": name, "command": command, "expectation": expectation, "passed": False,
                  "source_sha256_before": source_before, "evidence_directory": str(case)}
        start = time.monotonic()
        report = None
        try:
            with (case / "stdout.txt").open("wb") as stdout, (case / "stderr.txt").open("wb") as stderr:
                process = subprocess.run(command, cwd=cwd, stdout=stdout, stderr=stderr,
                                         timeout=self.timeout, check=False)
            record["returncode"] = process.returncode
            assert process.returncode in (0, 2), f"Unexpected process exit/crash: {process.returncode}"
            stdout = (case / "stdout.txt").read_text(encoding="utf-8", errors="replace")
            stderr = (case / "stderr.txt").read_text(encoding="utf-8", errors="replace")
            report = extract_report(stdout) or extract_report(stderr)
            assert isinstance(report, dict), "Missing structured reconstruction report"
            save_json(case / "report.json", report)
            check_report_contract(report)
            record["reported_status"] = report.get("status")
            if statuses is not None:
                assert report["status"] in statuses, report
            ready = report.get("status") == "candidate_ready"
            if expectation == "candidate":
                assert ready, report
            elif expectation == "rejected":
                assert not ready, report
            else:
                assert expectation == "candidate_or_rejected", expectation
            if ready:
                assert process.returncode == 0 and report.get("mesh_validated") is True, report
                if meshlib_contract:
                    resolved_worker = (self.binary.parent / ("OrcaMeshLibWorker.exe" if os.name == "nt" else "OrcaMeshLibWorker")
                                       if actual_worker is None else Path(actual_worker))
                    record["candidate"] = check_meshlib_candidate(report, source, output, resolved_worker, self.sdk_version)
                else:
                    record["candidate"] = check_candidate_file(output / "candidate.stl")
            else:
                assert process.returncode == 2 and report.get("mesh_validated") is False, report
                if output_before is None:
                    assert not (output / "candidate.stl").exists(), "Rejected result exported a candidate"
            if output_before is None and (output / "reconstruction.json").is_file():
                saved = json.loads((output / "reconstruction.json").read_text(encoding="utf-8"))
                assert saved == report, "Saved and stdout reports differ"
            elif output_before is not None:
                assert directory_snapshot(output) == output_before, "Existing output was overwritten"
            record["source_sha256_after"] = digest(source)
            assert record["source_sha256_after"] == source_before, "Source model was modified"
            assert not list(case.rglob("result.json")), "Entered the legacy result.json workflow"
            record["passed"] = True
        except subprocess.TimeoutExpired:
            record["error"] = "Harness deadline exceeded; not evidence of a supervised worker timeout."
        except Exception as error:
            record["error"] = f"{type(error).__name__}: {error}"
        finally:
            record["elapsed_seconds"] = round(time.monotonic() - start, 3)
            save_json(case / "case-summary.json", record)
            self.results.append(record)
            print(f"{'PASS' if record['passed'] else 'FAIL'} {name}: "
                  f"{record.get('reported_status', record.get('error'))}", flush=True)
        return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Already-built Orca executable, not an .app directory")
    parser.add_argument("--worker", type=Path, required=True, help="Already-built MeshLib sidecar executable")
    parser.add_argument("--output-dir", type=Path, required=True, help="Nonexistent output directory under an existing parent")
    parser.add_argument("--timeout", type=float, default=90., help="Harness deadline per invocation, at least 65 seconds")
    parser.add_argument("--sdk-version", default=EXPECTED_SDK_VERSION, help="Exact SDK identity expected in successful reports")
    args = parser.parse_args()
    try:
        binary, worker = args.binary.resolve(strict=True), args.worker.resolve(strict=True)
    except OSError as error:
        parser.error(str(error))
    for name, path in (("binary", binary), ("worker", worker)):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"--{name} must be an executable file")
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error("--output-dir must not exist and its parent must already exist")
    if not math.isfinite(args.timeout) or args.timeout < 65:
        parser.error("--timeout must be finite and at least 65 seconds")
    output.mkdir()
    started = datetime.now(timezone.utc).isoformat()
    binary_before, worker_before = digest(binary), digest(worker)
    fixtures = create_small_fixtures(output / "fixtures")
    fixtures_before = directory_snapshot(output / "fixtures")
    save_json(output / "fixture-manifest.json", fixtures_before)
    suite = Suite(binary, worker, output, args.timeout, args.sdk_version)
    first = suite.run("cube", fixtures["cube"], expectation="candidate")
    suite.run("ascii_cube", fixtures["ascii"], expectation="candidate")
    suite.run("unicode_paths_中文 空格", fixtures["unicode"], expectation="candidate")
    suite.run("open_cube", fixtures["open"], expectation="candidate_or_rejected")
    suite.run("two_separate_solids", fixtures["two_solids"], expectation="candidate_or_rejected")
    suite.run("folded_tube", fixtures["folded_tube"], expectation="candidate_or_rejected")
    suite.run("repeat_output_rejected", fixtures["cube"], existing_output=first)
    suite.run("missing_risk_acceptance", fixtures["cube"], risk=False)
    suite.run("missing_voxel", fixtures["cube"], voxel=None)
    for label, value in (("zero", "0"), ("negative", "-0.1"), ("nan", "nan"),
                         ("infinity", "inf"), ("float_underflow", "1e-100"),
                         ("float_overflow", "1e100"), ("trailing_text", "0.08junk")):
        suite.run("invalid_voxel_" + label, fixtures["cube"], voxel=value)
    for label, flags in (("alpha", ("--reconstruction-alpha", "0.5")),
                         ("offset", ("--reconstruction-offset", "0.05")),
                         ("no_close", ("--reconstruction-no-close-open-boundaries",)),
                         ("slice", ("--slice", "0")),
                         ("timeout", ("--reconstruction-timeout", "0.5"))):
        suite.run("reject_meshlib_" + label, fixtures["cube"], flags=flags)
    suite.run("unknown_engine", fixtures["cube"], engine="not-an-engine")
    suite.run("alpha_wrap_rejects_voxel", fixtures["cube"], engine="alpha-wrap", worker=None)
    suite.run("alpha_wrap_rejects_worker", fixtures["cube"], engine="alpha-wrap", voxel=None)
    suite.run("default_engine_rejects_meshlib_flags", fixtures["cube"], engine=None)
    suite.run("default_alpha_wrap_unchanged", fixtures["cube"], engine=None, voxel=None, worker=None,
              expectation="candidate", meshlib_contract=False)
    suite.run("missing_worker_no_fallback", fixtures["cube"], worker=output / "does-not-exist-worker")
    try:
        # Use an existing executable, not a missing relative path: otherwise
        # missing-file validation would conceal an accidental relative lookup.
        relative_worker = Path(os.path.relpath(worker, output / "relative_worker_path_rejected" / "working-directory"))
    except ValueError:
        suite.results.append({"name": "relative_worker_path_rejected", "skipped": True,
                              "reason": "Worker and evidence are on different Windows drives; no relative path exists."})
    else:
        suite.run("relative_worker_path_rejected", fixtures["cube"], worker=relative_worker)
    suite.run("directory_worker_rejected", fixtures["cube"], worker=output / "fixtures")
    # Executing the interpreter as if it were a sidecar cannot produce the
    # protocol. This uses no shell script, executable-bit trick or PATH lookup.
    suite.run("nonprotocol_process_rejected", fixtures["cube"], worker=Path(sys.executable).resolve())
    adjacent = binary.parent / ("OrcaMeshLibWorker.exe" if os.name == "nt" else "OrcaMeshLibWorker")
    if adjacent.is_file() and digest(adjacent) == worker_before:
        suite.run("adjacent_worker_discovery", fixtures["cube"], worker=None, expectation="candidate")
    elif not adjacent.exists():
        suite.run("missing_adjacent_worker_no_fallback", fixtures["cube"], worker=None)
    else:
        suite.results.append({"name": "adjacent_worker_discovery", "skipped": True,
                              "reason": "Adjacent executable differs from the selected worker; not executing unrelated code."})
    for key in ("colored", "truncated"):
        suite.run("reject_" + key, fixtures[key])
    # A deliberately impractical voxel size exercises the one-second budget.
    # Resource admission may reject first, which must not be called a timeout.
    suite.run("one_second_bounded_attempt", fixtures["cube"], voxel="0.000001",
              flags=("--reconstruction-timeout", "1"),
              statuses={"timed_out", "resource_limit", "memory_limit_exceeded", "engine_error", "failed", "rejected"})
    occupied = output / "existing-user-output"
    occupied.mkdir()
    (occupied / "keep.txt").write_text("Keep this user-owned output.\n", encoding="utf-8")
    (occupied / "candidate.stl").write_bytes(b"previous user candidate")
    suite.run("existing_output_preserved", fixtures["cube"], existing_output=occupied)
    invariants = {
        "fixtures_unchanged": directory_snapshot(output / "fixtures") == fixtures_before,
        "binary_unchanged": digest(binary) == binary_before,
        "worker_unchanged": digest(worker) == worker_before,
        "no_legacy_result_json": not list(output.rglob("result.json")),
    }
    failed = sum(not item.get("passed", False) for item in suite.results if not item.get("skipped", False))
    summary = {
        "schema": "meshlib_reconstruction_cli_regression_v1", "started_utc": started,
        "finished_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
        "binary_sha256": binary_before, "worker": str(worker), "worker_sha256": worker_before,
        "expected_sdk_version": args.sdk_version, "platform": platform.platform(), "python": sys.version,
        "passed": sum(item.get("passed", False) for item in suite.results), "failed": failed,
        "skipped": sum(item.get("skipped", False) for item in suite.results), "invariants": invariants,
        "worker_timeout_observed": any(item.get("reported_status") == "timed_out" for item in suite.results),
        "trial_slicing_performed": False, "cases": suite.results,
        "success": failed == 0 and all(invariants.values()),
    }
    save_json(output / "summary.json", summary)
    print(f"Summary: {summary['passed']} passed, {failed} failed, {summary['skipped']} skipped; {output / 'summary.json'}", flush=True)
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
