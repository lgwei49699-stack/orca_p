#!/usr/bin/env python3
"""Exercise the optional in-process MeshLib CLI with small isolated fixtures.

Requires an already-built Orca executable with the SDK enabled, or explicitly
--expect-unavailable for a build without it. Does not build, slice, print,
download an SDK, modify an input, or overwrite an output directory. A cooperative
timeout is not a hard deadline; a test-harness deadline is always a test failure.
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


def check_execution_contract(report, process_id):
    execution = report["execution"]
    assert execution["mode"] == "in_process", execution
    assert execution["process_id"] == process_id, execution
    assert execution["hard_timeout"] is False, execution
    assert execution["crash_isolation"] is False, execution
    limits = report["resource_limits"]
    assert limits["budget_mode"] == "cooperative_process_rss", limits
    assert limits["hard_memory_limit"] is False, limits


def check_meshlib_candidate(report, source, output, process_id, sdk_version):
    """Check independent Orca validation, not just the SDK's success flag."""
    assert report.get("backend") == "meshlib", report
    assert report.get("sdk_version") == sdk_version, report
    assert report.get("mesh_validated") is True, report
    assert report.get("candidate_file") == "candidate.stl", report
    assert report.get("export_round_trip") == "passed", report
    candidate = check_candidate_file(output / "candidate.stl")
    engine = report["engine"]
    assert isinstance(engine, dict), engine
    check_execution_contract(report, process_id)
    assert "sidecar" not in engine, "Legacy external worker report remained in the direct SDK path"
    assert isinstance(engine.get("sdk"), dict) and engine["sdk"], "Missing direct MeshLib SDK report"
    sdk = engine["sdk"]
    assert sdk.get("runtime_identity_verified") is True, sdk
    libraries = sdk.get("runtime_libraries")
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
    assert "worker_sha256" not in artifacts, "Direct SDK path must not identify a separate worker executable"
    assert isinstance(report.get("metrics"), dict) and report["metrics"], report
    return candidate


def process_descendants(process_id):
    """Observe child processes without optional dependencies or shell execution.

    A sampled absence does not prove a child never existed. The independent
    same-PID report assertion is also required; Windows records this observation
    as unavailable rather than claiming a check it did not execute.
    """
    if os.name != "posix":
        return None
    snapshot = subprocess.run(["ps", "-axo", "pid=,ppid=,command="], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=5, check=True)
    entries = []
    for line in snapshot.stdout.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) >= 2 and fields[0].isdigit() and fields[1].isdigit():
            entries.append((int(fields[0]), int(fields[1]), fields[2] if len(fields) == 3 else ""))
    parents = {process_id}
    result = {}
    while True:
        descendants = {pid for pid, parent, command in entries if parent in parents and pid not in parents}
        if not descendants:
            break
        result.update({pid: command for pid, parent, command in entries if pid in descendants})
        parents.update(descendants)
    return result


