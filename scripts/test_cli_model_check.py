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


def validate_issue_summary(issues, *, basic=False):
    assert set(issues) == ISSUE_KEYS, issues
    for key, issue in issues.items():
        if basic and key in ("nonmanifold_vertex", "self_intersection"):
            assert issue == {"count": None, "complete": False, "status": "not_requested"}, issue
            continue
        expected = {"count", "complete"}
        if basic and key == "boundary":
            expected.update(("method", "geometric_count"))
            assert issue["method"] == "orca_open_edges", issue
            assert type(issue["geometric_count"]) is int and issue["geometric_count"] >= 0
        if basic and key == "duplicate":
            expected.update(("same_winding_extra_faces", "opposite_winding_groups"))
            for field in ("same_winding_extra_faces", "opposite_winding_groups"):
                assert type(issue[field]) is int and issue[field] >= 0
        assert set(issue) == expected, issue
        assert type(issue["count"]) is int and issue["count"] >= 0, issue
        assert isinstance(issue["complete"], bool), issue


def validate_business_result(result, *, evidence=False, basic=False):
    """Exact public contract: detailed inspections and slicing records stay internal."""
    assert set(result) == {"return_code", "error_string", "model_checks"}, result.keys()
    assert type(result["return_code"]) is int and isinstance(result["error_string"], str)
    assert len(result["model_checks"]) == 1, result
    check = result["model_checks"][0]
    keys = {"schema_version", "mesh_status", "check_complete", "input_materials_ignored", "repair_status", "issues"}
    if evidence:
        keys.add("boundary")
    if basic:
        keys.add("inspection_scope")
        assert check["inspection_scope"] == "basic", check
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
        validate_issue_summary(check["issues"], basic=basic)
        if check["mesh_status"] == "passed":
            assert check["check_complete"]
            assert all(issue["count"] == 0 and issue["complete"] for key, issue in check["issues"].items()
                       if not (basic and key in ("nonmanifold_vertex", "self_intersection")))
    if evidence:
        validate_boundary(check)
    return check


