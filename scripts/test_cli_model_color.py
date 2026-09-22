#!/usr/bin/env python3
"""Check STL color grouping with an already rebuilt CLI; never builds or slices.

Pass resolved machine, process and filament JSON profiles. Fixtures, profile
copies, commands, logs and results are retained in an isolated temporary folder.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    for name in ("machine", "process", "filament"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="orca-cli-model-color-"))
    print(f"Evidence directory: {work}", flush=True)
    source_hashes = {str(binary): sha256(binary)}
    profiles = {}
    for name in ("machine", "process", "filament"):
        source = getattr(args, name).resolve(strict=True)
        source_hashes[str(source)] = sha256(source)
        profiles[name] = json.loads(source.read_text(encoding="utf-8"))
    # Small, deterministic plate with no extra adhesion/support footprint.
    profiles["machine"].update(printable_area=["0x0", "100x0", "100x100", "0x100"],
                               printable_height="100", bed_exclude_area=[])
    profiles["process"].update(enable_support="0", enforce_support_layers="0", raft_layers="0",
                               brim_type="no_brim", skirt_loops="0", enable_prime_tower="0", print_sequence="by layer")
    for name, profile in profiles.items():
        save_json(work / (name + ".json"), profile)

    vertices = [(0., 0., 0.), (8., 0., 0.), (0., 8., 0.), (0., 0., 8.)]
    faces = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]
    stl = b"model-color fixture".ljust(80, b"\0") + struct.pack("<I", len(faces))
    for face in faces:
        stl += struct.pack("<12fH", 0., 0., 0., *(v for index in face for v in vertices[index]), 0)
    for filename in ("stl-red.STL", "stl-blue.stl", "stl-uncolored.stl"):
        (work / filename).write_bytes(stl)
    geometry = "".join("v %g %g %g\n" % v for v in vertices)
    geometry += "".join("f %d %d %d\n" % tuple(i + 1 for i in face) for face in faces)
    (work / "palette.mtl").write_text("newmtl red\nKd 1 0 0\n", encoding="utf-8")
    (work / "obj-red.obj").write_text("mtllib palette.mtl\nusemtl red\n" + geometry, encoding="utf-8")
    (work / "obj-uncolored.obj").write_text(geometry, encoding="utf-8")
    for path in work.iterdir():
        if path.is_file():
            source_hashes[str(path)] = sha256(path)
    save_json(work / "identity.json", source_hashes)

    def run(name, flags):
        case = work / name
        case.mkdir()
        data = case / "data"
        data.mkdir()
        output = case / "output"
        output.mkdir()
        command = [str(binary), "--debug", "4", "--datadir", str(data), "--outputdir", str(output), *flags]
        save_json(case / "command.json", command)
        with (case / "run.log").open("wb") as log:
            result = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
        save_json(case / "exit.json", {"returncode": result.returncode})
        return result.returncode, output, (case / "run.log").read_text(encoding="utf-8", errors="replace")

    def arrange(name, auto_color, manual_group=None):
        flags = ["--load-settings", f"{work / 'machine.json'};{work / 'process.json'}",
                 "--load-filaments", str(work / "filament.json"),
                 "--model", str(work / "stl-red.STL"), "--model-color", "#ff0000",
                 "--model", str(work / "stl-blue.stl"), "--model-color=#0000ff"]
        if manual_group:
            flags += ["--model-arrange-group", manual_group]
        for filename in ("stl-uncolored.stl", "obj-red.obj", "obj-uncolored.obj"):
            flags += ["--model", str(work / filename)]
        flags += ["--clone-objects", "2,1,1,1,1", "--arrange", "1", "--auto-plate", "1",
                  "--allow-multicolor-oneplate", "--export-model-transforms", "--export-3mf", "arranged.3mf"]
        if auto_color:
            flags += ["--arrange-by-color"]
        # --model declares per-input overrides; actual inputs remain positional.
        flags += [str(work / filename) for filename in
                  ("stl-red.STL", "stl-blue.stl", "stl-uncolored.stl", "obj-red.obj", "obj-uncolored.obj")]
        code, output, log = run(name, flags)
        assert code == 0, (name, code)
        report = json.loads((output / "result.json").read_text(encoding="utf-8"))
        assert report["return_code"] == 0, (name, report)
        unarranged = re.findall(r"\bunarranged=(\d+)", log)
        assert unarranged and all(int(count) == 0 for count in unarranged), (name, unarranged)
        rows = report["model_transforms"]
        assert len(rows) == 6, (name, rows)
        plates = {}
        for prefix, count in (("stl-red", 2), ("stl-blue", 1), ("stl-uncolored", 1), ("obj-red", 1), ("obj-uncolored", 1)):
            matches = [row for row in rows if row["object_name"].startswith(prefix)]
            assert len(matches) == count, (name, prefix, rows)
            indices = {row["plate_index"] for row in matches}
            assert len(indices) == 1 and min(indices) >= 0, (name, prefix, indices)
            plates[prefix] = indices.pop()
            assert all(0 <= row["position"][0] <= 100 and 0 <= row["position"][1] <= 100 for row in matches), (name, matches)
        assert "built explicit multi-model filament catalog" not in log, name
        assert "OBJ multicolor auto mapping detected" in log, name
        projects = list((work / name).rglob("arranged.3mf"))
        assert len(projects) == 1, (name, projects)
        with zipfile.ZipFile(projects[0]) as archive:
            settings = json.loads(archive.read("Metadata/project_settings.config"))
        palette = {key: settings[key] for key in ("filament_colour", "filament_settings_id")}
        return plates, palette, log

    plates, palette, log = arrange("colors-on", True)
    assert plates["stl-red"] == plates["obj-red"]
    assert plates["stl-uncolored"] == plates["obj-uncolored"]
    assert len({plates["stl-red"], plates["stl-blue"], plates["stl-uncolored"]}) == 3
    assert "resolved=#FF0000, model_color=#FF0000" in log
    assert "resolved=#0000FF, model_color=#0000FF" in log
    print("PASS: colors-on", flush=True)
    plates, unchanged_palette, _ = arrange("colors-off", False)
    assert len(set(plates.values())) == 1
    assert unchanged_palette == palette
    print("PASS: colors-off", flush=True)
    plates, unchanged_palette, _ = arrange("manual-overrides-color", True, "#FF0000")
    assert plates["stl-red"] == plates["stl-blue"] == plates["obj-red"]
    assert plates["stl-uncolored"] == plates["obj-uncolored"] != plates["stl-red"]
    assert unchanged_palette == palette
    print("PASS: manual-overrides-color", flush=True)
    plates, unchanged_palette, _ = arrange("colors-off-with-manual-group", False, "manual")
    assert len({plates[key] for key in plates if key != "stl-blue"}) == 1
    assert plates["stl-blue"] != plates["stl-red"]
    assert unchanged_palette == palette
    print("PASS: colors-off-with-manual-group", flush=True)

    model = ["--model", str(work / "stl-red.STL")]
    negatives = [("bad-color-" + str(i), model + ["--model-color", value], "--model-color must be a color in #RRGGBB format.")
                 for i, value in enumerate(("red", "FF0000", "#F00", "#GG0000", "#FF000080", " #FF0000"))]
    negatives += [
        ("non-stl", ["--model", str(work / "obj-red.obj"), "--model-color", "#FF0000"], "requires an STL"),
        ("orphan-color", ["--model-color", "#FF0000", *model], "must follow a --model"),
        ("duplicate-color", model + ["--model-color", "#FF0000", "--model-color", "#0000FF"], "Duplicate --model-color"),
        ("missing-color", model + ["--model-color"], "model-color"),
    ]
    for name, flags, diagnostic in negatives:
        code, output, log = run(name, ["--help", *flags])
        assert code != 0 and diagnostic in log, (name, code, log)
        assert "read model file:" not in log and not list(output.rglob("*.3mf")), name
        print(f"PASS: {name}", flush=True)
    assert all(sha256(Path(path)) == expected for path, expected in source_hashes.items()), "An input or binary changed"
    print("All STL model-color CLI checks passed; no build or slicing was performed.")


if __name__ == "__main__":
    main()