class Suite:
    def __init__(self, binary, output, timeout, sdk_version):
        self.binary = binary
        self.output = output
        self.timeout = timeout
        self.sdk_version = sdk_version
        self.results = []

    def run(self, name, source, *, expectation="rejected", engine="meshlib", voxel=VOXEL_MM,
            flags=(), risk=True, existing_output=None, statuses=None,
            meshlib_contract=True, message_contains=None, expected_components=None,
            admission_rejected=False, expected_memory_limit_mb=None):
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
        if risk:
            command.append("--accept-reconstruction-risk")
        command.extend(flags)
        save_json(case / "command.json", command)
        record = {"name": name, "command": command, "expectation": expectation, "passed": False,
                  "source_sha256_before": source_before, "evidence_directory": str(case)}
        start = time.monotonic()
        report = None
        process = None
        try:
            with (case / "stdout.txt").open("wb") as stdout, (case / "stderr.txt").open("wb") as stderr:
                process = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr)
                record["process_id"] = process.pid
                record["descendant_observation"] = {"available": os.name == "posix", "samples": 0,
                                                    "observed": {}, "sampling_interval_seconds": 0.1,
                                                    "status": "sampled" if os.name == "posix" else "not_available",
                                                    "limitation": "Sampled absence does not prove no child ever existed."
                                                    if os.name == "posix" else
                                                    "No process-tree probe on this platform; only the same-PID report is checked."}
                next_observation = start
                while process.poll() is None:
                    now = time.monotonic()
                    if now - start > self.timeout:
                        raise subprocess.TimeoutExpired(command, self.timeout)
                    if engine == "meshlib" and now >= next_observation:
                        observation = process_descendants(process.pid)
                        if observation is not None:
                            record["descendant_observation"]["samples"] += 1
                            record["descendant_observation"]["observed"].update(observation)
                        next_observation = time.monotonic() + 0.1
                    time.sleep(0.02)
            record["returncode"] = process.returncode
            assert process.returncode in (0, 2), f"Unexpected process exit/crash: {process.returncode}"
            stdout = (case / "stdout.txt").read_text(encoding="utf-8", errors="replace")
            stderr = (case / "stderr.txt").read_text(encoding="utf-8", errors="replace")
            report = extract_report(stdout) or extract_report(stderr)
            assert isinstance(report, dict), "Missing structured reconstruction report"
            save_json(case / "report.json", report)
            check_report_contract(report)
            if engine == "meshlib" and record["descendant_observation"]["available"]:
                assert not record["descendant_observation"]["observed"], "MeshLib spawned a child process"
            if message_contains is not None:
                assert message_contains.lower() in report["message"].lower(), report
            record["reported_status"] = report.get("status")
            if "resource_limits" in report:
                record["memory_limit_mb"] = report["resource_limits"]["memory_limit_mb"]
            if statuses is not None:
                assert report["status"] in statuses, report
            if engine == "meshlib" and report.get("execution"):
                check_execution_contract(report, process.pid)
            if expected_memory_limit_mb is not None:
                assert report["resource_limits"]["memory_limit_mb"] == expected_memory_limit_mb, report
            if admission_rejected:
                admission = report.get("admission", {})
                assert admission.get("accepted") is False and admission.get("reason"), report
                assert admission.get("estimate_is_upper_bound") is False, report
                assert not report.get("engine", {}).get("sdk"), "Admission rejection still entered the SDK"
                record["admission_reason"] = admission["reason"]
            ready = report.get("status") == "candidate_ready"
            if expectation == "candidate":
                assert ready, report
            elif expectation == "rejected":
                assert not ready, report
            else:
                assert expectation == "candidate_or_rejected", expectation
            if ready:
                assert process.returncode == 0 and report.get("mesh_validated") is True, report
                assert (output / "reconstruction.json").is_file(), "Candidate success did not save its report"
                if meshlib_contract:
                    record["candidate"] = check_meshlib_candidate(report, source, output, process.pid, self.sdk_version)
                    if "--reconstruction-memory-mb" not in command:
                        assert 128 <= record["memory_limit_mb"] <= 1024, "Small-model automatic memory budget changed"
                else:
                    record["candidate"] = check_candidate_file(output / "candidate.stl")
                if expected_components is not None:
                    assert report["metrics"]["component_count"] == expected_components, report
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
            record["error"] = "Harness deadline exceeded; not evidence of a cooperative SDK timeout."
        except Exception as error:
            record["error"] = f"{type(error).__name__}: {error}"
        finally:
            if process is not None and process.poll() is None:
                # Only terminate this test's process. Do not kill user apps or
                # claim this harness intervention was application cancellation.
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            record["elapsed_seconds"] = round(time.monotonic() - start, 3)
            save_json(case / "case-summary.json", record)
            self.results.append(record)
            print(f"{'PASS' if record['passed'] else 'FAIL'} {name}: "
                  f"{record.get('reported_status', record.get('error'))}", flush=True)
        return output


