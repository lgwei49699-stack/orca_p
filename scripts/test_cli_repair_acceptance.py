#!/usr/bin/env python3
"""Repair fixture acceptance against a freshly built CLI. Never builds or sends G-code to a printer."""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile


def stl_volume(path):
    data = path.read_bytes()
    if len(data) >= 84 and len(data) == 84 + 50 * struct.unpack_from("<I", data, 80)[0]:
        triangles = [struct.unpack_from("<9f", data, offset + 12) for offset in range(84, len(data), 50)]
    else:
        vertices = [list(map(float, line.split()[1:4])) for line in data.decode("ascii").splitlines()
                    if line.strip().startswith("vertex ")]
        assert len(vertices) % 3 == 0, path
        triangles = [sum(vertices[i:i + 3], []) for i in range(0, len(vertices), 3)]
    assert triangles and all(math.isfinite(n) for face in triangles for n in face), path
    return abs(sum(a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
                   for a, b, c, d, e, f, g, h, i in triangles) / 6)


def check_obj_topology(text):
    """Check the triangular OBJ exporter contract without welding or repairing.

    This tests indexed edge connectivity, not geometric self-intersections or
    nonmanifold vertices. The CGAL roundtrip remains a separate full inspection.
    """
    vertices, faces = [], []
    for line in text.splitlines():
        fields = line.partition("#")[0].split()
        if not fields:
            continue
        if fields[0] == "v":
            assert len(fields) == 4, "Expected XYZ-only output vertices"
            vertex = tuple(map(float, fields[1:]))
            assert all(math.isfinite(value) for value in vertex), "Non-finite OBJ vertex"
            vertices.append(vertex)
        elif fields[0] == "f":
            assert len(fields) == 4, "Expected triangular OBJ output"
            face = tuple(int(value.split("/")[0]) - 1 for value in fields[1:])
            assert len(set(face)) == 3, "Repeated OBJ face index"
            assert all(0 <= index < len(vertices) for index in face), "Invalid OBJ face index"
            faces.append(face)
    assert faces, "No OBJ faces"
    directed = Counter((face[i], face[(i + 1) % 3]) for face in faces for i in range(3))
    edges = {tuple(sorted(edge)) for edge in directed}
    bad_edges = sum(directed[a, b] != 1 or directed[b, a] != 1 for a, b in edges)
    assert bad_edges == 0, f"OBJ has {bad_edges} open/nonmanifold/inconsistently oriented indexed edges"
    return len(vertices), len(faces)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--timeout", type=int, default=180, help="Timeout per CLI process in seconds")
    parser.add_argument("--load-settings", help="Optional absolute printer/process JSON paths separated by semicolons")
    parser.add_argument("--load-filaments", help="Optional absolute filament JSON paths separated by semicolons; two for colored OBJ")
    args = parser.parse_args()
    if bool(args.load_settings) != bool(args.load_filaments):
        parser.error("For slicing, supply both --load-settings and --load-filaments. Omit both for repair-only checks.")
    for value in (args.load_settings, args.load_filaments):
        if value:
            for item in value.split(";"):
                path = Path(item)
                if not path.is_absolute() or not path.is_file():
                    parser.error(f"Slicing preset must be an existing absolute file path: {item}")
    binary = args.binary.resolve(strict=True)
    fixtures = Path(__file__).resolve().parents[1] / "tests/data/model_repair_acceptance"
    manifest = json.loads((fixtures / "manifest.json").read_text(encoding="utf-8"))
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()}
    work = Path(tempfile.mkdtemp(prefix="orca-repair-acceptance-"))
    print(f"Evidence directory: {work}", flush=True)
    results = []

    def run(name, source, flags, expected_success, *, slice_job=False, expected_partial=False, extra_sources=()):
        case = work / name
        case.mkdir()
        data, output = case / "data", case / "output"
        data.mkdir()
        command = [str(binary), "--datadir", str(data), "--outputdir", str(output),
                   "--repair-model", "cgal", *flags, str(source), *(str(p) for p in extra_sources)]
        (case / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        try:
            with (case / "run.log").open("wb") as log:
                process = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            report = json.loads((output / "result.json").read_text(encoding="utf-8"))
            # A crash / missing report is never an acceptable "expected failure".
            assert process.returncode >= 0, f"Process crashed: {process.returncode}"
            assert (process.returncode == 0) == expected_success, report
            assert (report["return_code"] == 0) == expected_success, report
            paths = [Path(p) if Path(p).is_absolute() else case / p for p in report.get("output_files", [])]
            if expected_success:
                repairs = report["model_repairs"]
                validation = report["repair_validation"]
                assert repairs and all(r["can_continue"] for r in repairs), report
                if expected_partial:
                    assert any(r["committed"] for r in repairs), report
                    assert any(r["repair_actions"]["unresolved"] > 0 for r in repairs), report
                    assert any(not r["mesh_validated"] for r in repairs), report
                    assert validation["mesh"] == "not_passed" and validation["unresolved_volumes"] > 0, report
                    assert all(r["repair_actions"]["failed"] == 0 for r in repairs), report
                else:
                    assert all(r["success"] and r["mesh_validated"] and r["after"]["closed"] for r in repairs), report
                    assert validation["mesh"] == "passed", report
                if slice_job:
                    assert validation["slicing"] == "completed" and validation["completed_plates"] == 1, report
                    plates = report.get("sliced_plates", [])
                    assert len(plates) == 1 and plates[0]["total_filament_g"] > 0 and plates[0]["total_time"] > 0, report
                    assert not plates[0].get("warning_message"), "Sliced with warnings; requires manual review: " + str(plates)
                    gcodes = list(output.rglob("*.gcode"))
                    assert gcodes and all(p.stat().st_size > 0 for p in gcodes), "No non-empty G-code"
                else:
                    assert validation["slicing"] == "not_requested", report
                    assert paths and all(p.is_file() and p.stat().st_size > 0 for p in paths), report
            else:
                assert report.get("error_string"), report
                if source.stem == "10-only-triangle":
                    repairs = report["model_repairs"]
                    assert repairs and all(r["load_succeeded"] and r["repair_attempted"] and not r["success"]
                                           and not r["committed"] for r in repairs), \
                        "Single-triangle fixture must fail in mesh repair, not just import/export"
                    assert all(not r["can_continue"] for r in repairs), report
                if source.stem in ("08-intersecting-shells", "09-nonmanifold-edge"):
                    repairs = report["model_repairs"]
                    assert repairs and all(r["status"] == "unresolved" and r["can_continue"]
                                           and not r["committed"] and not r["mesh_validated"] for r in repairs), report
                assert not paths, report
                assert not any(p.suffix.lower() in (".stl", ".obj", ".mtl", ".gcode") for p in output.rglob("*")), report
            results.append(dict(case=name, passed=True))
            print("PASS: " + name, flush=True)
            return report, paths
        except (AssertionError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            results.append(dict(case=name, passed=False, error=str(error)))
            print(f"FAIL: {name}: {error}", flush=True)
            return None, []

    for fixture in manifest["cases"]:
        for extension in fixture.get("formats", ["stl", "obj"]):
            name = fixture["name"] + "-" + extension
            source = fixtures / (fixture["name"] + "." + extension)
            success = fixture["expected"] == "success"
            report, outputs = run(name, source, ["--export-" + extension], success)
            if report is not None and success:
                try:
                    if extension == "stl":
                        actual_volume = sum(stl_volume(p) for p in outputs)
                        expected_volume = fixture["expected_volume_mm3"]
                        assert expected_volume is not None and abs(actual_volume - expected_volume) <= 0.1, \
                            f"Volume mismatch: expected {expected_volume}, got {actual_volume}"
                    if extension == "obj":
                        for output in outputs:
                            if output.suffix.lower() == ".obj":
                                check_obj_topology(output.read_text(encoding="utf-8"))
                    if extension == "obj" and fixture["colored"]:
                        assert {p.suffix.lower() for p in outputs} == {".obj", ".mtl"}, outputs
                        assert all(not v["annotations_cleared"] for r in report["model_repairs"] for v in r["volumes"]), report
                        mtl = next(p for p in outputs if p.suffix.lower() == ".mtl").read_text(encoding="utf-8")
                        colors = {tuple(map(float, line.split()[1:])) for line in mtl.splitlines() if line.startswith("Kd ")}
                        assert {(1., 0., 0.), (0., 1., 0.)} <= colors, colors
                except (AssertionError, OSError, ValueError, StopIteration) as error:
                    results.append(dict(case=name + "-geometry-or-color", passed=False, error=str(error)))
                    print(f"FAIL: {name}-geometry-or-color: {error}", flush=True)
                # Re-import exported geometry with legacy repair disabled by CGAL mode.
                # It must pass full inspection without a second mesh repair.
                exported = next((p for p in outputs if p.suffix.lower() == "." + extension), None)
                if exported is None:
                    results.append(dict(case=name + "-roundtrip", passed=False, error="Missing model output"))
                    print(f"FAIL: {name}-roundtrip: Missing model output", flush=True)
                    continue
                again, _ = run(name + "-roundtrip", exported, ["--export-" + extension], True)
                if again is not None and any(r["repair_actions"]["mesh_repaired"] != 0 for r in again["model_repairs"]):
                    results.append(dict(case=name + "-roundtrip-validation", passed=False, error="Export still required mesh repair"))
                    print(f"FAIL: {name}-roundtrip-validation: Export still required mesh repair", flush=True)
            # The sub-layer-height solid is a geometry-retention test, not a
            # promise that these settings can produce a physical thin feature.
            if args.load_settings and success and fixture["name"] != "07-thin-solid":
                run(name + "-slice", source, ["--load-settings", args.load_settings, "--load-filaments", args.load_filaments,
                                             "--arrange", "1", "--slice", "0"], True, slice_job=True)

    # A synthetic mixed defect in ONE mesh: remove one duplicate safely while the
    # existing non-manifold/intersecting edge remains. Failed CGAL candidates
    # must never be exported, and the result must not claim full validation.
    mixed = work / "mixed-duplicate-and-nonmanifold.obj"
    mixed_text = (fixtures / "09-nonmanifold-edge.obj").read_text(encoding="utf-8")
    first_face = next(line for line in mixed_text.splitlines() if line.startswith("f "))
    mixed.write_text(mixed_text.rstrip() + "\n" + first_face + "\n", encoding="utf-8")
    hashes[mixed] = hashlib.sha256(mixed.read_bytes()).hexdigest()
    partial, outputs = run("partial-same-mesh", mixed, ["--export-obj"], True, expected_partial=True)
    if partial is not None:
        try:
            repair = partial["model_repairs"][0]
            assert repair["status"] == "partially_repaired", repair
            assert repair["repair_actions"]["basic_removed_faces"] == 1, repair
            assert repair["before"]["facets"] == 9 and repair["after"]["facets"] == 8, repair
            exported = next(p for p in outputs if p.suffix.lower() == ".obj")
            # Partial output is deliberately NOT a fully repaired roundtrip.
            again, _ = run("partial-roundtrip-unresolved", exported, ["--export-obj"], False)
            if again is not None:
                assert all(r["status"] == "unresolved" and r["can_continue"] and not r["committed"]
                           and r["repair_actions"]["basic_removed_faces"] == 0 for r in again["model_repairs"]), again
        except (AssertionError, OSError, ValueError, KeyError, StopIteration) as error:
            results.append(dict(case="partial-same-mesh-validation", passed=False, error=str(error)))
            print(f"FAIL: partial-same-mesh-validation: {error}", flush=True)

    for bad_first in (True, False):
        batch = [fixtures / "09-nonmanifold-edge.stl", fixtures / "01-missing-top.stl"]
        if not bad_first:
            batch.reverse()
        run("partial-batch-" + ("unresolved-first" if bad_first else "repaired-first"), batch[0],
            ["--export-stl"], True, expected_partial=True, extra_sources=batch[1:])

    healthy = fixtures / "00-healthy-cube.stl"
    for name, flags in (("reject-no-check", ["--no-check"]), ("reject-allow-newer", ["--allow-newer-file"]),
                        ("reject-slice-cache", ["--load-slicedata", str(work / "unused-cache")])):
        run(name, healthy, ["--slice", "0", *flags], False)
    for path, expected in hashes.items():
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            results.append(dict(case="source-protection", passed=False, error=str(path)))
    summary = dict(slicing_requested=bool(args.load_settings),
                   note="No physical printing or universal printability guarantee. Thin-solid fixture is not sliced. "
                        "Partial repair cases must report mesh=not_passed and are export-only checks.", results=results)
    (work / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    failed = sum(not r["passed"] for r in results)
    print(f"{len(results) - failed} passed; {failed} failed. Evidence: {work}")
    if not args.load_settings:
        print("SLICING NOT VERIFIED. Supply actual server printer/process and filament settings for the slicing pass.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
