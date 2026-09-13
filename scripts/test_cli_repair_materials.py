#!/usr/bin/env python3
"""Run against a freshly built CLI; never builds. Keep isolated artifacts for review."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Path to the newly built OrcaSlicer executable")
    parser.add_argument("--timeout", type=int, default=120, help="Per-process timeout in seconds")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="orca-cli-repair-materials-"))
    print(f"Evidence directory: {work}", flush=True)
    source = work / "input.obj"
    mtl = work / "palette.mtl"
    # Closed tetrahedron: geometry does not require a repair, but imports UV/material metadata.
    source.write_text(
        "mtllib palette.mtl\n"
        "v 0 0 0\nv 10 0 0\nv 0 10 0\nv 0 0 10\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nusemtl color\n"
        "f 1/1 3/3 2/2\nf 1/1 2/2 4/3\nf 1/1 4/2 3/3\nf 2/1 3/2 4/3\n",
        encoding="utf-8",
    )
    mtl.write_text("X" * 70000, encoding="utf-8")
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, mtl)}

    def run(name, flags, inputs, success, materials_ignored=None, expected_error=None, obj_mode=None, changed=False,
            expected_log_error=None):
        case = work / name
        case.mkdir()
        output = case / "output"
        data = case / "data"
        data.mkdir()
        # Warning-level output is part of the missing-MTL notification checks;
        # the normal CLI default only emits errors. This changes logging, not repair policy.
        command = [str(binary), "--debug", "3", "--datadir", str(data), "--outputdir", str(output), *flags, *map(str, inputs)]
        (case / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
        with (case / "run.log").open("wb") as log:
            result = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
        report = json.loads((output / "result.json").read_text(encoding="utf-8"))
        assert (result.returncode == 0) == success, (name, result.returncode, report)
        assert (report["return_code"] == 0) == success, (name, report)
        if expected_error is not None:
            assert expected_error in report["error_string"], (name, report)
        if expected_log_error is not None:
            # Import failures use the shared parse-error code/message in JSON;
            # the loader's specific diagnostic is emitted separately in the log.
            assert not success and report["return_code"] == -6, (name, report)
            detail = (case / "run.log").read_text(encoding="utf-8", errors="replace")
            assert expected_log_error in detail, (name, expected_log_error, detail)
        if obj_mode is not None:
            exports = report.get("obj_exports", [])
            assert exports and all(e["mode"] == obj_mode for e in exports), (name, report)
        if changed:
            assert any(r["committed"] for r in report.get("model_repairs", [])), (name, report)
            assert all(not v["annotations_cleared"] for r in report["model_repairs"] for v in r["volumes"]), (name, report)
        if materials_ignored is not None:
            repairs = report.get("model_repairs", [])
            assert repairs and all(r["success"] for r in repairs), (name, report)
            assert all(r["input_materials_ignored"] == materials_ignored for r in repairs), (name, report)
        outputs = []
        for value in report.get("output_files", []):
            path = Path(value)
            outputs.append(path if path.is_absolute() else case / path)
        if success:
            assert outputs and all(p.is_file() and p.stat().st_size for p in outputs), (name, report)
        else:
            assert not outputs, (name, report)
            assert not list(output.rglob("*.stl")) and not list(output.rglob("*.obj")), name
        for path, original in hashes.items():
            assert hashlib.sha256(path.read_bytes()).hexdigest() == original, f"Source changed: {path}"
        print(f"PASS: {name}", flush=True)
        return outputs

    run("default-mtl-parser-failure", ["--repair-model", "cgal", "--export-stl"], [source], False)
    flags = ["--repair-model", "cgal", "--repair-ignore-materials"]
    stls = run("ignore-mtl-export-stl", flags + ["--export-stl"], [source], True, True)
    objs = run("ignore-mtl-export-obj", flags + ["--export-obj"], [source], True, True)
    assert all(p.suffix.lower() == ".obj" for p in objs)
    for obj in objs:
        text = obj.read_text(encoding="utf-8")
        assert not any(line.startswith(("mtllib ", "usemtl ", "vt ")) for line in text.splitlines())
        assert not obj.with_suffix(".mtl").exists()
    run("stl-repair-roundtrip", ["--repair-model", "cgal", "--export-stl"], stls, True, False)
    run("requires-repair", ["--repair-ignore-materials", "--export-stl"], [source], False,
        expected_error="requires --repair-model cgal")
    run("no-implicit-gui", ["--repair-ignore-materials"], [], False,
        expected_error="requires --repair-model cgal")
    for name, extra in (
        ("reject-multicolor", ["--export-multicolor-obj", "colored.obj"]),
        ("reject-color-split", ["--split-by-color", "--export-stl"]),
        ("reject-color-arrange", ["--arrange-by-color", "--export-stl"]),
    ):
        run(name, flags + extra, [source], False, expected_error="--repair-ignore-materials cannot be combined")
    run("reject-material-binding", flags + ["--export-stl", "--model", str(source), "--model-filaments", "1"], [], False,
        expected_error="--repair-ignore-materials cannot be combined")

    # The normal command now selects colored vs. geometry-only output automatically.
    colored = work / "colored.obj"
    palette = work / "colors.mtl"
    palette.write_text("newmtl red\nKa 0 0 0\nKd 1 0 0\nnewmtl green\nKa 0 0 0\nKd 0 1 0\n", encoding="utf-8")
    tetra = "v 0 0 0\nv 10 0 0\nv 0 10 0\nv 0 0 10\n"
    colored.write_text("mtllib colors.mtl\n" + tetra +
                       "usemtl red\nf 1 3 2\nf 1 2 4\nusemtl green\nf 1 4 3\nf 2 3 4\n", encoding="utf-8")
    open_colored = work / "open-colored.obj"
    open_colored.write_text("mtllib colors.mtl\n" + tetra +
                            "usemtl red\nf 1 3 2\nf 1 2 4\nusemtl green\nf 1 4 3\n", encoding="utf-8")
    plain = work / "plain.obj"
    plain.write_text(tetra + "f 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n", encoding="utf-8")
    for path in (colored, palette, open_colored, plain):
        hashes[path] = hashlib.sha256(path.read_bytes()).hexdigest()
    automatic = ["--repair-model", "cgal", "--export-obj"]
    for name, input_path, changed in (("auto-colored-closed", colored, False), ("auto-colored-repair", open_colored, True)):
        paths = run(name, automatic, [input_path], True, False, obj_mode="filament_mmu", changed=changed)
        assert {p.suffix.lower() for p in paths} == {".obj", ".mtl"}
        obj = next(p for p in paths if p.suffix.lower() == ".obj").read_text(encoding="utf-8")
        assert len({line for line in obj.splitlines() if line.startswith("usemtl ")}) >= 2
        mtl_text = next(p for p in paths if p.suffix.lower() == ".mtl").read_text(encoding="utf-8")
        colors = {tuple(map(float, line.split()[1:])) for line in mtl_text.splitlines() if line.startswith("Kd ")}
        assert {(1.0, 0.0, 0.0), (0.0, 1.0, 0.0)} <= colors
    run("auto-plain", automatic, [plain], True, False, obj_mode="geometry_only")

    # Missing sidecars are an automatic geometry fallback, not an import failure.
    # Include dangling UV indices: they must not block geometry-only import.
    for name, libraries in (
        ("missing-mtl", "mtllib absent.mtl\n"),
        ("no-mtllib", ""),
        ("partially-missing-mtl", "mtllib colors.mtl\nmtllib absent.mtl\n"),
        ("partially-missing-mtl-reversed", "mtllib absent.mtl\nmtllib colors.mtl\n"),
    ):
        missing = work / (name + ".obj")
        missing.write_text(libraries + tetra + "usemtl red\nf 1/99 3/99 2/99\nf 1/99 2/99 4/99\n"
                                             "usemtl green\nf 1/99 4/99 3/99\n", encoding="utf-8")
        hashes[missing] = hashlib.sha256(missing.read_bytes()).hexdigest()
        paths = run("auto-" + name, automatic, [missing], True, True, obj_mode="geometry_only", changed=True)
        assert all(p.suffix.lower() == ".obj" for p in paths)
        for path in paths:
            text = path.read_text(encoding="utf-8")
            assert not any(line.startswith(("mtllib ", "usemtl ", "vt ")) for line in text.splitlines())
            assert not path.with_suffix(".mtl").exists()
        log = (work / ("auto-" + name) / "run.log").read_text(encoding="utf-8", errors="replace")
        assert "source colors and textures were ignored" in log
        # Re-import no longer refers to an unavailable MTL, and needs no repair.
        run("roundtrip-" + name, automatic, paths, True, False, obj_mode="geometry_only")
        roundtrip = json.loads((work / ("roundtrip-" + name) / "output" / "result.json").read_text(encoding="utf-8"))
        assert all(r["repair_actions"]["mesh_repaired"] == 0 for r in roundtrip["model_repairs"])
        run("stl-" + name, ["--repair-model", "cgal", "--export-stl"], [missing], True, True, changed=True)

    bad_geometry = work / "missing-mtl-invalid-geometry.obj"
    bad_geometry.write_text("mtllib absent.mtl\n" + tetra + "usemtl red\nf 1 2 99\n", encoding="utf-8")
    hashes[bad_geometry] = hashlib.sha256(bad_geometry.read_bytes()).hexdigest()
    run("reject-missing-mtl-invalid-geometry", automatic, [bad_geometry], False,
        expected_log_error="invalid vertex index")
    unresolved_material = work / "unresolved-material.obj"
    unresolved_material.write_text("mtllib colors.mtl\n" + tetra +
                                   "usemtl undefined\nf 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n", encoding="utf-8")
    hashes[unresolved_material] = hashlib.sha256(unresolved_material.read_bytes()).hexdigest()
    run("reject-existing-mtl-unresolved-material", automatic, [unresolved_material], False,
        expected_log_error="a face material is missing or unresolved")

    run("reject-ambiguous-multiple-palettes", automatic, [colored, plain], False,
        expected_error="Automatic colored OBJ repair requires one input")
    run("explicit-multicolor-compatible", ["--repair-model", "cgal", "--export-multicolor-obj", "result.obj"],
        [colored], True, False, obj_mode="filament_mmu")
    run("non-repair-export-unchanged", ["--export-obj"], [colored], True, obj_mode="geometry_only")
    print("All CLI material-policy smoke checks passed. Evidence retained; no build was performed.")


if __name__ == "__main__":
    main()
