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
        check = result["model_checks"][0]
        assert check["operation_status"] == ("completed" if success else "failed"), check
        assert isinstance(check["check_complete"], bool)
        if check.get("basic_repair"):
            policy = "orca_stl_import_v1" if source.suffix.lower() == ".stl" else "orca_obj_import_v1"
            assert check["basic_repair"]["policy"] == policy, (label, check)
            assert check["basic_repair"]["status"] == "completed", (label, check)
        for artifact in result["output_files"]:
            assert Path(artifact).is_file(), (label, artifact)
            assert Path(artifact).stat().st_size > 0, (label, artifact)
        results.append({"case": label, "exit_code": process.returncode, "mesh_status": check["mesh_status"]})
        return check, result

    try:
        for extension in ("stl", "obj"):
            for stem, status in (("00-healthy-cube", "passed"), ("01-missing-top", "needs_repair"),
                                 ("04-duplicate-face", "needs_repair"), ("05-degenerate-face", "needs_repair"),
                                 ("08-intersecting-shells", "needs_repair"), ("09-nonmanifold-edge", "needs_repair")):
                check, _ = run(f"{stem}-{extension}", fixtures / f"{stem}.{extension}")
                assert check["schema_version"] == 2
                policy = "orca_stl_import_v1" if extension == "stl" else "orca_obj_import_v1"
                assert check["basic_repair"]["policy"] == policy
                # Native STL import may repair these defects, unlike OBJ.
                # C++ workflow tests compare against load_stl(repair=true);
                # raw serialized-artifact inspection is covered in C++ and
                # by the optional demo comparison below.
                if extension == "obj" or stem not in ("04-duplicate-face", "05-degenerate-face"):
                    assert check["mesh_status"] == status, check
                assert len(check["inspection"]["issues"]) == 7
                assert check["slicing"] == "not_requested"
                assert check["advanced_repair"]["status"] == "not_requested"
                if extension == "obj" and stem == "04-duplicate-face":
                    assert check["inspection"]["issues"]["duplicate"]["count"] == 1
                if extension == "obj" and stem == "05-degenerate-face":
                    assert check["inspection"]["issues"]["degenerate"]["count"] == 2
                if extension == "stl":
                    # Acceptance fixtures are deterministic ASCII STL files.
                    count = sum(line.strip().startswith("facet normal ") for line in (fixtures / f"{stem}.stl").read_text().splitlines())
                    assert check["basic_repair"]["facets_before"] == count
                    assert check["basic_repair"]["facets_after"] == check["inspection"]["triangle_count"]
                repeated, _ = run(f"{stem}-{extension}-reimport", Path(check["working_model"]))
                # A new task always imports again, even when given a working
                # model. Do not confuse it with same-task read-only validation.
                if extension == "obj":
                    assert repeated["inspection"] == check["inspection"]
                else:
                    assert repeated["basic_repair"]["facets_before"] == check["inspection"]["triangle_count"]
                if demo and extension == "stl":
                    command = [str(demo), check["working_model"]]
                    comparison = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, check=False)
                    (evidence / f"{stem}-demo.json").write_text(comparison.stdout, encoding="utf-8")
                    (evidence / f"{stem}-demo.log").write_text(comparison.stderr, encoding="utf-8")
                    assert comparison.returncode == 0, (stem, comparison.returncode, comparison.stderr)
                    expected = json.loads(comparison.stdout)
                    for issue in expected["issues"]:
                        actual = check["inspection"]["issues"][issue["key"]]
                        assert (actual["count"], actual["complete"]) == (issue["count"], issue["complete"]), issue["key"]
                    actual = check["inspection"]["component_analysis"]
                    expected = expected["component_analysis"]
                    assert actual["component_count"] == len(expected["components"])
                    for key in ("containment_complete", "intersections_complete", "size_complete", "limit_reached"):
                        assert actual[key] == expected[key], key
                    for actual_component, expected_component in zip(actual["components"], expected["components"]):
                        for key in ("closed", "small", "orientation", "expected_orientation", "orientation_suspect", "containers", "blocked_by"):
                            assert actual_component[key] == expected_component[key], (stem, key)

        for extension in ("stl", "obj"):
            check, result = run(f"repair-hole-{extension}", fixtures / f"01-missing-top.{extension}", "--repair-model", "cgal")
            assert check["advanced_repair"]["status"] == "repaired"
            assert check["mesh_status"] == "passed" and check["check_complete"]
            assert result["repair_validation"]["mesh"] == "passed"
            assert result["repair_validation"]["slicing"] == "not_requested"
            repeated, _ = run(f"recheck-{extension}", Path(check["working_model"]))
            assert not repeated["basic_repair"]["changed"]
            assert repeated["mesh_status"] == "passed"

        colored, _ = run("colored-hole", fixtures / "11-colored-missing-top.obj", "--repair-model", "cgal", "--export-obj")
        assert colored["mesh_status"] == "passed"
        assert any(path.endswith(".mtl") for path in colored["final_output_files"])
        # Detailed color area/coverage remains in the existing materials suite.
        missing = evidence / "missing-mtl.obj"
        missing.write_text("mtllib absent.mtl\nusemtl absent\n" + (fixtures / "00-healthy-cube.obj").read_text(), encoding="utf-8")
        check, _ = run("missing-mtl", missing)
        assert check["input_materials_ignored"] and check["mesh_status"] == "passed"
        check, _ = run("invalid-index", fixtures / "12-invalid-index.obj", success=False)
        assert not check["check_complete"] and check["mesh_status"] == "unknown"
        check, _ = run("unsafe-plane", fixtures / "10-only-triangle.obj", "--repair-model", "cgal", success=False)
        assert Path(check["prepared_model"]).is_file()
        assert check["working_model"] == check["prepared_model"]
        assert check["advanced_repair"]["status"] == "failed"
        check, result = run("unresolved-no-progress", fixtures / "09-nonmanifold-edge.obj", "--repair-model", "cgal", success=False)
        assert check["advanced_repair"]["status"] == "unresolved"
        assert check["advanced_repair"]["engine_status"] == "unresolved"
        assert not check["advanced_repair"]["committed"]
        assert result["model_repairs"][0]["can_continue"]
        assert check["working_model"] == check["prepared_model"]
        # Native OBJ import does not independently remove duplicates. Retainable
        # cleanup belongs to the advanced transaction, not the prepared model.
        duplicate_bad = evidence / "duplicate-nonmanifold.obj"
        bad_text = (fixtures / "09-nonmanifold-edge.obj").read_text()
        first_face = next(line for line in bad_text.splitlines() if line.startswith("f "))
        duplicate_bad.write_text(bad_text + "\n" + first_face + "\n", encoding="utf-8")
        check, result = run("keep-basic-progress", duplicate_bad, "--repair-model", "cgal")
        assert not check["basic_repair"]["changed"]
        assert check["before_advanced"]["issues"]["duplicate"]["count"] == 1
        assert result["model_repairs"][0]["repair_actions"]["basic_removed_faces"] == 1
        assert check["working_model"] != check["prepared_model"]
        assert check["inspection"]["issues"]["duplicate"]["count"] == 0
        assert check["inspection"]["issues"]["nonmanifold_edge"]["count"] > 0
        assert check["advanced_repair"]["status"] == "partially_repaired"
        assert result["repair_validation"]["mesh"] == "not_passed"
        run("reject-slice", fixtures / "00-healthy-cube.stl", "--slice", "0", success=False)
        run("reject-legacy-repair", fixtures / "00-healthy-cube.stl", "--auto-repair-model", "1", success=False)

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
                before = baseline["inspection"]["issues"]
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
                assert check["before_advanced"] == baseline["inspection"]
                after = check["inspection"]["issues"]
                assert all(issue["complete"] for issue in after.values()), (label, after)
                assert after["boundary"]["count"] == 0
                for issue in before:
                    if issue != "boundary":
                        assert after[issue]["count"] == before[issue]["count"], (label, issue)
                if has_hole:
                    assert check["working_model"] != check["prepared_model"]
                    assert check["advanced_repair"]["committed"]
                    assert check["advanced_repair"]["retained_stage"] == "advanced"
                    assert check["advanced_repair"]["status"] == ("partially_repaired" if unresolved else "repaired")
                else:
                    # ADMesh already closed the repairable component. The
                    # remaining unsupported component cannot fabricate an
                    # advanced commit (or justify a successful repair exit).
                    assert check["inspection"] == baseline["inspection"]
                    assert check["working_model"] == check["prepared_model"]
                    assert not check["advanced_repair"].get("committed", False)
                    assert check["advanced_repair"]["status"] == ("unresolved" if unresolved else "not_needed")
                    if unresolved:
                        assert result["return_code"] == -25
                        assert check["advanced_repair"]["engine_status"] == "unresolved"
                        assert check["advanced_repair"]["retained_stage"] == "prepared"
                        repair = result["model_repairs"][0]
                        assert repair["can_continue"] and not repair["committed"]
                        actions = repair["repair_actions"]
                        assert actions["unresolved"] > 0 and actions["failed"] == 0
                        for action in ("repaired", "mesh_repaired", "basic_removed_faces", "removed_parts", "rolled_back"):
                            assert actions[action] == 0, (label, action, actions)
                if variant == "colored-obj":
                    assert not check["input_materials_ignored"]
                    assert any(path.endswith(".mtl") for path in check["final_output_files"])
                    assert all(not volume["annotations_cleared"] for repair in result["model_repairs"] for volume in repair["volumes"])
                    before_areas, after_areas = material_areas(partial), material_areas(Path(check["working_model"]))
                    assert set(before_areas) == set(after_areas) == {(1., 0., 0.), (0., 1., 0.)}
                    # Preserve original color areas. The 100 mm² new cap uses
                    # the owning part's base color, not an inferred hole pattern.
                    assert all(after_areas[color] + 1.e-3 >= area for color, area in before_areas.items())
                    assert math.isclose(sum(after_areas.values()) - sum(before_areas.values()), 100., abs_tol=1.e-3)
                else:
                    assert check["inspection"]["triangle_count"] == check["before_advanced"]["triangle_count"] + (2 if has_hole else 0)
                assert result["repair_validation"]["mesh"] == ("not_passed" if unresolved else "passed")
                if has_hole or unresolved:
                    assert result["model_repairs"][0]["mesh_validated"] == (not unresolved)
                working = Path(check["working_model"])
                recheck, _ = run(label + "-recheck", working)
                if extension == "obj":
                    assert recheck["inspection"] == check["inspection"]
                    assert not recheck["basic_repair"]["changed"]
                # Each retry imports again. Only defects that survive that
                # import count as unresolved; regrouping is not repair progress.
                needs_repair = recheck["mesh_status"] != "passed"
                repeated, _ = run(label + "-retry", working, "--repair-model", "cgal", success=not needs_repair)
                assert repeated["before_advanced"] == recheck["inspection"]
                assert repeated["advanced_repair"]["status"] == ("unresolved" if needs_repair else "not_needed")
                assert repeated["working_model"] == repeated["prepared_model"]
                assert not repeated["advanced_repair"].get("committed", False)
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