def main():
    # Windows redirected stdout may use an ANSI code page that cannot encode a
    # Unicode fixture path. Keep human-readable output valid instead of failing
    # after a successful application invocation; JSON evidence remains UTF-8.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Already-built Orca executable, not an .app directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="Nonexistent output directory under an existing parent")
    parser.add_argument("--timeout", type=float, default=90., help="Harness deadline per invocation, at least 65 seconds")
    parser.add_argument("--sdk-version", default=EXPECTED_SDK_VERSION, help="Exact SDK identity expected in successful reports")
    parser.add_argument("--expect-unavailable", action="store_true", help="Test the SDK-OFF rejection and default Alpha Wrap path")
    parser.add_argument("--timeout-model", type=Path,
                        help="Optional known-slow STL for an actual one-second cooperative timeout (not modified)")
    args = parser.parse_args()
    try:
        binary = args.binary.resolve(strict=True)
    except OSError as error:
        parser.error(str(error))
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error("--binary must be an executable file")
    if args.timeout_model is not None:
        try:
            args.timeout_model = args.timeout_model.resolve(strict=True)
        except OSError as error:
            parser.error(str(error))
        if args.expect_unavailable or not args.timeout_model.is_file():
            parser.error("--timeout-model requires an existing file and an SDK-enabled build")
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error("--output-dir must not exist and its parent must already exist")
    if not math.isfinite(args.timeout) or args.timeout < 65:
        parser.error("--timeout must be finite and at least 65 seconds")
    output.mkdir()
    started = datetime.now(timezone.utc).isoformat()
    binary_before = digest(binary)
    fixtures = create_small_fixtures(output / "fixtures")
    fixtures_before = directory_snapshot(output / "fixtures")
    save_json(output / "fixture-manifest.json", fixtures_before)
    suite = Suite(binary, output, args.timeout, args.sdk_version)
    default_memory_limit_mb = None
    if args.expect_unavailable:
        for key in ("cube", "ascii", "unicode"):
            suite.run("sdk_off_" + key, fixtures[key], statuses={"failed"},
                      message_contains="does not include the MeshLib SDK backend")
    else:
        first = suite.run("cube", fixtures["cube"], expectation="candidate", expected_components=1)
        default_memory_limit_mb = suite.results[-1].get("memory_limit_mb")
        suite.run("ascii_cube", fixtures["ascii"], expectation="candidate", expected_components=1)
        suite.run("unicode_paths_中文 空格", fixtures["unicode"], expectation="candidate", expected_components=1)
        suite.run("open_cube", fixtures["open"], expectation="candidate", expected_components=1)
        suite.run("two_separate_solids", fixtures["two_solids"], expectation="candidate", expected_components=2)
        suite.run("folded_tube", fixtures["folded_tube"], expectation="candidate", expected_components=1)
        suite.run("repeat_output_rejected", fixtures["cube"], existing_output=first)
        suite.run("explicit_memory_2048", fixtures["cube"], expectation="candidate", expected_components=1,
                  flags=("--reconstruction-memory-mb", "2048"), expected_memory_limit_mb=2048)
        suite.run("explicit_memory_maximum_4096", fixtures["cube"], expectation="candidate", expected_components=1,
                  flags=("--reconstruction-memory-mb", "4096"), expected_memory_limit_mb=4096)
        suite.run("explicit_memory_minimum_128", fixtures["cube"],
                  flags=("--reconstruction-memory-mb", "128"), expected_memory_limit_mb=128,
                  statuses={"resource_limit", "memory_limit_exceeded"})
    suite.run("missing_risk_acceptance", fixtures["cube"], risk=False)
    suite.run("missing_voxel", fixtures["cube"], voxel=None)
    for label, value in (("zero", "0"), ("negative", "-0.1"), ("nan", "nan"),
                         ("infinity", "inf"), ("float_underflow", "1e-100"),
                         ("float_overflow", "1e100"), ("trailing_text", "0.08junk")):
        suite.run("invalid_voxel_" + label, fixtures["cube"], voxel=value)
    for label, value in (("zero", "0"), ("negative", "-1"), ("below_minimum", "127"),
                         ("above_maximum", "4097"), ("fractional", "128.5"),
                         ("nan", "nan"), ("infinity", "inf"), ("text", "invalid")):
        suite.run("invalid_memory_" + label, fixtures["cube"], flags=("--reconstruction-memory-mb", value),
                  message_contains="memory budget must be an integer from 128 to 4096")
    suite.run("missing_memory_value", fixtures["cube"], flags=("--reconstruction-memory-mb",),
              message_contains="Missing reconstruction option value")
    for label, flags in (("alpha", ("--reconstruction-alpha", "0.5")),
                         ("offset", ("--reconstruction-offset", "0.05")),
                         ("no_close", ("--reconstruction-no-close-open-boundaries",)),
                         ("slice", ("--slice", "0")),
                         ("timeout", ("--reconstruction-timeout", "0.5"))):
        suite.run("reject_meshlib_" + label, fixtures["cube"], flags=flags)
    suite.run("unknown_engine", fixtures["cube"], engine="not-an-engine")
    suite.run("alpha_wrap_rejects_voxel", fixtures["cube"], engine="alpha-wrap")
    suite.run("default_engine_rejects_meshlib_flags", fixtures["cube"], engine=None)
    suite.run("default_alpha_wrap_unchanged", fixtures["cube"], engine=None, voxel=None,
              expectation="candidate", meshlib_contract=False, expected_memory_limit_mb=default_memory_limit_mb)
    # This former option must be unknown even when it names a valid executable.
    suite.run("legacy_worker_path_rejected", fixtures["cube"],
              flags=("--reconstruction-worker-path", str(Path(sys.executable).resolve())),
              message_contains="--reconstruction-worker-path")
    for key in ("colored", "truncated"):
        suite.run("reject_" + key, fixtures[key])
    if not args.expect_unavailable:
        # Impossible resolution must be rejected before entering the SDK. It is
        # not timeout evidence; a coarse substitute candidate is also forbidden.
        suite.run("overfine_voxel_admission_rejected", fixtures["cube"], voxel="0.000001",
                  statuses={"resource_limit"}, admission_rejected=True)
        if args.timeout_model is not None:
            suite.run("one_second_cooperative_timeout", args.timeout_model,
                      flags=("--reconstruction-timeout", "1"), statuses={"timed_out"})
    occupied = output / "existing-user-output"
    occupied.mkdir()
    (occupied / "keep.txt").write_text("Keep this user-owned output.\n", encoding="utf-8")
    (occupied / "candidate.stl").write_bytes(b"previous user candidate")
    suite.run("existing_output_preserved", fixtures["cube"], existing_output=occupied)
    invariants = {
        "fixtures_unchanged": directory_snapshot(output / "fixtures") == fixtures_before,
        "binary_unchanged": digest(binary) == binary_before,
        "no_legacy_result_json": not list(output.rglob("result.json")),
    }
    failed = sum(not item.get("passed", False) for item in suite.results if not item.get("skipped", False))
    summary = {
        "schema": "meshlib_reconstruction_cli_regression_v2", "started_utc": started,
        "finished_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
        "binary_sha256": binary_before, "expect_unavailable": args.expect_unavailable,
        "expected_sdk_version": args.sdk_version, "platform": platform.platform(), "python": sys.version,
        "passed": sum(item.get("passed", False) for item in suite.results), "failed": failed,
        "skipped": sum(item.get("skipped", False) for item in suite.results), "invariants": invariants,
        "cooperative_timeout_observed": any(item.get("reported_status") == "timed_out" for item in suite.results),
        "process_observation_limit": "Child process observations are sampled, not an exhaustive execution trace.",
        "descendant_process_check": "sampled_on_posix" if os.name == "posix" else "not_available_on_this_platform",
        "trial_slicing_performed": False, "cases": suite.results,
        "success": failed == 0 and all(invariants.values()),
    }
    save_json(output / "summary.json", summary)
    print(f"Summary: {summary['passed']} passed, {failed} failed, {summary['skipped']} skipped; {output / 'summary.json'}", flush=True)
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
