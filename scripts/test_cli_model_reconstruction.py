#!/usr/bin/env python3
"""Run isolated reconstruction CLI regressions against an already built application.

Creates its fixtures and evidence only inside a new --output-dir. Never builds,
slices, prints, overwrites a previous evidence directory, or edits the sources.
The final float geometry remains the application's responsibility; these tests
check the CLI contract, rejection behavior, serialization and file preservation.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import time


VERTICES = [(2., 2., 0.), (2., 0., 0.), (0., 0., 0.), (0., 2., 0.),
            (2., 2., 2.), (0., 2., 2.), (0., 0., 2.), (2., 0., 2.)]
FACES = [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7),
         (0, 4, 7), (0, 7, 1), (1, 7, 6), (1, 6, 2),
         (2, 6, 5), (2, 5, 3), (4, 0, 3), (4, 3, 5)]
PARAMETERS = {"alpha_mm": 0.5, "offset_mm": 0.05, "max_deviation_mm": 0.25, "close_open_boundaries": True,
              "max_input_faces": 3000000, "max_output_faces": 1000000}


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def directory_snapshot(path):
    return {str(item.relative_to(path)): digest(item)
            for item in sorted(path.rglob("*")) if item.is_file()}


def binary_stl(vertices=VERTICES, faces=FACES, *, attribute=0, header=b"Orca reconstruction regression"):
    data = bytearray(header[:80].ljust(80, b" ") + struct.pack("<I", len(faces)))
    for face in faces:
        xyz = [value for vertex in face for value in vertices[vertex]]
        data.extend(struct.pack("<12fH", 0., 0., 0., *xyz, attribute))
    return bytes(data)


def ascii_stl(name="cube", *, extra_line="", vertices=VERTICES, faces=FACES):
    lines = ["solid " + name]
    if extra_line:
        lines.append(extra_line)
    for face in faces:
        lines.extend(("  facet normal 0 0 0", "    outer loop"))
        lines.extend("      vertex " + " ".join(format(value, ".9g") for value in vertices[index]) for index in face)
        lines.extend(("    endloop", "  endfacet"))
    lines.append("endsolid " + name)
    return ("\n".join(lines) + "\n").encode("utf-8")


def create_fixtures(directory):
    directory.mkdir()
    shifted = [(x + 4., y, z) for x, y, z in VERTICES]
    two_solids = binary_stl(VERTICES + shifted, FACES + [tuple(index + 8 for index in face) for face in FACES])
    nan_vertices = list(VERTICES)
    nan_vertices[0] = (float("nan"), 2., 0.)
    # Same closed, single-shell self-intersection as the core folded_tube test.
    folded_vertices = [(float(ring), y, z) for ring in range(4)
                       for y, z in ((0., 0.), (1., 0.), (1., 1.), (0., 1.))]
    folded_vertices[6] = (0.5, 0.5, -0.2)
    folded_faces = [(0, 2, 1), (0, 3, 2)]
    for ring in range(3):
        for corner in range(4):
            a, b = ring * 4 + corner, ring * 4 + (corner + 1) % 4
            folded_faces.extend(((a, b, b + 4), (a, b + 4, a + 4)))
    folded_faces.extend(((12, 13, 14), (12, 14, 15)))
    fixtures = {
        "binary": ("cube_binary.stl", binary_stl()),
        "ascii": ("cube_ascii.stl", ascii_stl()),
        "open": ("open_cube.stl", binary_stl(faces=FACES[2:])),
        "two_solids": ("two_solids.stl", two_solids),
        "folded_tube": ("folded_tube.stl", binary_stl(folded_vertices, folded_faces)),
        "color_attribute": ("colored_attribute.stl", binary_stl(attribute=0x801F)),
        "color_header": ("colored_header.stl", binary_stl(header=b"COLOR=\xff\x00\x00\xff")),
        "nan_binary": ("nan_binary.stl", binary_stl(nan_vertices)),
        "nan_ascii": ("nan_ascii.stl", ascii_stl(vertices=nan_vertices)),
        "truncated": ("truncated.stl", binary_stl()[:-1]),
        "long_name": ("long_name.stl", ascii_stl("Long solid name " + "x" * 512)),
        "overlong_name": ("overlong_name.stl", ascii_stl("x" * 5000)),
        "mw_field": ("mw_field.stl", ascii_stl(extra_line="MW 0.5")),
        "unicode": ("中文 模型/方块 模型.stl", ascii_stl("中文方块")),
    }
    result = {}
    for key, (name, data) in fixtures.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        result[key] = path
    # Exercise the inclusive input bound AND the old 64 MiB file gate without
    # storing a huge fixture in Git. Repeated triangles test IO/cleanup, not the
    # performance of a genuinely dense model (which needs a separate real trial).
    face_limit = PARAMETERS["max_input_faces"]
    repeated = directory / "at_input_limit.stl"
    with repeated.open("wb") as stream:
        stream.write(b"Large input regression".ljust(80, b" ") + struct.pack("<I", face_limit))
        block = binary_stl()[84:] * 1000
        for _ in range(face_limit // (len(FACES) * 1000)):
            stream.write(block)
    assert repeated.stat().st_size == 84 + 50 * face_limit
    result["large_input"] = repeated
    for name, count, size in (("too_many_faces", face_limit + 1, 84 + 50 * (face_limit + 1)),
                              ("too_many_bytes", 0, 256 * 1024 * 1024 + 1)):
        path = directory / (name + ".stl")
        with path.open("wb") as stream:
            stream.write(b"Oversized input regression".ljust(80, b" ") + struct.pack("<I", count))
            stream.truncate(size)
        result[name] = path
    return result


def extract_report(text):
    decoder = json.JSONDecoder()
    for index, character in enumerate(text):
        if character != "{":
            continue
        try:
            value, _ = decoder.raw_decode(text[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and value.get("schema") == "model_reconstruction_v1":
            return value
    return None


def check_report_contract(report):
    assert report.get("schema") == "model_reconstruction_v1", report
    assert report.get("trial_slice") == "not_run", report
    assert report.get("print_ready") is False, report
    assert report.get("applied") is False, report
    assert report.get("shape_confirmation") == "not_confirmed", report
    assert report.get("requires_manual_confirmation") is True, report
    assert isinstance(report.get("message"), str) and report["message"], report


def check_candidate_file(path):
    assert path.is_file(), "No candidate STL was exported"
    data = path.read_bytes()
    assert len(data) >= 84, "Truncated candidate STL"
    faces = struct.unpack_from("<I", data, 80)[0]
    assert 0 < faces <= 1000000 and len(data) == 84 + faces * 50, "Invalid candidate STL size/count"
    for offset in range(84, len(data), 50):
        values = struct.unpack_from("<12fH", data, offset)
        assert all(math.isfinite(value) for value in values[:12]), "Candidate contains non-finite numbers"
    return {"triangles": faces, "sha256": digest(path)}


class Suite:
    def __init__(self, binary, output, wall_timeout):
        self.binary = binary
        self.output = output
        self.wall_timeout = wall_timeout
        self.results = []

    def run(self, name, source, *, expectation="rejected", flags=(), risk=True, existing_output=None,
            allowed_statuses=None, message_contains=None, worker=False, expect_ambiguous_interior=False,
            expect_closure=None, automatic_budget=False, expected_timeout_ms=None):
        case = self.output / name
        case.mkdir()
        cwd = case / "working-directory"
        cwd.mkdir()
        export = existing_output if existing_output is not None else case / "candidate-output"
        before_output = directory_snapshot(export) if export.exists() else None
        input_hash = directory_snapshot(source) if worker else {str(source): digest(source)}
        if worker:
            command = [str(self.binary), "--reconstruction-worker", str(source)]
        else:
            command = [str(self.binary), "--reconstruct-model", "--outputdir", str(export)]
            if not automatic_budget:
                command.extend(("--reconstruction-timeout", "10"))
            if risk:
                command.append("--accept-reconstruction-risk")
            command.extend(flags)
            command.append(str(source))
        save_json(case / "command.json", command)
        record = {"name": name, "command": command, "expectation": expectation, "passed": False,
                  "source_hashes_before": input_hash, "evidence_directory": str(case)}
        start = time.monotonic()
        report = None
        try:
            with (case / "stdout.txt").open("wb") as stdout, (case / "stderr.txt").open("wb") as stderr:
                completed = subprocess.run(command, cwd=cwd, stdout=stdout, stderr=stderr, timeout=self.wall_timeout, check=False)
            record["returncode"] = completed.returncode
            assert completed.returncode in ((0,) if worker else (0, 2)), f"Unexpected exit/crash: {completed.returncode}"
            stdout_text = (case / "stdout.txt").read_text(encoding="utf-8", errors="replace")
            stderr_text = (case / "stderr.txt").read_text(encoding="utf-8", errors="replace")
            if worker:
                report = json.loads((source / "report.json").read_text(encoding="utf-8"))
            else:
                report = extract_report(stdout_text) or extract_report(stderr_text)
            assert isinstance(report, dict), "No structured report was returned"
            save_json(case / "report.json", report)
            record["reported_status"] = report.get("status")
            check_report_contract(report)
            if expected_timeout_ms is not None:
                assert report["resource_limits"]["timeout_ms"] == expected_timeout_ms, report
                assert report["resource_limits"]["max_input_faces"] == PARAMETERS["max_input_faces"], report
            if message_contains is not None:
                assert message_contains.lower() in report["message"].lower(), report
            if allowed_statuses is not None:
                assert report.get("status") in allowed_statuses, report
            candidate_ready = report.get("status") == "candidate_ready"
            if expectation == "candidate":
                assert candidate_ready, report
            elif expectation == "rejected":
                assert not candidate_ready, report
            else:
                assert expectation == "candidate_or_rejected", expectation
            if candidate_ready:
                assert not worker and completed.returncode == 0 and report.get("mesh_validated") is True, report
                assert report.get("candidate_file") == "candidate.stl", report
                assert report.get("export_round_trip") == "passed", report
                assert report.get("engine", {}).get("validation", {}).get("requires_manual_confirmation") is True, report
                assert report.get("convex_hull_validation", {}).get("status") == "validated_mesh", report
                assert report["convex_hull_validation"]["validation"]["complete"] is True, report
                assert len(report.get("convex_hull_mesh_identity", "")) == 40, report
                if expect_ambiguous_interior:
                    assert report["engine"]["metrics"]["deviation_policy"] == "warning_only", report
                    assert report["engine"]["validation"]["reference_interior_check_complete"] is False, report
                    assert report["engine"]["validation"]["interior_check_status"] == "not_preserved", report
                    assert report["engine"]["final_mesh_check"]["inspection"]["check_complete"] is True, report
                inspection = report["engine"]["final_mesh_check"]["inspection"]
                assert inspection["check_complete"] is True and inspection["mesh_status"] == "passed", report
                assert len(inspection["issues"]) == 7, report
                assert all(item["complete"] and item["count"] == 0 for item in inspection["issues"].values()), report
                assert 1 <= len(report["engine"]["attempts"]) <= 3, report
                if expect_closure is not None:
                    assert report["parameters"]["close_open_boundaries"] is expect_closure, report
                    assert report["engine"]["boundary_closure"]["used_for_wrapping"] is expect_closure, report
                    assert report["metrics"]["open_surface_envelope_warning"] is not expect_closure, report
                    if expect_closure:
                        assert report["metrics"]["output_volume_mm3"] > 8., report
                    else:
                        assert report["metrics"]["output_volume_mm3"] < 4., report
                record["candidate"] = check_candidate_file(export / "candidate.stl")
            else:
                assert report.get("mesh_validated") is False, report
                assert worker or completed.returncode == 2, report
                if before_output is None:
                    assert not (export / "candidate.stl").exists(), "A rejected candidate leaked into the output"
            if worker:
                assert not (source / "candidate.mesh").exists(), "An invalid protocol input produced a candidate"
                assert not (source / "candidate.local.mesh").exists(), "An invalid protocol input produced a local candidate"
                assert not (source / "candidate.hull.mesh").exists(), "An invalid protocol input produced a convex hull"
                after_input = {relative: digest(source / relative) for relative in input_hash}
            else:
                after_input = {str(source): digest(source)}
                if before_output is None and (export / "reconstruction.json").exists():
                    saved_report = json.loads((export / "reconstruction.json").read_text(encoding="utf-8"))
                    assert saved_report == report, "Console and saved reports disagree"
            record["source_hashes_after"] = after_input
            assert after_input == input_hash, "Source input was modified"
            if before_output is not None:
                assert directory_snapshot(export) == before_output, "An existing output directory was changed"
            assert not list(case.rglob("result.json")), "Reconstruction entered a legacy result.json workflow"
            record["passed"] = True
        except subprocess.TimeoutExpired:
            record["error"] = "Harness deadline exceeded; this is a failure, not a passing worker-timeout test."
        except Exception as error:
            record["error"] = f"{type(error).__name__}: {error}"
        finally:
            record["elapsed_seconds"] = round(time.monotonic() - start, 3)
            if not (case / "report.json").exists():
                save_json(case / "report.json", {"harness_error": record.get("error", "No report")})
            save_json(case / "case-summary.json", record)
            self.results.append(record)
            print(f"{'PASS' if record['passed'] else 'FAIL'} {name}: {record.get('reported_status', record.get('error'))}", flush=True)
        return export

    def skipped(self, name, reason):
        case = self.output / name
        case.mkdir()
        record = {"name": name, "skipped": True, "reason": reason, "evidence_directory": str(case)}
        save_json(case / "report.json", record)
        (case / "stdout.txt").write_text("", encoding="utf-8")
        (case / "stderr.txt").write_text(reason + "\n", encoding="utf-8")
        save_json(case / "case-summary.json", record)
        self.results.append(record)
        print(f"SKIP {name}: {reason}", flush=True)


def create_protocol_fixture(directory, *, invalid_index=False, nonfinite=False):
    directory.mkdir()
    vertices = list(VERTICES)
    faces = list(FACES)
    if nonfinite:
        vertices[0] = (float("nan"), 2., 0.)
    if invalid_index:
        faces[0] = (-1, 1, 2)
    # Protocol fields use the host byte order, exactly like this same-host worker.
    payload = b"".join(struct.pack("=3f", *v) for v in vertices)
    payload += b"".join(struct.pack("=3i", *f) for f in faces)
    data = b"ORCARC01" + struct.pack("=QQ", len(vertices), len(faces)) + payload
    (directory / "input.mesh").write_bytes(data)
    save_json(directory / "request.json", {
        "schema": "reconstruction_worker_v1", "source_identity": "cli-regression-invalid-protocol",
        "mesh_identity": hashlib.sha1(data).hexdigest(), "memory_limit_mb": 2048, "parameters": PARAMETERS,
        "parent_pid": os.getpid(), "timeout_ms": 60000,
        "to_mm": [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.],
    })
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Already built application executable, not an .app directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="New evidence directory under an existing parent")
    parser.add_argument("--timeout", type=float, default=30., help="Harness deadline per invocation; must exceed the 10-second worker budget")
    args = parser.parse_args()
    try:
        binary = args.binary.resolve(strict=True)
    except OSError as error:
        parser.error(str(error))
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error("--binary must be an executable file")
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error("--output-dir must not exist, and its parent must already exist")
    if not math.isfinite(args.timeout) or args.timeout < 15:
        parser.error("--timeout must be finite and at least 15 seconds")
    output.mkdir()
    print(f"Evidence directory: {output}", flush=True)
    fixtures = create_fixtures(output / "fixtures")
    fixture_hashes = directory_snapshot(output / "fixtures")
    save_json(output / "fixture-manifest.json", fixture_hashes)
    suite = Suite(binary, output, args.timeout)
    started = datetime.now(timezone.utc).isoformat()
    initial_binary_hash = digest(binary)

    first_export = suite.run("binary_cube", fixtures["binary"], expectation="candidate")
    suite.run("repeat_same_output", fixtures["binary"], existing_output=first_export)
    suite.run("ascii_cube", fixtures["ascii"], expectation="candidate")
    suite.run("unicode_paths_中文 空格", fixtures["unicode"], expectation="candidate")
    suite.run("ascii_name_over_255", fixtures["long_name"], expectation="candidate")
    suite.run("self_intersection_approximate_candidate", fixtures["folded_tube"], expectation="candidate",
              flags=("--reconstruction-max-deviation", "0.001"), expect_ambiguous_interior=True)
    suite.run("ascii_mw_extension", fixtures["mw_field"], expectation="candidate_or_rejected")
    suite.run("open", fixtures["open"], expectation="candidate", expect_closure=True)
    suite.run("open_surface_wrap_opt_out", fixtures["open"], expectation="candidate",
              flags=("--reconstruction-no-close-open-boundaries",), expect_closure=False)
    suite.run("two_solids", fixtures["two_solids"], expectation="candidate")
    suite.run("automatic_small_budget", fixtures["binary"], expectation="candidate",
              automatic_budget=True, expected_timeout_ms=60000)
    suite.run("large_input_at_limit", fixtures["large_input"], expectation="candidate",
              automatic_budget=True, expected_timeout_ms=300000)
    suite.run("large_input_explicit_timeout", fixtures["large_input"], expectation="candidate_or_rejected",
              flags=("--reconstruction-timeout", "1"), allowed_statuses={"candidate_ready", "timed_out"}, expected_timeout_ms=1000)
    suite.run("over_input_face_limit", fixtures["too_many_faces"], message_contains="face limit")
    suite.run("over_input_file_limit", fixtures["too_many_bytes"], message_contains="256 MiB")
    for name in ("color_attribute", "color_header", "nan_binary", "nan_ascii", "truncated", "overlong_name"):
        suite.run(name, fixtures[name])
    suite.run("missing_risk_acceptance", fixtures["binary"], risk=False)
    for name, value in (("zero", "0"), ("negative", "-1"), ("nan", "nan"), ("trailing_text", "0.5junk")):
        suite.run("invalid_alpha_" + name, fixtures["binary"], flags=("--reconstruction-alpha", value))
    suite.run("invalid_offset_ratio", fixtures["binary"], flags=("--reconstruction-offset", "0.2"))
    suite.run("invalid_timeout", fixtures["binary"], flags=("--reconstruction-timeout", "0.5"))
    # Deliberately exceeds the estimated refinement budget. A one-second worker
    # deadline may win instead. This does not replace the dedicated process tests
    # which verify an actual timeout, descendant termination and reaping.
    suite.run("one_second_bounded_attempt", fixtures["binary"],
              flags=("--reconstruction-timeout", "1", "--reconstruction-alpha", "0.002", "--reconstruction-offset", "0.0002"),
              allowed_statuses={"timed_out", "resource_limit"})
    occupied = output / "preexisting-output"
    occupied.mkdir()
    (occupied / "keep.txt").write_text("User-owned output must remain unchanged.\n", encoding="utf-8")
    (occupied / "candidate.stl").write_bytes(b"existing user candidate; not an STL")
    suite.run("preexisting_output_preserved", fixtures["binary"], existing_output=occupied)

    for name, kwargs in (("protocol_invalid_index", {"invalid_index": True}), ("protocol_nan", {"nonfinite": True})):
        if os.name == "nt":
            suite.skipped(name, "Direct worker invocation cannot satisfy the Windows parent Job contract; use the C++ worker tests.")
        else:
            protocol = create_protocol_fixture(output / (name + "-input"), **kwargs)
            suite.run(name, protocol, worker=True, allowed_statuses={"failed"},
                      message_contains="invalid face index" if kwargs.get("invalid_index") else "non-finite")

    invariants = {
        "fixtures_unchanged": directory_snapshot(output / "fixtures") == fixture_hashes,
        "binary_unchanged": digest(binary) == initial_binary_hash,
        "no_legacy_result_json": not list(output.rglob("result.json")),
    }
    failed = sum(not item.get("passed", False) for item in suite.results if not item.get("skipped", False))
    summary = {"schema": "reconstruction_cli_regression_v1", "started_utc": started,
               "finished_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
               "binary_sha256": initial_binary_hash, "platform": platform.platform(), "python": sys.version,
               "passed": sum(item.get("passed", False) for item in suite.results), "failed": failed,
               "skipped": sum(item.get("skipped", False) for item in suite.results), "invariants": invariants,
               "worker_timeout_observed": any(item.get("reported_status") == "timed_out" for item in suite.results),
               "trial_slicing_performed": False, "cases": suite.results}
    summary["success"] = failed == 0 and all(invariants.values())
    save_json(output / "summary.json", summary)
    print(f"Summary: {summary['passed']} passed, {failed} failed, {summary['skipped']} skipped; {output / 'summary.json'}", flush=True)
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