def validate_boundary(check):
    boundary = check["boundary"]
    assert set(boundary) == {"schema_version", "inspection", "repair", "proper_crossings", "sections", "reconstruction"}, boundary
    assert boundary["schema_version"] == 2, boundary
    inspection, repair, sections, reconstruction = (boundary[key] for key in ("inspection", "repair", "sections", "reconstruction"))
    assert inspection["status"] in ("not_run", "passed", "defects_detected", "incomplete"), inspection
    assert isinstance(inspection["known_defects"], list) and isinstance(inspection["incomplete_checks"], list), inspection
    assert isinstance(inspection["structural_blockers"], list), inspection
    expected = {} if check["issues"] is None else {
        key: {"count": value["count"], "complete": value["complete"] and not value.get("count_is_lower_bound", False)}
        for key, value in check["issues"].items() if value["count"] is not None and value["count"] > 0}
    known = {item["check"]: {"count": item["count"], "complete": item["count_complete"]}
             for item in inspection["known_defects"]}
    assert known == expected, (known, expected)
    assert inspection["printability"] == "not_assessed", inspection
    assert repair["status"] in ("not_run", "completed", "incomplete", "canceled", "failed"), repair
    assert sections["status"] in ("not_run", "completed", "incomplete", "canceled", "failed"), sections
    assert reconstruction["assessment"] in ("not_needed", "undetermined", "evidence_present", "blocked"), reconstruction
    assert reconstruction["automatic_adoption"] is False, reconstruction
    assert reconstruction["applicability"] in ("not_evaluated", "eligible", "unsupported"), reconstruction
    assert reconstruction["necessity"] == "not_established" and reconstruction["printability"] == "not_assessed", reconstruction
    assert set(reconstruction["policy"]) == {"version", "allow_boundary_closure"}, reconstruction
    assert reconstruction["policy"]["version"] == 1, reconstruction
    assert isinstance(reconstruction["policy"]["allow_boundary_closure"], bool), reconstruction
    crossings = boundary["proper_crossings"]
    assert crossings["status"] in ("not_run", "completed", "incomplete"), crossings
    if crossings["status"] != "not_run":
        assert isinstance(crossings["detected"], bool) and isinstance(crossings["search_complete"], bool), crossings
        assert type(crossings["pairs_checked"]) is int and type(crossings["total_pairs"]) is int, crossings
        assert 0 <= crossings["pairs_checked"] <= crossings["total_pairs"], crossings
        assert crossings["elapsed_ms"] >= 0, crossings
        assert crossings["total_pairs"] == check["issues"]["self_intersection"]["count"], (crossings, check)
        assert crossings["search_complete"] == check["issues"]["self_intersection"]["complete"], (crossings, check)
        if crossings["detected"]:
            assert crossings["pairs_checked"] > 0 and crossings["status"] == "completed", crossings
            witness = crossings["witness"]
            assert len(witness["faces"]) == 2 and witness["faces"][0] != witness["faces"][1], witness
            assert all(type(face) is int and face >= 0 for face in witness["faces"]), witness
            assert len(witness["segment"]) == 2 and witness["segment"][0] != witness["segment"][1], witness
            assert all(len(point) == 3 and all(math.isfinite(value) for value in point) for point in witness["segment"]), witness
        else:
            assert crossings["pairs_checked"] == crossings["total_pairs"] and crossings["witness"] is None, crossings
    if check["repair_status"] in ("not_requested", "not_started", "not_needed"):
        assert repair["status"] == "not_run" and sections["status"] == "not_run", boundary
    if check["repair_status"] in ("partially_repaired", "unresolved"):
        assert repair["execution_completed"] is True and repair["failed"] == 0, repair
    if sections["status"] == "completed":
        assert sections["planes"] == sections["requested_planes"] and sections["planes"] > 0, sections
        assert len(sections["axes"]) == 1 and sections["axes"][0]["axis"] == "Z", sections
        assert sections["printer_layer_plan"] is False, sections
        assert type(sections["remaining_open_chains"]) is int and sections["remaining_open_chains"] >= 0, sections
    if reconstruction["assessment"] == "evidence_present":
        assert inspection["status"] == "defects_detected" and repair["execution_completed"] is True, boundary
        assert reconstruction["applicability"] == "eligible", boundary
        reason = reconstruction["reason"]
        assert reason in ("proper_crossing_after_repair", "open_boundaries_under_closed_solid_policy", "no_effective_regions"), boundary
        if reason == "no_effective_regions":
            assert sections["status"] == "completed", boundary
            assert sections["all_effective_regions_empty"] is True and sections["effective_regions"] == 0, boundary
            assert sections["nondegenerate_closed_loop_planes"] > 0 and sections["parameters_can_remove_regions"] is False, boundary
        else:
            assert sections["status"] == "not_run" and sections["reason"] == "reconstruction_evidence_already_present", boundary
            if reason == "proper_crossing_after_repair":
                assert crossings["status"] == "completed" and crossings["search_complete"] and crossings["detected"], boundary
            else:
                assert reconstruction["policy"]["allow_boundary_closure"] is True, boundary
                assert check["issues"]["boundary"]["complete"] and check["issues"]["boundary"]["count"] > 0, boundary


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

    def run(label, source, *options, success=True, include_evidence=False):
        output = evidence / label
        output.mkdir()
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        command = [str(binary), "--check-model", "--outputdir", str(output), *options,
                   *(('--repair-evidence',) if include_evidence else ()), str(source)]
        (output / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        with (output / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
        result = json.loads((output / "result.json").read_text(encoding="utf-8"))
        assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash, (label, "Input changed")
        assert (process.returncode == 0) == success, (label, process.returncode, result)
        assert (result["return_code"] == 0) == success, (label, result)
        check = validate_business_result(result, evidence=include_evidence)
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
            if variant == "obj":
                observed, _ = run("cap-proper-crossing-evidence", cap, "--repair-model", "cgal", include_evidence=True)
                assert observed["repair_status"] == "partially_repaired", observed
                assert observed["boundary"]["proper_crossings"]["detected"] is True, observed
                assert observed["boundary"]["reconstruction"]["assessment"] == "evidence_present", observed
                assert observed["boundary"]["reconstruction"]["reason"] == "proper_crossing_after_repair", observed
                assert observed["boundary"]["sections"]["status"] == "not_run", observed
                assert observed["boundary"]["reconstruction"]["policy"]["allow_boundary_closure"] is False, observed
                assert observed["issues"] == fixed["issues"], (observed, fixed)
                assert Path(observed["working_model"]).read_bytes() == Path(fixed["working_model"]).read_bytes()
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
        check, _ = run("unsafe-plane", fixtures / "10-only-triangle.obj", "--repair-model", "cgal", success=False, include_evidence=True)
        assert Path(check["working_model"]).name == "prepared.obj"
        assert check["repair_status"] == "failed"
        assert check["issues"] == plane_baseline["issues"]
        assert check["boundary"]["sections"]["status"] == "not_run"
        assert check["boundary"]["reconstruction"]["assessment"] == "blocked"
        assert check["boundary"]["reconstruction"]["failure_kind"] == "repair_failed"
        check, result = run("unresolved-no-progress", fixtures / "09-nonmanifold-edge.obj", "--repair-model", "cgal",
                            include_evidence=True)
        assert check["repair_status"] == "unresolved" and result["return_code"] == 0
        assert check["issues"] == baselines[("09-nonmanifold-edge", "obj")]["issues"]
        assert Path(check["working_model"]).name == "prepared.obj"
        assert check["boundary"]["repair"]["status"] == "completed"
        assert "failure_kind" not in check["boundary"]["reconstruction"]
        assert check["boundary"]["sections"]["status"] == "completed"
        assert check["boundary"]["reconstruction"]["assessment"] == "undetermined"
        assert check["boundary"]["reconstruction"]["reason"] == "no_conclusive_region_failure"
        # The opt-in combined workflow completed, not the geometric repair.
        # Default and explicit-off calls preserve their legacy failure code,
        # exact six-field v4 model summary and the same retained geometry.
        for label, flags in (("unresolved-default-no-evidence", ()), ("unresolved-explicit-off", ("--repair-evidence", "0"))):
            plain, plain_result = run(label, fixtures / "09-nonmanifold-edge.obj", "--repair-model", "cgal",
                                      *flags, success=False)
            expected = {key: value for key, value in result["model_checks"][0].items() if key != "boundary"}
            assert plain_result["model_checks"][0] == expected, (plain_result, expected)
            assert plain_result["return_code"] == -25
            assert Path(plain["working_model"]).read_bytes() == Path(check["working_model"]).read_bytes()
        # Normal no-progress must continue through an explicitly requested
        # conversion and inspect the serialized result, rather than return 0
        # early with only prepared.obj. STL conversion itself is not a repair.
        converted, converted_result = run("unresolved-evidence-export-stl", fixtures / "09-nonmanifold-edge.obj",
                                           "--repair-model", "cgal", "--export-stl", include_evidence=True)
        assert converted_result["return_code"] == 0 and converted["repair_status"] == "unresolved"
        assert Path(converted["working_model"]).name == "result.stl"
        assert triangle_count(Path(converted["working_model"])) == triangle_count(fixtures / "09-nonmanifold-edge.obj")
        assert converted["issues"] == check["issues"]
        assert converted["mesh_status"] == check["mesh_status"] == "needs_repair"
        assert converted["check_complete"] == check["check_complete"]
        assert converted["boundary"]["sections"]["status"] == "completed"
        assert converted["boundary"]["sections"]["planes"] == check["boundary"]["sections"]["planes"]
        assert converted["boundary"]["proper_crossings"]["detected"] is False, converted
        policy_no_opening, _ = run("closed-contact-policy-enabled", fixtures / "09-nonmanifold-edge.obj",
                                  "--repair-model", "cgal", "--repair-evidence-close-boundaries", "1", include_evidence=True)
        assert policy_no_opening["boundary"]["reconstruction"]["policy"]["allow_boundary_closure"] is True, policy_no_opening
        assert policy_no_opening["issues"]["boundary"]["count"] == 0, policy_no_opening
        assert policy_no_opening["boundary"]["reconstruction"]["assessment"] == "undetermined", policy_no_opening
        assert policy_no_opening["boundary"]["reconstruction"]["reason"] == "no_conclusive_region_failure", policy_no_opening
        plain, plain_result = run("unresolved-export-stl-explicit-off", fixtures / "09-nonmanifold-edge.obj",
                                  "--repair-model", "cgal", "--export-stl", "--repair-evidence", "0", success=False)
        assert plain_result["return_code"] == -25 and plain["repair_status"] == "unresolved"
        assert Path(plain["working_model"]).name == "prepared.obj"
        assert not list((evidence / "unresolved-export-stl-explicit-off").rglob("result.stl"))
        # Native OBJ import does not independently remove duplicates. Retainable
        # cleanup belongs to the advanced transaction, not the prepared model.
        duplicate_bad = evidence / "duplicate-nonmanifold.obj"
        bad_text = (fixtures / "09-nonmanifold-edge.obj").read_text()
        first_face = next(line for line in bad_text.splitlines() if line.startswith("f "))
        # Two tetrahedra share a non-manifold edge; remove one outer face so
        # advanced repair retains a genuine open boundary, not a repairable
        # healthy cube. Enabling this policy changes only the recommendation.
        open_bad = evidence / "open-nonmanifold.obj"
        open_lines = bad_text.splitlines()
        open_lines.pop(max(index for index, line in enumerate(open_lines) if line.startswith("f ")))
        open_bad.write_text("\n".join(open_lines) + "\n", encoding="utf-8")
        open_default, _ = run("residual-open-default-policy", open_bad, "--repair-model", "cgal", include_evidence=True)
        assert open_default["repair_status"] in ("partially_repaired", "unresolved"), open_default
        assert open_default["issues"]["boundary"]["count"] > 0, open_default
        assert open_default["boundary"]["proper_crossings"]["detected"] is False, open_default
        assert open_default["boundary"]["reconstruction"]["assessment"] == "undetermined", open_default
        assert open_default["boundary"]["reconstruction"]["policy"]["allow_boundary_closure"] is False, open_default
        for value in ("0", "1"):
            open_policy, _ = run(f"residual-open-policy-{value}", open_bad, "--repair-model", "cgal",
                                 "--repair-evidence-close-boundaries", value, include_evidence=True)
            assert open_policy["boundary"]["reconstruction"]["policy"]["allow_boundary_closure"] == (value == "1"), open_policy
            assert open_policy["issues"] == open_default["issues"], (open_policy, open_default)
            assert Path(open_policy["working_model"]).read_bytes() == Path(open_default["working_model"]).read_bytes()
            if value == "1":
                assert open_policy["boundary"]["reconstruction"]["assessment"] == "evidence_present", open_policy
                assert open_policy["boundary"]["reconstruction"]["reason"] == "open_boundaries_under_closed_solid_policy", open_policy
                assert open_policy["boundary"]["sections"]["status"] == "not_run", open_policy
            else:
                assert open_policy["boundary"]["reconstruction"]["assessment"] == "undetermined", open_policy
                assert open_policy["boundary"]["sections"]["status"] == "completed", open_policy
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
        observed, observed_result = run("keep-basic-progress-evidence", duplicate_bad, "--repair-model", "cgal", include_evidence=True)
        assert {key: value for key, value in observed_result["model_checks"][0].items() if key != "boundary"} == result["model_checks"][0]
        assert Path(observed["working_model"]).read_bytes() == Path(check["working_model"]).read_bytes()
        assert observed["boundary"]["sections"]["status"] == "completed"
        # Stretch Z only: the same eight-face defect remains deterministic, but
        # its 600 mm height exceeds the 2,000 reference-plane budget. This is a
        # real CLI resource boundary without adding a public test-only option.
        tall_bad_text = "\n".join(
            "v " + " ".join((*line.split()[1:3], str(float(line.split()[3]) * 30.)))
            if line.startswith("v ") else line
            for line in bad_text.splitlines()) + "\n"
        for partial in (False, True):
            label = "partial" if partial else "unresolved"
            tall = evidence / f"{label}-plane-limit.obj"
            tall.write_text(tall_bad_text + (first_face + "\n" if partial else ""), encoding="utf-8")
            limited, limited_result = run(f"{label}-evidence-plane-limit", tall, "--repair-model", "cgal",
                                           success=False, include_evidence=True)
            assert limited_result["return_code"] == -25
            assert limited["repair_status"] == ("partially_repaired" if partial else "unresolved")
            assert limited["mesh_status"] == "needs_repair"
            assert limited["boundary"]["repair"]["status"] == "completed"
            assert limited["boundary"]["sections"]["status"] == "incomplete"
            assert limited["boundary"]["sections"]["reason"] == "plane_limit"
            assert limited["boundary"]["sections"]["planes"] == 0
            assert limited["boundary"]["reconstruction"]["assessment"] == "blocked"
            assert limited["boundary"]["reconstruction"]["failure_kind"] == "repair_evidence_incomplete", limited
            assert limited["boundary"]["reconstruction"]["failure_stage"] == "sections", limited
            assert limited["boundary"]["reconstruction"]["failure_cause"] == "plane_limit", limited
            assert limited["boundary"]["reconstruction"]["failure_category"] == "resource", limited
            plain, plain_result = run(f"{label}-plane-limit-explicit-off", tall, "--repair-model", "cgal",
                                      "--repair-evidence", "0", success=partial)
            assert plain_result["return_code"] == (0 if partial else -25)
            assert {key: value for key, value in limited_result["model_checks"][0].items() if key != "boundary"} == plain_result["model_checks"][0]
            assert Path(limited["working_model"]).read_bytes() == Path(plain["working_model"]).read_bytes()
        for label, source, flags in (
                ("healthy-evidence", fixtures / "00-healthy-cube.obj", ()),
                ("healthy-repair-evidence", fixtures / "00-healthy-cube.obj", ("--repair-model", "cgal")),
                ("defects-detection-only-evidence", fixtures / "09-nonmanifold-edge.obj", ()),
                ("repairable-evidence", fixtures / "01-missing-top.obj", ("--repair-model", "cgal"))):
            observed, _ = run(label, source, *flags, include_evidence=True)
            assert observed["boundary"]["sections"]["status"] == "not_run", observed
        invalid, _ = run("invalid-index-evidence", fixtures / "12-invalid-index.obj", success=False, include_evidence=True)
        assert invalid["boundary"]["reconstruction"]["assessment"] == "blocked", invalid
        invalid, _ = run("invalid-combination-evidence", fixtures / "00-healthy-cube.obj", "--slice", "0",
                         success=False, include_evidence=True)
        assert invalid["boundary"]["reconstruction"]["failure_kind"] == "invalid_arguments", invalid
        for value in ("0", "1"):
            for label, flags, include in (
                    ("no-advanced", (), True),
                    ("no-evidence", ("--repair-model", "cgal"), False),
                    ("evidence-off", ("--repair-model", "cgal", "--repair-evidence", "0"), False)):
                rejected, rejected_result = run(f"reject-closure-policy-{label}-{value}", fixtures / "00-healthy-cube.obj",
                                                *flags, "--repair-evidence-close-boundaries", value,
                                                success=False, include_evidence=include)
                assert "--repair-evidence-close-boundaries requires" in rejected_result["error_string"], rejected_result
                assert rejected["issues"] is None and not rejected["_artifacts"], rejected
        accepted, _ = run("healthy-closure-policy-enabled", fixtures / "00-healthy-cube.obj", "--repair-model", "cgal",
                          "--repair-evidence-close-boundaries", "1", include_evidence=True)
        assert accepted["boundary"]["reconstruction"]["policy"]["allow_boundary_closure"] is True, accepted
        assert accepted["boundary"]["reconstruction"]["assessment"] == "not_needed", accepted
        assert accepted["boundary"]["sections"]["status"] == "not_run", accepted

        for label, flags in (
                ("ordinary-repair", ("--repair-model", "cgal", "--repair-evidence", "--export-stl")),
                ("ordinary-export", ("--export-stl",)),
                ("reconstruct", ("--reconstruct-model", "--accept-reconstruction-risk")),
                ("reconstruct-help", ("--reconstruct-model", "--help"))):
            for value in ("0", "1"):
                output = evidence / f"reject-closure-policy-{label}-{value}"
                command = [str(binary), *flags, "--repair-evidence-close-boundaries=" + value,
                           "--outputdir", str(output), str(fixtures / "00-healthy-cube.stl")]
                process = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, check=False)
                assert process.returncode != 0, (command, process.stdout, process.stderr)
                assert "--repair-evidence-close-boundaries requires" in process.stderr, (command, process.stderr)
                if label.startswith("reconstruct"):
                    assert not output.exists(), output
                else:
                    rejected_result = json.loads((output / "result.json").read_text(encoding="utf-8"))
                    assert rejected_result["return_code"] != 0, rejected_result
                    assert "--repair-evidence-close-boundaries requires" in rejected_result["error_string"], rejected_result
                    assert set(path.name for path in output.iterdir()) == {"result.json"}, output
                results.append({"case": f"reject-closure-policy-{label}-{value}", "exit_code": process.returncode})
        for prefix in ("--", "-"):
            output = evidence / f"reject-closure-policy-invalid-{len(prefix)}"
            command = [str(binary), "--check-model", "--repair-model", "cgal", "--repair-evidence",
                       prefix + "repair-evidence-close-boundaries=2", "--outputdir", str(output),
                       str(fixtures / "00-healthy-cube.obj")]
            process = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, check=False)
            assert process.returncode != 0 and "expects 0 or 1" in process.stderr, (command, process.stderr)
            assert not output.exists(), output
            results.append({"case": f"reject-closure-policy-invalid-{len(prefix)}", "exit_code": process.returncode})
        output = evidence / "reject-evidence-without-model-action"
        output.mkdir()
        command = [str(binary), "--repair-evidence", "--export-stl", "--outputdir", str(output),
                   str(fixtures / "00-healthy-cube.obj")]
        (output / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        with (output / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
        rejected = json.loads((output / "result.json").read_text(encoding="utf-8"))
        assert process.returncode > 0 and rejected["return_code"] != 0, rejected
        assert "--repair-evidence requires" in rejected["error_string"], rejected
        assert not list(output.rglob("*.stl")), output
        results.append({"case": "reject-evidence-without-model-action", "exit_code": process.returncode})
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
