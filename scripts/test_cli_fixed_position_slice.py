#!/usr/bin/env python3
"""Exercise the real CLI slicing contract with isolated, self-contained fixtures.

No build, network, GUI, printing or user profiles. Keep commands, logs, binary
identity and output in a new evidence directory. A zero exit alone never passes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from test_cli_slice_analysis import gcode_facts, read_json


CORE = "{http://schemas.microsoft.com/3dmanufacturing/core/2015/02}"


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_box(path, x=8, y=6, z=2):
    vertices = [(0, 0, 0), (x, 0, 0), (x, y, 0), (0, y, 0),
                (0, 0, z), (x, 0, z), (x, y, z), (0, y, z)]
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
             (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    if path.suffix == ".obj":
        path.write_text("".join("v %s %s %s\n" % v for v in vertices) +
                        "".join("f %s %s %s\n" % tuple(i + 1 for i in f) for f in faces))
    else:
        data = b"CLI fixed position regression".ljust(80, b"\0") + struct.pack("<I", len(faces))
        for face in faces:
            data += struct.pack("<12fH", 0, 0, 0, *(v for i in face for v in vertices[i]), 0)
        path.write_bytes(data)


def write_profiles(work):
    # Explicit small printer and safe synthetic G-code. Remaining settings use
    # the application's defaults; no installed/vendor profile inheritance.
    profiles = {
        "machine": dict(printer_technology="FFF", printer_model="", printer_variant="0.4",
                        gcode_flavor="marlin2", printable_area=["0x0", "100x0", "100x100", "0x100"],
                        printable_height="100", bed_exclude_area=[], nozzle_diameter=["0.4"],
                        machine_start_gcode="G90\nM83", machine_end_gcode="", change_filament_gcode="",
                        layer_change_gcode="G92 E0", before_layer_change_gcode=""),
        "process": dict(layer_height="0.2", initial_layer_print_height="0.2", wall_loops="2",
                        top_shell_layers="2", bottom_shell_layers="2", sparse_infill_density="15%",
                        enable_support="0", raft_layers="0", brim_type="no_brim", skirt_loops="0",
                        enable_prime_tower="0", print_sequence="by layer", curr_bed_type="Textured PEI Plate"),
        "filament": dict(filament_type=["PLA"], filament_diameter=["1.75"], filament_density=["1.24"],
                         filament_flow_ratio=["1"], nozzle_temperature=["210"],
                         nozzle_temperature_initial_layer=["210"], textured_plate_temp=["60"],
                         textured_plate_temp_initial_layer=["60"]),
    }
    for name, values in profiles.items():
        config = dict(type=name, name="CLI regression " + name, **{"from": "system"}, **values)
        if name != "machine":
            config["compatible_printers"] = ["CLI regression machine"]
        save_json(work / (name + ".json"), config)


def project_variant(source, destination, disabled=(), empty=()):
    """Change printing state only; preserve geometry, transforms and plate IDs."""
    ET.register_namespace("", CORE[1:-1])
    ET.register_namespace("p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06")
    with zipfile.ZipFile(source) as archive, zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        plates = settings.findall("plate")
        assert len(plates) == 2, "Seed must contain two physical plates"
        disable_ids, remove_ids = set(), set()
        for index, plate in enumerate(plates):
            for instance in list(plate.findall("model_instance")):
                values = {m.get("key"): m.get("value") for m in instance.findall("metadata")}
                if index in disabled:
                    disable_ids.add(values["object_id"])
                if index in empty:
                    remove_ids.add(values["object_id"])
                    plate.remove(instance)
        build = model.find(CORE + "build")
        for item in list(build):
            if item.get("objectid") in disable_ids:
                item.set("printable", "0")
            if item.get("objectid") in remove_ids:
                build.remove(item)
        for entry in archive.infolist():
            data = archive.read(entry)
            if entry.filename == "Metadata/model_settings.config":
                data = ET.tostring(settings, encoding="utf-8", xml_declaration=True)
            elif entry.filename == "3D/3dmodel.model":
                data = ET.tostring(model, encoding="utf-8", xml_declaration=True)
            output.writestr(entry, data)


def instance_variant(source, destination, *, printable, offset):
    """Move only the second instance in an actual CLI-exported project."""
    ET.register_namespace("", CORE[1:-1])
    ET.register_namespace("p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06")
    with zipfile.ZipFile(source) as archive, zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        items = model.find(CORE + "build")
        assert len(items) == 2, "Seed must contain two instances"
        items[1].set("printable", "1" if printable else "0")
        items[1].set("transform", "1 0 0 0 1 0 0 0 1 " + " ".join(map(str, offset)))
        for entry in archive.infolist():
            data = archive.read(entry)
            if entry.filename == "3D/3dmodel.model":
                data = ET.tostring(model, encoding="utf-8", xml_declaration=True)
            output.writestr(entry, data)


def check_gcode_positions(gcode, expected):
    """Verify the toolpaths as well as JSON; profiles explicitly use relative E."""
    points = {name: [] for name in expected}
    active, x, y = None, None, None
    for line in gcode.splitlines():
        match = re.match(r"; printing object (.+) id:", line)
        if match:
            active = match[1]
        elif line.startswith("; stop printing object"):
            active = None
        if not line.startswith(("G0 ", "G1 ")):
            continue
        values = {axis: float(value) for axis, value in re.findall(r"\b([XYE])(-?[\d.]+)", line)}
        x, y = values.get("X", x), values.get("Y", y)
        if active in points and values.get("E", 0) > 0 and ("X" in values or "Y" in values):
            assert x is not None and y is not None, line
            points[active].append((x, y))
    for name, position in expected.items():
        assert points[name], f"No extrusion for {name}"
        for axis, half_size in ((0, 4), (1, 3)):
            low, high = min(p[axis] for p in points[name]), max(p[axis] for p in points[name])
            assert position[axis] - half_size - 0.5 <= low < high <= position[axis] + half_size + 0.5, (name, low, high)
            assert abs((low + high) / 2 - position[axis]) < 0.3, (name, low, high)


def check_project_membership(project):
    """Every saved plate member must reference an existing build instance."""
    with zipfile.ZipFile(project) as archive:
        assert archive.testzip() is None, "Corrupt project"
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        instances = {}
        for item in model.find(CORE + "build"):
            object_id = item.get("objectid")
            instances[object_id] = instances.get(object_id, 0) + 1
        for plate in settings.findall("plate"):
            for member in plate.findall("model_instance"):
                values = {m.get("key"): m.get("value") for m in member.findall("metadata")}
                assert values["object_id"] in instances, ("Missing plate object", values)
                assert 0 <= int(values["instance_id"]) < instances[values["object_id"]], values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--outputdir", type=Path, required=True, help="New evidence directory")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = args.outputdir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    write_profiles(work)
    for name in ("box.stl", "box.obj", "second.stl"):
        write_box(work / name)
    for name in ("large-a.stl", "large-b.stl"):
        write_box(work / name, 65, 65, 0.6)
    results = []
    identity = {"binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "analysis_helper_sha256": hashlib.sha256(
                    Path(__file__).with_name("test_cli_slice_analysis.py").read_bytes()).hexdigest(), "cases": results}
    save_json(work / "validation.json", identity)

    def run(name, inputs, *, arrange=0, auto_plate=0, plate=0, positions=(), expected=(1,),
            error=None, export=False, slicing=True, transforms=True, extra=(), toolpaths=None, absent=(),
            analysis=False, compare_to=None):
        case = work / name
        case.mkdir()
        output = case / "output"
        output.mkdir()
        command = [str(binary), "--debug", "4", "--datadir", str(case / "data"), "--outputdir", str(output),
                   "--load-settings", f"{work / 'machine.json'};{work / 'process.json'}",
                   "--load-filaments", str(work / "filament.json"), "--auto-plate", str(auto_plate),
                   "--arrange", str(arrange), "--ensure-on-bed", "--allow-multicolor-oneplate"]
        command += list(extra)
        if analysis:
            command += ["--slice-analysis-report"]
        if slicing:
            command += ["--slice", str(plate)]
        if export:
            command += ["--export-3mf", "model.3mf"]
        if positions:
            if transforms:
                command += ["--export-model-transforms"]
            for path, position in zip(inputs, positions):
                command += ["--model", path.name, "--model-position", ",".join(map(str, position)),
                            "--model-scale", "1", "--model-rotate", "0,0,0", "--model-support", "0"]
        command += [str(path) for path in inputs]
        save_json(case / "command.json", command)
        result = {"case": name, "passed": False}
        try:
            with (case / "run.log").open("wb") as log:
                process = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            result["process_code"] = process.returncode
            report = json.loads((output / "result.json").read_text(encoding="utf-8"))
            result["report_code"] = report["return_code"]
            files = sorted(p.name for p in output.glob("*.gcode"))
            result["gcode_files"] = files
            analysis_files = sorted(p.name for p in output.glob("*.analysis.json"))
            result["analysis_files"] = analysis_files
            log_text = (case / "run.log").read_text(errors="replace")
            if error is not None:
                assert process.returncode != 0 and report["return_code"] in error, result
                assert not files and "All done, Success" not in log_text, result
                assert not analysis_files, "Failed slice published a successful analysis report"
            else:
                assert process.returncode == 0 and report["return_code"] == 0, result
                assert files == [f"plate_{i}.gcode" for i in expected], result
                for filename in files:
                    gcode = (output / filename).read_text(errors="replace")
                    assert re.search(r"^G1 .*\b[XY][-\d.]+.*\bE[\d.]+", gcode, re.MULTILINE), "No extrusion moves"
                    if positions:
                        check_gcode_positions(gcode, {path.name: position for path, position in zip(inputs, positions)})
                    if toolpaths:
                        check_gcode_positions(gcode, toolpaths)
                    for object_name in absent:
                        assert f"; printing object {object_name} id:" not in gcode, object_name
                if positions and transforms:
                    rows = {row["object_name"]: row for row in report["model_transforms"]}
                    for path, position in zip(inputs, positions):
                        row = rows[path.name]
                        assert all(abs(a - b) < 1e-5 for a, b in zip(row["position"], position)), row
                        assert row["plate_index"] == 0, row
                if not transforms:
                    assert not report.get("model_transforms"), "Unexpected transform JSON"
                if export:
                    assert (output / "model.3mf").is_file(), "No exported project"
                    check_project_membership(output / "model.3mf")
                    if "--min-save" in extra:
                        with zipfile.ZipFile(output / "model.3mf") as archive:
                            assert archive.testzip() is None, "Corrupt sliced project"
                            packaged = sorted(n for n in archive.namelist() if n.endswith(".gcode"))
                            assert packaged == ["Metadata/" + filename for filename in files], packaged
                            for filename in files:
                                assert archive.read("Metadata/" + filename) == (output / filename).read_bytes(), filename
                if analysis:
                    assert analysis_files == sorted(f"plate_{i}.analysis.json" for i in expected), result
                    assert compare_to and any(case["case"] == compare_to and case["passed"] for case in results), \
                        "Analysis comparison requires a successful opt-out baseline"
                    result["analysis_compared_to"] = compare_to
                    result["commands_sha256"] = {}
                    for index in expected:
                        value = read_json(output / f"plate_{index}.analysis.json")
                        assert value["schema_version"] == 1 and value["scope"] == "sliced_print_metrics", value
                        assert value["plate_index"] == index and value["gcode_file"] == f"plate_{index}.gcode", value
                        facts = gcode_facts(output / value["gcode_file"])
                        original = gcode_facts(work / compare_to / "output" / value["gcode_file"])
                        for field in ("commands", "configuration", "layers"):
                            assert facts[field] == original[field], f"Analysis changed {field} for plate {index}"
                        result["commands_sha256"][str(index)] = facts["commands_sha256"]
            if not analysis:
                assert not analysis_files, "Opt-out invocation published an analysis report"
            result["passed"] = True
        except (AssertionError, OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.TimeoutExpired) as exc:
            result["error"] = str(exc)
        results.append(result)
        save_json(work / "validation.json", identity)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        return output / "model.3mf" if result["passed"] and export else None

    box, obj = work / "box.stl", work / "box.obj"
    run("arrange-control", [box], arrange=1)
    run("fixed-stl", [box], positions=[(20, 30, 0)])
    run("fixed-obj", [obj], positions=[(20, 30, 0)])
    run("fixed-stl-analysis", [box], positions=[(20, 30, 0)], analysis=True, compare_to="fixed-stl")
    run("fixed-obj-analysis", [obj], positions=[(20, 30, 0)], analysis=True, compare_to="fixed-obj")
    run("fixed-stl-no-json", [box], positions=[(20, 30, 0)], transforms=False)
    run("fixed-obj-no-json", [obj], positions=[(20, 30, 0)], transforms=False)
    run("fixed-two-models", [box, work / "second.stl"], positions=[(20, 30, 0), (70, 60, 0)])
    run("explicit-first-plate", [box], plate=1, positions=[(20, 30, 0)])
    run("outside", [box], positions=[(200, 200, 0)], expected=(), error={-50})
    run("outside-analysis", [box], positions=[(200, 200, 0)], expected=(), error={-50}, analysis=True)
    run("partly-outside", [box], positions=[(99, 30, 0)], expected=(), error={-50, -52})
    # A valid first object must not hide a second enabled object that is lost
    # outside the real plates (including single-plate arrangement overflow).
    run("mixed-outside", [box, work / "second.stl"], positions=[(20, 30, 0), (200, 200, 0)],
        expected=(), error={-50})
    run("mixed-outside-no-json", [box, work / "second.stl"], positions=[(20, 30, 0), (200, 200, 0)],
        transforms=False, expected=(), error={-50})
    run("mixed-outside-explicit", [box, work / "second.stl"], plate=1,
        positions=[(20, 30, 0), (200, 200, 0)], expected=(), error={-50})
    run("mixed-partly-outside", [box, work / "second.stl"], positions=[(20, 30, 0), (99, 30, 0)],
        expected=(), error={-52})
    run("arranged-overflow", [work / "large-a.stl", work / "large-b.stl"], arrange=1,
        expected=(), error={-50})
    run("arranged-second-plate", [work / "large-a.stl", work / "large-b.stl"], arrange=1,
        auto_plate=1, plate=2, expected=(2,))
    run("invalid-raw-plate", [box], plate=3, positions=[(20, 30, 0)], expected=(), error={-2})
    run("negative-plate", [box], plate=-1, positions=[(20, 30, 0)], expected=(), error={-2})
    assembled = run("assembled-raw-export", [box, work / "second.stl"], extra=("--assemble",),
                    slicing=False, export=True, expected=())
    run("assembled-raw-slice", [box, work / "second.stl"], extra=("--assemble",), export=True)
    if assembled:
        run("assembled-reopen-slice", [assembled])

    mixed_seed = run("mixed-project-seed", [box, work / "second.stl"], positions=[(20, 30, 0), (70, 60, 0)],
                     export=True, slicing=False, expected=())
    if mixed_seed:
        run("assembled-project-export", [mixed_seed], extra=("--assemble",), slicing=False, export=True, expected=())
        for suffix in ("stl", "obj"):
            raw = work / ("additional." + suffix)
            write_box(raw)
            for mode in (0, 1):
                for label, position, error in (("inside", "50,50,0", None),
                                                ("outside", "200,200,0", {-50}),
                                                ("partial", "99,40,0", {-52})):
                    run(f"project-new-{suffix}-{mode}-{label}", [mixed_seed, raw], auto_plate=mode,
                        extra=("--export-model-transforms", "--model", mixed_seed.name,
                               "--model", raw.name, "--model-position", position), error=error,
                        toolpaths={"box.stl": (20, 30, 0), "second.stl": (70, 60, 0), raw.name: (50, 50, 0)})
            run(f"project-new-{suffix}-explicit-outside", [mixed_seed, raw], plate=1, error={-50},
                extra=("--model", mixed_seed.name, "--model", raw.name, "--model-position", "200,200,0"))
        for name, enabled, offset in (("disabled-inside", False, (66, 57, 0)),
                                      ("disabled-partly-outside", False, (95, 27, 0)),
                                      ("disabled-outside", False, (196, 197, 0)),
                                      ("enabled-outside-project", True, (196, 197, 0))):
            target = work / (name + ".3mf")
            instance_variant(mixed_seed, target, printable=enabled, offset=offset)
            # A saved project may intentionally retain unassigned geometry in
            # its editing area. Match GUI plate selection; raw batches are strict.
            run(name, [target],
                toolpaths={"box.stl": (20, 30, 0)}, absent=("second.stl",))
        run("disabled-triangle-budget", [work / "disabled-inside.3mf"], extra=("--mtcpp", "12"),
            toolpaths={"box.stl": (20, 30, 0)}, absent=("second.stl",))
        run("disabled-partly-outside-explicit", [work / "disabled-partly-outside.3mf"], plate=1,
            toolpaths={"box.stl": (20, 30, 0)}, absent=("second.stl",))
        # Selecting a physical plate does not request unrelated unassigned
        # objects elsewhere in a saved project, unlike the all-input action.
        run("explicit-project-with-unassigned", [work / "enabled-outside-project.3mf"], plate=1,
            toolpaths={"box.stl": (20, 30, 0)}, absent=("second.stl",))
        raised = work / "raised-project.3mf"
        instance_variant(mixed_seed, raised, printable=True, offset=(66, 57, 105))
        run("raised-project-ensure-bed", [raised],
            toolpaths={"box.stl": (20, 30, 0), "second.stl": (70, 60, 0)})
        with zipfile.ZipFile(mixed_seed) as archive:
            settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        second = settings.find("plate").findall("model_instance")[1]
        skip_id = next(m.get("value") for m in second.findall("metadata") if m.get("key") == "identify_id")
        run("explicitly-skipped-outside", [work / "enabled-outside-project.3mf"], extra=("--skip-objects", skip_id),
            toolpaths={"box.stl": (20, 30, 0)}, absent=("second.stl",))
    # Build real multi-plate metadata with the CLI; then change only the print
    # switches/build entries to exercise skips, without inventing plate geometry.
    seed = run("two-plate-seed", [work / "large-a.stl", work / "large-b.stl"], arrange=1,
               auto_plate=1, export=True, expected=(), slicing=False)
    if seed:
        run("two-plates", [seed], expected=(1, 2))
        run("two-plates-analysis", [seed], expected=(1, 2), analysis=True, compare_to="two-plates")
        run("two-plates-sliced-project", [seed], expected=(1, 2), export=True, extra=("--min-save",))
        for name, disabled, empty in (("first-disabled", (0,), ()), ("all-disabled", (0, 1), ()),
                                      ("first-empty", (), (0,)), ("all-empty", (), (0, 1))):
            target = work / (name + ".3mf")
            project_variant(seed, target, disabled, empty)
            all_skipped = name.startswith("all-")
            # An entirely empty 3MF may be rejected during import (-6) before
            # slicing starts; disabled-but-present geometry must reach -50.
            errors = ({-6, -50} if name == "all-empty" else {-50}) if all_skipped else None
            run(name, [target], expected=() if all_skipped else (2,), error=errors)
        run("first-disabled-analysis", [work / "first-disabled.3mf"], expected=(2,),
            analysis=True, compare_to="first-disabled")
        run("all-disabled-analysis", [work / "all-disabled.3mf"], expected=(), error={-50}, analysis=True)
        run("explicit-disabled", [work / "first-disabled.3mf"], plate=1, expected=(), error={-50})
        run("explicit-empty", [work / "first-empty.3mf"], plate=1, expected=(), error={-6, -50})
        run("explicit-second", [work / "first-disabled.3mf"], plate=2, expected=(2,))
        run("disabled-first-sliced-project", [work / "first-disabled.3mf"], expected=(2,),
            export=True, extra=("--min-save",))
        run("explicit-second-sliced-project", [seed], plate=2, expected=(2,),
            export=True, extra=("--min-save",))
        run("non-slice-export", [work / "all-disabled.3mf"], expected=(), export=True, slicing=False)
    return 0 if all(case["passed"] for case in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
