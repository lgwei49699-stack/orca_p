#!/usr/bin/env python3
"""Exercise automatic multi-OBJ colors through slicing and a colored OBJ export."""

import argparse
from collections import Counter
import hashlib
import itertools
import json
import re
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def check_legacy_bindings(binary, work, filament):
    """Different ordinary profiles must keep the pre-merge object/face bindings."""
    paths = []
    temperatures, flows = [190, 205, 225, 235], [0.9, 1.0, 1.1, 1.2]
    for index, (temperature, flow) in enumerate(zip(temperatures, flows)):
        path = work / f"distinct-profile-{index}.json"
        save_json(path, dict(filament, nozzle_temperature=[str(temperature)],
                             nozzle_temperature_initial_layer=[str(temperature)], filament_flow_ratio=[str(flow)]))
        paths.append(str(path))

    def run(label, names, options, control):
        out = work / (label + ("-control" if control else ""))
        out.mkdir()
        (out / "data").mkdir()
        cmd = [str(binary.resolve()), "--debug", "4", "--slice", "0", "--arrange", "0", "--auto-plate", "0",
               "--ensure-on-bed", "--allow-multicolor-oneplate", "--datadir", str(out / "data"),
               "--load-settings", str(work / "machine.json"), "--load-settings", str(work / "process.json"),
               "--load-filaments", ";".join(paths), "--outputdir", str(out), "--export-3mf", "result.3mf", *options]
        if control:
            # Existing zero IDs leave imported object/face slots untouched and
            # exclude automatic merging; this isolates the legacy numbering path.
            cmd += ["--load-filament-ids", ",".join("0" for _ in names)]
        for index, name in enumerate(names):
            cmd += ["--model", name + ".obj", "--model-position", f"{40 + index * 60},70,0"]
        cmd += [str(work / (name + ".obj")) for name in names]
        save_json(out / "command.json", cmd)
        with (out / "run.log").open("w") as log:
            status = subprocess.run(cmd, cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=120).returncode
        save_json(out / "exit.json", {"returncode": status})
        assert status == 0, (label, control, status)
        with zipfile.ZipFile(out / "result.3mf") as archive:
            assert archive.testzip() is None
            settings = json.loads(archive.read("Metadata/project_settings.config"))
            for key, expected in (("nozzle_temperature", temperatures), ("nozzle_temperature_initial_layer", temperatures),
                                  ("filament_flow_ratio", flows)):
                assert list(map(float, settings[key])) == expected, (label, key, settings[key])
            metadata = ET.fromstring(archive.read("Metadata/model_settings.config"))
            slots = [entry.attrib["value"] for obj in metadata.findall("object")
                     for entry in obj.iter("metadata") if entry.attrib.get("key") == "extruder"]
            # Compare painted face assignments as well as object/volume defaults.
            triangles = {name: [node.attrib for node in ET.fromstring(archive.read(name)).iter()
                                 if node.tag.endswith("}triangle")]
                         for name in archive.namelist() if name.endswith(".model")}
            gcode = [name for name in sorted(archive.namelist()) if name.endswith(".gcode")]
            assert gcode
            instructions = [line.split(";", 1)[0].strip() for name in gcode
                            for line in archive.read(name).decode().splitlines() if line.split(";", 1)[0].strip()]
            tools = {int(match.group(1)) for line in instructions if (match := re.fullmatch(r"T(\d+)", line))}
        save_json(out / "instructions.json", instructions)
        return {"slots": slots, "triangles": triangles, "instructions": instructions, "tools": sorted(tools)}

    results = {}
    for label, names, options, tools in (
        ("legacy-distinct", ["plain-red", "plain-blue"], [], [1]),
        ("legacy-distinct-reversed", ["plain-blue", "plain-red"], [], [1]),
        ("legacy-distinct-default-flag", ["plain-red", "plain-blue"], ["--load-defaultfila"], [1]),
        ("legacy-distinct-painted-faces", ["multi", "plain-blue"], [], [1, 2, 3]),
    ):
        control = run(label, names, options, True)
        actual = run(label, names, options, False)
        assert actual["tools"] == tools, (label, tools, actual["tools"])
        for key in ("slots", "triangles", "instructions"):
            assert actual[key] == control[key], (label, key, "changed from the preserved legacy bindings")
        log = (work / label / "run.log").read_text(errors="replace")
        assert "Merged automatic OBJ palette" not in log, label
        results[label] = {"tools": actual["tools"], "instructions": len(actual["instructions"]), "legacy_identical": True}
        print("PASS", label, flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    for name in ("machine", "process", "filament"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--baseline-only", action="store_true", help="Run previously valid inputs before rebuilding")
    parser.add_argument("--compare-to", type=Path, help="Compare executable G-code instructions with a baseline run")
    parser.add_argument("--bindings-only", action="store_true", help="Run only the ordinary distinct-profile compatibility cases")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="orca-multi-obj-palette-"))
    print(f"Evidence: {work}", flush=True)
    identity = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (args.binary, args.machine, args.process, args.filament)}
    save_json(work / "identity.json", identity)
    results = {}
    profiles = {name: json.loads(getattr(args, name).read_text()) for name in ("machine", "process", "filament")}
    profiles["machine"].update(printable_area=["0x0", "180x0", "180x180", "0x180"],
                               bed_exclude_area=[], machine_start_gcode="", machine_end_gcode="",
                               change_filament_gcode="", layer_change_gcode="", before_layer_change_gcode="")
    profiles["process"].update(enable_prime_tower="1", enable_support="0", raft_layers="0",
                               brim_type="no_brim", skirt_loops="0", print_sequence="by layer")
    for name, data in profiles.items():
        save_json(work / (name + ".json"), data)

    vertices = [(-3, -3, 0), (3, -3, 0), (-3, 3, 0), (-3, -3, 6)]
    faces = [(1, 3, 2), (1, 2, 4), (1, 4, 3), (2, 3, 4)]

    def fixture(name, colors, ambient="0 0 0"):
        palette = list(dict.fromkeys(colors))
        material = {color: "c" + str(i) for i, color in enumerate(palette)}
        (work / (name + ".mtl")).write_text("".join(
            f"newmtl {material[color]}\nKa {ambient}\nKd " + " ".join(str(v / 255) for v in color) + "\nd 1\n"
            for color in palette))
        text = f"mtllib {name}.mtl\n" + "".join("v %s %s %s\n" % vertex for vertex in vertices)
        for face, color in zip(faces, colors):
            text += f"usemtl {material[color]}\n" + "f %s %s %s\n" % face
        (work / (name + ".obj")).write_text(text)
        return palette

    red, blue, yellow, green = (255, 0, 0), (0, 0, 255), (255, 255, 0), (0, 255, 0)
    colors = {"multi": [red, blue, yellow, red], "yellow": [yellow] * 4, "green": [green] * 4}
    palettes = {name: fixture(name, values) for name, values in colors.items()}
    fixture("plain-red", [red] * 4)
    fixture("plain-blue", [blue] * 4)
    if args.bindings_only:
        results = check_legacy_bindings(args.binary, work, profiles["filament"])
        save_json(work / "summary.json", results)
        print(f"All {len(results)} legacy binding cases passed.")
        return
    positions = {"multi": 30, "yellow": 70, "green": 110}
    filament_paths = {}
    for color in (red, blue, yellow, green):
        hex_color = "#%02X%02X%02X" % color
        config = dict(profiles["filament"], filament_colour=[hex_color], default_filament_colour=[hex_color])
        path = work / (hex_color[1:] + ".json")
        save_json(path, config)
        filament_paths[color] = path

    def exported_colors(path, names):
        material_colors = {}
        material = None
        for line in path.with_suffix(".mtl").read_text().splitlines():
            parts = line.split()
            if parts and parts[0] == "newmtl":
                material = parts[1]
            if parts and parts[0] == "Kd":
                material_colors[material] = tuple(round(float(v) * 255) for v in parts[1:4])
        points, result = [], {name: Counter() for name in names}
        for line in path.read_text().splitlines():
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "v":
                points.append(tuple(map(float, parts[1:4])))
            elif parts[0] == "usemtl":
                material = parts[1]
            elif parts[0] == "f":
                x = sum(points[int(v.split("/")[0]) - 1][0] for v in parts[1:]) / len(parts[1:])
                name = min(names, key=lambda key: abs(positions[key] - x))
                result[name][material_colors[material]] += 1
        return result

    def run(label, names, explicit=False, rejection=False, profile_count=8, bindings=None, expected_profile_values=None):
        out = work / label
        out.mkdir()
        (out / "data").mkdir()
        cmd = [str(args.binary.resolve()), "--debug", "4", "--slice", "0", "--arrange", "0", "--auto-plate", "0",
               "--ensure-on-bed", "--allow-multicolor-oneplate", "--datadir", str(out / "data"), "--load-settings", str(work / "machine.json"),
               "--load-settings", str(work / "process.json"), "--outputdir", str(out),
               "--export-3mf", "result.3mf", "--export-multicolor-obj", "colors.obj"]
        if not explicit:
            cmd += ["--load-filaments", ";".join([str(work / "filament.json")] * profile_count)]
        for name in names:
            cmd += ["--model", name + ".obj", "--model-position", f"{positions[name]},70,0"]
            if explicit:
                paths = bindings[name] if bindings else [filament_paths[c] for c in palettes[name]]
                cmd += ["--model-filaments", ";".join(map(str, paths))]
        cmd += [str(work / (name + ".obj")) for name in names]
        save_json(out / "command.json", cmd)
        with (out / "run.log").open("w") as log:
            status = subprocess.run(cmd, cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=120).returncode
        log = (out / "run.log").read_text(errors="replace")
        save_json(out / "exit.json", {"returncode": status})
        if rejection:
            assert status > 0 and "Too many colors in automatic multi-OBJ" in log, (label, status)
        else:
            assert status == 0, (label, status, log[-2000:])
            assert exported_colors(out / "colors.obj", names) == {name: Counter(colors[name]) for name in names}, label
            with zipfile.ZipFile(out / "result.3mf") as archive:
                assert archive.testzip() is None
                gcode = [n for n in archive.namelist() if n.endswith(".gcode")]
                assert gcode and all(archive.read(n) for n in gcode), label
                instructions = [line.split(";", 1)[0].strip()
                                for n in sorted(gcode) for line in archive.read(n).decode().splitlines()
                                if line.split(";", 1)[0].strip()]
                actual_tools = {int(match.group(1)) for line in instructions
                                if (match := re.fullmatch(r"T(\d+)", line))}
                unique_colors = set(c for name in names for c in colors[name])
                expected_tools = (set(range(len(set(p for paths in bindings.values() for p in paths)))) if bindings else
                                  set(range(len(unique_colors)))) if explicit else set(range(1, len(unique_colors) + 1))
                assert expected_tools <= actual_tools, (label, expected_tools, actual_tools)
                settings = json.loads(archive.read("Metadata/project_settings.config"))
                if expected_profile_values:
                    for key, expected in expected_profile_values.items():
                        assert list(map(float, settings[key])) == expected, (label, key, settings[key])
            save_json(out / "instructions.json", instructions)
            results[label] = {"tools": sorted(actual_tools), "instructions": len(instructions)}
            if args.compare_to and (args.compare_to / label / "instructions.json").exists():
                previous = json.loads((args.compare_to / label / "instructions.json").read_text())
                assert instructions == previous, (label, "G-code changed for an existing valid input")
                results[label]["baseline_identical"] = True
            if explicit or len(names) == 1:
                assert "Merged automatic OBJ palette" not in log, label
        print("PASS", label, flush=True)

    for index, order in enumerate(itertools.permutations(colors)):
        run("order-" + str(index), order)
    run("single-multi", ["multi"])
    run("single-yellow", ["yellow"])
    run("explicit", list(colors), explicit=True)
    run("explicit-reversed", list(reversed(colors)), explicit=True)
    colors["yellow-2"] = [yellow] * 4
    palettes["yellow-2"] = fixture("yellow-2", colors["yellow-2"])
    positions["yellow-2"] = 110
    other_yellow = work / "yellow-other-material.json"
    save_json(other_yellow, dict(profiles["filament"], filament_colour=["#FFFF00"], default_filament_colour=["#FFFF00"],
                                nozzle_temperature=["225"], filament_flow_ratio=["0.91"]))
    bindings = {"yellow": [filament_paths[yellow]], "yellow-2": [other_yellow]}
    run("explicit-same-color-different-profiles", ["yellow", "yellow-2"], explicit=True, bindings=bindings,
        expected_profile_values={"nozzle_temperature": [float(profiles["filament"]["nozzle_temperature"][0]), 225.],
                                 "filament_flow_ratio": [float(profiles["filament"]["filament_flow_ratio"][0]), 0.91]})
    # Reproduce the production MTL: white ambient must not turn diffuse black white.
    colors["black-white"] = [(0, 0, 0), (255, 255, 255), red, yellow]
    palettes["black-white"] = fixture("black-white", colors["black-white"], ambient="1 1 1")
    positions["black-white"] = 30
    run("ambient-black-single", ["black-white"])
    run("ambient-black-multi", ["black-white", "yellow"])
    run("ambient-black-reversed", ["yellow", "black-white"])
    if not args.baseline_only:
        for count in (1, 4, 5):
            run("default-profiles-" + str(count), ["multi", "yellow", "green"], profile_count=count)
        run("single-default-profile", ["black-white"], profile_count=1)
    for index in range(4):
        name = "overflow-" + str(index)
        values = [(10 + index * 40 + face * 7, 30 + face * 11, 80 + index) for face in range(4)]
        colors[name] = values
        palettes[name] = fixture(name, values)
        positions[name] = 25 + index * 35
    run("reject-slot-overflow", ["overflow-" + str(i) for i in range(4)], rejection=True)
    # Fifteen painted colors plus the reserved default slot is the valid limit.
    colors["overflow-3"][-1] = colors["overflow-3"][0]
    palettes["overflow-3"] = fixture("overflow-3", colors["overflow-3"])
    run("accept-slot-limit", ["overflow-" + str(i) for i in range(4)], profile_count=16)
    if not args.baseline_only:
        run("accept-slot-limit-default", ["overflow-" + str(i) for i in range(4)], profile_count=1)
        results.update(check_legacy_bindings(args.binary, work, profiles["filament"]))
    save_json(work / "summary.json", results)
    print(f"All {len(results) + 1} multi-OBJ palette cases passed.")


if __name__ == "__main__":
    main()
