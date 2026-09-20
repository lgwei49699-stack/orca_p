#!/usr/bin/env python3
"""Validate the model-only CLI workflow using a freshly built binary. Never builds or slices."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import struct
import tempfile


ISSUE_KEYS = {"boundary", "nonmanifold_edge", "nonmanifold_vertex", "self_intersection", "degenerate", "duplicate", "orientation"}


def validate_issue_summary(issues):
    assert set(issues) == ISSUE_KEYS, issues
    for issue in issues.values():
        assert set(issue) == {"count", "complete"}, issue
        assert type(issue["count"]) is int and issue["count"] >= 0, issue
        assert isinstance(issue["complete"], bool), issue


def validate_business_result(result):
    """Exact public contract: detailed inspections and slicing records stay internal."""
    assert set(result) == {"return_code", "error_string", "model_checks"}, result.keys()
    assert type(result["return_code"]) is int and isinstance(result["error_string"], str)
    assert len(result["model_checks"]) == 1, result
    check = result["model_checks"][0]
    keys = {"schema_version", "mesh_status", "check_complete", "input_materials_ignored", "repair_status", "issues"}
    assert set(check) == keys, check.keys()
    assert check["schema_version"] == 4
    assert isinstance(check["check_complete"], bool)
    assert isinstance(check["input_materials_ignored"], bool)
    assert check["mesh_status"] in ("passed", "needs_repair", "unknown")
    assert check["repair_status"] in ("not_requested", "not_started", "not_needed", "repaired", "partially_repaired", "unresolved", "failed")
    if check["issues"] is None:
        assert not check["check_complete"]
        assert check["mesh_status"] == "unknown"
    else:
        validate_issue_summary(check["issues"])
        if check["mesh_status"] == "passed":
            assert check["check_complete"]
            assert all(issue == {"count": 0, "complete": True} for issue in check["issues"].values())
    return check


def fixture_artifacts(output, source, options, result):
    """Inspect only this test's fresh output directory, not a public path API.

    A failed export may leave a rejected result file: never choose it merely
    because it exists. The tests assert the existing prepared/result layout;
    this helper must not add artifact paths to the actual response JSON.
    """
    check = result["model_checks"][0]
    if check["issues"] is None:
        return []
    runs = list(output.glob("model-check-*"))
    assert len(runs) == 1 and runs[0].is_dir(), runs
    converted = result["return_code"] == 0 and any(flag in options for flag in ("--export-obj", "--export-stl"))
    advanced = check["repair_status"] in ("repaired", "partially_repaired")
    suffix = source.suffix.lower()
    if converted:
        suffix = ".obj" if "--export-obj" in options else ".stl"
    model = runs[0] / (("result" if advanced or converted else "prepared") + suffix)
    assert model.is_file() and model.stat().st_size > 0, model
    artifacts = [str(model)]
    if suffix == ".obj" and model.with_suffix(".mtl").is_file():
        material = model.with_suffix(".mtl")
        assert material.stat().st_size > 0, material
        artifacts.append(str(material))
    return artifacts


def triangle_count(path):
    """Count the actual exported file, not a removed implementation-only JSON statistic."""
    data = path.read_bytes()
    if path.suffix.lower() == ".obj":
        faces = [line.split() for line in data.decode("utf-8").splitlines() if line.startswith("f ")]
        assert all(len(face) == 4 for face in faces), path
        return len(faces)
    assert path.suffix.lower() == ".stl", path
    if len(data) >= 84:
        count = struct.unpack_from("<I", data, 80)[0]
        if len(data) == 84 + 50 * count:
            return count
    return sum(line.strip().startswith("facet normal ") for line in data.decode("ascii").splitlines())


def material_areas(path):
    """Read our triangular fixture/export subset, keyed by diffuse RGB, not material names."""
    colors, vertices, areas = {}, [], {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "mtllib":
            material = None
            for record in (path.parent / fields[1]).read_text(encoding="utf-8").splitlines():
                values = record.split()
                if values and values[0] == "newmtl":
                    material = values[1]
                elif values and values[0] == "Kd":
                    colors[material] = tuple(map(float, values[1:4]))
        elif fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "usemtl":
            current = fields[1]
        elif fields[0] == "f":
            assert len(fields) == 4 and current in colors, (path, line)
            indices = [int(value.split("/")[0]) for value in fields[1:]]
            assert all(0 < index <= len(vertices) for index in indices), (path, line)
            a, b, c = [vertices[index - 1] for index in indices]
            u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            rgb = colors[current]
            areas[rgb] = areas.get(rgb, 0.) + 0.5 * math.sqrt(sum(value * value for value in cross))
    assert areas, path
    return areas


def write_cap_fixture(path, colored=False):
    """One closed, self-intersecting tube with an isolated zero-area edge cap."""
    vertices = [(float(ring * 10), y, z) for ring in range(4) for y, z in ((0., 0.), (10., 0.), (10., 10.), (0., 10.))]
    vertices[6] = (5., 5., -2.)
    faces = [(0, 2, 1), (0, 3, 2)]
    for ring in range(3):
        for corner in range(4):
            a, b = ring * 4 + corner, ring * 4 + (corner + 1) % 4
            faces.extend(((a, b, b + 4), (b + 4, a + 4, a)))
    faces.extend(((12, 13, 14), (12, 14, 15)))
    a, b, c = faces[-1]
    middle = len(vertices)
    vertices.append(tuple((vertices[a][axis] + vertices[b][axis]) * 0.5 for axis in range(3)))
    faces[-1] = (a, middle, c)
    faces.extend(((middle, b, c), (a, b, middle)))
    if path.suffix == ".stl":
        data = bytearray(b"closed cap with residual intersections".ljust(80, b" ") + struct.pack("<I", len(faces)))
        for face in faces:
            coords = tuple(value for index in face for value in vertices[index])
            data.extend(struct.pack("<12fH", 0., 0., 0., *coords, 0))
        path.write_bytes(data)
    else:
        lines = []
        if colored:
            path.with_suffix(".mtl").write_text("newmtl red\nKd 1 0 0\nnewmtl green\nKd 0 1 0\n", encoding="utf-8")
            lines.append(f"mtllib {path.with_suffix('.mtl').name}")
        lines.extend("v " + " ".join(map(str, point)) for point in vertices)
        for index, face in enumerate(faces):
            if colored:
                lines.append("usemtl green" if index == 26 else "usemtl red")
            lines.append("f " + " ".join(str(vertex + 1) for vertex in face))
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--demo-inspector", type=Path, help="Optional existing native demo binary for same-STL parity checks")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    demo = args.demo_inspector.resolve(strict=True) if args.demo_inspector else None
    repo = Path(__file__).resolve().parents[1]
    fixtures = repo / "tests/data/model_repair_acceptance"
    evidence = Path(tempfile.mkdtemp(prefix="orca-model-check-"))
    print(f"Evidence: {evidence}", flush=True)
    original_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()}
    results = []
    baselines = {}

    def run(label, source, *options, success=True):
        output = evidence / label
        output.mkdir()
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        command = [str(binary), "--check-model", "--outputdir", str(output), *options, str(source)]
        (output / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        with (output / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
        result = json.loads((output / "result.json").read_text(encoding="utf-8"))
        assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash, (label, "Input changed")
        assert (process.returncode == 0) == success, (label, process.returncode, result)
        assert (result["return_code"] == 0) == success, (label, result)
        check = validate_business_result(result)
        assert bool(result["error_string"]) == (not success), (label, result)
        artifacts = fixture_artifacts(output, source, options, result)
        # Test-only adapter: neither path field belongs to the response JSON.
        check = {**check, "working_model": artifacts[0] if artifacts else None, "_artifacts": artifacts}
        results.append({"case": label, "exit_code": process.returncode, "mesh_status": check["mesh_status"]})
        return check, result

    try:
        for extension in ("stl", "obj"):
            for stem, status in (("00-healthy-cube", "passed"), ("01-missing-top", "needs_repair"),
                                 ("04-duplicate-face", "needs_repair"), ("05-degenerate-face", "needs_repair"),
                                 ("08-intersecting-shells", "needs_repair"), ("09-nonmanifold-edge", "needs_repair")):
                check, _ = run(f"{stem}-{extension}", fixtures / f"{stem}.{extension}")
                baselines[(stem, extension)] = check
                # Native STL import may repair these defects, unlike OBJ.
                # C++ workflow tests compare against load_stl(repair=true);
                # raw serialized-artifact inspection is covered in C++ and
                # by the optional demo comparison below.
                if extension == "obj" or stem not in ("04-duplicate-face", "05-degenerate-face"):
                    assert check["mesh_status"] == status, check
                assert len(check["issues"]) == 7
                assert check["repair_status"] == "not_requested"
                assert "issues_before_repair" not in check
                if extension == "obj" and stem == "04-duplicate-face":
                    assert check["issues"]["duplicate"]["count"] == 1
                if extension == "obj" and stem == "05-degenerate-face":
                    assert check["issues"]["degenerate"]["count"] == 2
                assert triangle_count(Path(check["working_model"])) > 0
                repeated, _ = run(f"{stem}-{extension}-reimport", Path(check["working_model"]))
                # A new task always imports again, even when given a working
                # model. Do not confuse it with same-task read-only validation.
                if extension == "obj":
                    assert repeated["issues"] == check["issues"]
                    assert repeated["check_complete"] == check["check_complete"]
                    assert triangle_count(Path(repeated["working_model"])) == triangle_count(Path(check["working_model"]))
                if demo and extension == "stl":
                    command = [str(demo), check["working_model"]]
                    comparison = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, check=False)
                    (evidence / f"{stem}-demo.json").write_text(comparison.stdout, encoding="utf-8")
                    (evidence / f"{stem}-demo.log").write_text(comparison.stderr, encoding="utf-8")
                    assert comparison.returncode == 0, (stem, comparison.returncode, comparison.stderr)
                    expected = json.loads(comparison.stdout)
                    for issue in expected["issues"]:
                        actual = check["issues"][issue["key"]]
                        assert (actual["count"], actual["complete"]) == (issue["count"], issue["complete"]), issue["key"]
                    components = expected["component_analysis"]
                    expected_complete = (all(issue["complete"] for issue in expected["issues"])
                                         and components["size_complete"] and components["containment_complete"])
                    assert check["check_complete"] == expected_complete

        for extension in ("stl", "obj"):
            check, result = run(f"repair-hole-{extension}", fixtures / f"01-missing-top.{extension}", "--repair-model", "cgal")
            assert check["repair_status"] == "repaired"
            assert check["mesh_status"] == "passed" and check["check_complete"]
            assert baselines[("01-missing-top", extension)]["issues"]["boundary"]["count"] == 4
            assert check["issues"]["boundary"]["count"] == 0
            repeated, _ = run(f"recheck-{extension}", Path(check["working_model"]))
            assert repeated["mesh_status"] == "passed"
            assert triangle_count(Path(repeated["working_model"])) == triangle_count(Path(check["working_model"])) == 12

        inward = evidence / "inward-cube.obj"
        inward.write_text("\n".join("f " + " ".join(reversed(line.split()[1:])) if line.startswith("f ") else line
                                     for line in (fixtures / "00-healthy-cube.obj").read_text().splitlines()) + "\n", encoding="utf-8")
        inward_check, _ = run("whole-shell-direction-not-a-check", inward, "--repair-model", "cgal")
        assert inward_check["mesh_status"] == "passed" and inward_check["check_complete"]
        assert inward_check["repair_status"] == "not_needed"

        for variant in ("obj", "stl", "colored-obj"):
            extension = "obj" if variant == "colored-obj" else variant
            cap = evidence / f"degenerate-cap-{variant}.{extension}"
            write_cap_fixture(cap, colored=variant == "colored-obj")
            baseline, _ = run(f"cap-baseline-{variant}", cap)
            if extension == "obj":
                assert baseline["issues"]["degenerate"]["count"] == 1
                assert baseline["issues"]["boundary"]["count"] == 3
            # STL assertions use the actual ADMesh-prepared baseline; a native
            # import improvement must not be attributed to the advanced stage.
            fixed, result = run(f"cap-repair-{variant}", cap, "--repair-model", "cgal")
            assert fixed["issues"]["degenerate"] == {"count": 0, "complete": True}
            assert fixed["issues"]["boundary"] == {"count": 0, "complete": True}
            assert fixed["issues"]["nonmanifold_edge"]["count"] == 0
            assert fixed["issues"]["nonmanifold_vertex"]["count"] == 0
            assert fixed["repair_status"] in ("repaired", "partially_repaired", "not_needed")
            if fixed["issues"]["self_intersection"]["count"]:
                assert fixed["repair_status"] == "partially_repaired" and fixed["mesh_status"] == "needs_repair"
            repeated, _ = run(f"cap-recheck-{variant}", Path(fixed["working_model"]))
            assert repeated["issues"]["degenerate"]["count"] == 0
            assert repeated["issues"]["boundary"]["count"] == 0
            if variant == "colored-obj":
                assert any(path.endswith(".mtl") for path in fixed["_artifacts"])
                area = material_areas(Path(fixed["working_model"]))
                assert math.isclose(area.get((0., 1., 0.), 0.), 50., rel_tol=1e-5, abs_tol=1e-5), area

        colored, colored_result = run("colored-hole", fixtures / "11-colored-missing-top.obj", "--repair-model", "cgal", "--export-obj")
        assert colored["mesh_status"] == "passed"
        assert any(path.endswith(".mtl") for path in colored["_artifacts"])
        # Detailed color area/coverage remains in the existing materials suite.
        missing = evidence / "missing-mtl.obj"
        missing.write_text("mtllib absent.mtl\nusemtl absent\n" + (fixtures / "00-healthy-cube.obj").read_text(), encoding="utf-8")
        check, _ = run("missing-mtl", missing)
        assert check["input_materials_ignored"] and check["mesh_status"] == "passed"
        check, _ = run("invalid-index", fixtures / "12-invalid-index.obj", success=False)
        assert not check["check_complete"] and check["mesh_status"] == "unknown"
        assert check["issues"] is None and check["working_model"] is None
        plane_baseline, _ = run("unsafe-plane-baseline", fixtures / "10-only-triangle.obj")
        check, _ = run("unsafe-plane", fixtures / "10-only-triangle.obj", "--repair-model", "cgal", success=False)
        assert Path(check["working_model"]).name == "prepared.obj"
        assert check["repair_status"] == "failed"
        assert check["issues"] == plane_baseline["issues"]
        check, result = run("unresolved-no-progress", fixtures / "09-nonmanifold-edge.obj", "--repair-model", "cgal", success=False)
        assert check["repair_status"] == "unresolved" and result["return_code"] == -25
        assert check["issues"] == baselines[("09-nonmanifold-edge", "obj")]["issues"]
        assert Path(check["working_model"]).name == "prepared.obj"
        # Native OBJ import does not independently remove duplicates. Retainable
        # cleanup belongs to the advanced transaction, not the prepared model.
        duplicate_bad = evidence / "duplicate-nonmanifold.obj"
        bad_text = (fixtures / "09-nonmanifold-edge.obj").read_text()
        first_face = next(line for line in bad_text.splitlines() if line.startswith("f "))
        duplicate_bad.write_text(bad_text + "\n" + first_face + "\n", encoding="utf-8")
        duplicate_baseline, _ = run("keep-basic-progress-before", duplicate_bad)
        assert duplicate_baseline["issues"]["duplicate"]["count"] == 1
        check, result = run("keep-basic-progress", duplicate_bad, "--repair-model", "cgal")
        assert triangle_count(Path(check["working_model"])) == triangle_count(duplicate_bad) - 1
        assert Path(check["working_model"]).name == "result.obj"
        assert check["issues"]["duplicate"]["count"] == 0
        assert check["issues"]["nonmanifold_edge"]["count"] > 0
        assert check["repair_status"] == "partially_repaired"
        assert check["mesh_status"] == "needs_repair"
        run("reject-slice", fixtures / "00-healthy-cube.stl", "--slice", "0", success=False)
        run("reject-legacy-repair", fixtures / "00-healthy-cube.stl", "--auto-repair-model", "1", success=False)
        run("reject-disable-legacy-repair", fixtures / "00-healthy-cube.stl", "--auto-repair-model", "0", success=False)
        rejected, rejected_result = run("reject-repair-format-conflict", fixtures / "00-healthy-cube.obj",
                                        "--repair-model", "cgal", "--export-obj", "--export-stl", success=False)
        assert rejected["repair_status"] == "not_started"
        assert rejected["issues"] is None and not rejected["_artifacts"]
        healthy, _ = run("healthy-repair-not-needed", fixtures / "00-healthy-cube.obj", "--repair-model", "cgal")
        assert healthy["repair_status"] == "not_needed" and "issues_before_repair" not in healthy
        converted, _ = run("check-format-conversion", fixtures / "00-healthy-cube.obj", "--export-stl")
        assert converted["repair_status"] == "not_requested" and "issues_before_repair" not in converted
        assert Path(converted["working_model"]).name == "result.stl"
        assert converted["mesh_status"] == "passed" and triangle_count(Path(converted["working_model"])) == 12

        # One repairable part and one unsupported part, spatially separated.
        for bad_first in (False, True):
            vertices, faces = [], []
            inputs = [("01-missing-top.obj", 0), ("09-nonmanifold-edge.obj", 50)]
            for name, shift in reversed(inputs) if bad_first else inputs:
                offset = len(vertices)
                for line in (fixtures / name).read_text().splitlines():
                    fields = line.split()
                    if fields and fields[0] == "v":
                        xyz = list(map(float, fields[1:4]))
                        xyz[0] += shift
                        vertices.append(xyz)
                    elif fields and fields[0] == "f":
                        faces.append([int(index.split("/")[0]) + offset for index in fields[1:]])
            for variant in ("obj", "stl", "colored-obj"):
                extension = "obj" if variant == "colored-obj" else variant
                label = f"partial-{'bad-first' if bad_first else 'good-first'}-{variant}"
                partial = evidence / (label + "." + extension)
                material_hash = None
                if extension == "obj":
                    prefix = ""
                    face_records = []
                    if variant == "colored-obj":
                        material = partial.with_suffix(".mtl")
                        material.write_text("newmtl red\nKd 1 0 0\nnewmtl green\nKd 0 1 0\n", encoding="utf-8")
                        material_hash = hashlib.sha256(material.read_bytes()).hexdigest()
                        prefix = f"mtllib {material.name}\n"
                    for face in faces:
                        if variant == "colored-obj":
                            face_records.append("usemtl " + ("red" if vertices[face[0] - 1][0] >= 50 else "green") + "\n")
                        face_records.append("f " + " ".join(map(str, face)) + "\n")
                    partial.write_text(prefix + "".join("v " + " ".join(map(str, v)) + "\n" for v in vertices) +
                                       "".join(face_records), encoding="utf-8")
                else:
                    partial.write_bytes(b"Model check partial repair fixture".ljust(80, b"\0") + struct.pack("<I", len(faces)) +
                                        b"".join(struct.pack("<12fH", 0., 0., 0.,
                                                            *(v for index in f for v in vertices[index - 1]), 0) for f in faces))
                original_hash = hashlib.sha256(partial.read_bytes()).hexdigest()
                # Native STL repair may alter the non-manifold fixture before
                # CGAL. Assert against the actual imported baseline, never
                # bypass import to force STL to retain the OBJ topology.
                baseline, _ = run(label + "-import", partial)
                before = baseline["issues"]
                assert all(issue["complete"] for issue in before.values()), (label, before)
                assert before["boundary"]["count"] in (0, 4), (label, before)
                for issue in ("degenerate", "duplicate", "orientation", "self_intersection"):
                    assert before[issue]["count"] == 0, (label, issue, before[issue])
                has_hole = before["boundary"]["count"] == 4
                unresolved = any(before[issue]["count"] > 0 for issue in ("nonmanifold_edge", "nonmanifold_vertex"))
                if extension == "obj":
                    # OBJ still exercises retained progress, in both input
                    # orders and with/without color. Do not weaken this case.
                    assert has_hole
                    assert before["nonmanifold_edge"]["count"] == 1
                    assert before["nonmanifold_vertex"]["count"] == 2
                check, result = run(label, partial, "--repair-model", "cgal", success=has_hole or not unresolved)
                after = check["issues"]
                assert all(issue["complete"] for issue in after.values()), (label, after)
                assert after["boundary"]["count"] == 0
                for issue in before:
                    if issue != "boundary":
                        assert after[issue]["count"] == before[issue]["count"], (label, issue)
                if has_hole:
                    assert Path(check["working_model"]).name == f"result.{extension}"
                    assert check["repair_status"] == ("partially_repaired" if unresolved else "repaired")
                else:
                    # ADMesh already closed the repairable component. The
                    # remaining unsupported component cannot fabricate an
                    # advanced commit (or justify a successful repair exit).
                    assert check["issues"] == before
                    assert check["check_complete"] == baseline["check_complete"]
                    assert Path(check["working_model"]).name == f"prepared.{extension}"
                    assert triangle_count(Path(check["working_model"])) == triangle_count(Path(baseline["working_model"]))
                    assert check["repair_status"] == ("unresolved" if unresolved else "not_needed")
                    if unresolved:
                        assert result["return_code"] == -25
                        # Engine action counters stay covered by C++ and the
                        # unchanged legacy repair suite, not the business API.
                if variant == "colored-obj":
                    assert not check["input_materials_ignored"]
                    assert any(path.endswith(".mtl") for path in check["_artifacts"])
                    before_areas, after_areas = material_areas(partial), material_areas(Path(check["working_model"]))
                    assert set(before_areas) == set(after_areas) == {(1., 0., 0.), (0., 1., 0.)}
                    # Preserve original color areas. The 100 mm² new cap uses
                    # the owning part's base color, not an inferred hole pattern.
                    assert all(after_areas[color] + 1.e-3 >= area for color, area in before_areas.items())
                    assert math.isclose(sum(after_areas.values()) - sum(before_areas.values()), 100., abs_tol=1.e-3)
                else:
                    assert triangle_count(Path(check["working_model"])) == triangle_count(Path(baseline["working_model"])) + (2 if has_hole else 0)
                assert check["mesh_status"] == ("needs_repair" if unresolved else "passed")
                working = Path(check["working_model"])
                recheck, _ = run(label + "-recheck", working)
                if extension == "obj":
                    assert recheck["issues"] == check["issues"]
                    assert recheck["check_complete"] == check["check_complete"]
                    assert triangle_count(Path(recheck["working_model"])) == triangle_count(working)
                # Each retry imports again. Only defects that survive that
                # import count as unresolved; regrouping is not repair progress.
                needs_repair = recheck["mesh_status"] != "passed"
                repeated, _ = run(label + "-retry", working, "--repair-model", "cgal", success=not needs_repair)
                assert repeated["issues"] == recheck["issues"]
                assert repeated["repair_status"] == ("unresolved" if needs_repair else "not_needed")
                assert Path(repeated["working_model"]).name == f"prepared.{extension}"
                assert triangle_count(Path(repeated["working_model"])) == triangle_count(Path(recheck["working_model"]))
                assert hashlib.sha256(partial.read_bytes()).hexdigest() == original_hash
                if material_hash is not None:
                    assert hashlib.sha256(material.read_bytes()).hexdigest() == material_hash

        for source, expected in original_hashes.items():
            assert hashlib.sha256(source.read_bytes()).hexdigest() == expected, f"Input changed: {source}"
    except Exception as error:
        (evidence / "summary.json").write_text(json.dumps({"passed": False, "error": str(error), "cases": results}, indent=2), encoding="utf-8")
        raise
    (evidence / "summary.json").write_text(json.dumps({"passed": True, "binary": str(binary), "binary_mtime": binary.stat().st_mtime,
        "demo_parity_executed": demo is not None, "slicing_verified": False, "cases": results}, indent=2), encoding="utf-8")
    print(f"Passed {len(results)} workflow cases; no slicing/printing verification. Evidence: {evidence}")


if __name__ == "__main__":
    main()
