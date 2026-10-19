#!/usr/bin/env python3
"""Verify multi-plate JSON overflow projection and unchanged single-plate output.

Requires a current CLI and resolved profiles. Creates isolated fixtures, commands,
logs and exports; does not build, launch the GUI, or print.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

CORE = "{http://schemas.microsoft.com/3dmanufacturing/core/2015/02}"
PRODUCTION = "{http://schemas.microsoft.com/3dmanufacturing/production/2015/06}"
IDENTITY = "1 0 0 0 1 0 0 0 1 0 0 0"
FIELDS = {"object_name", "instance_id", "plate_index", "position", "rotation", "scale"}


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_box(path, x, y, z, centered=False):
    vertices = [(0, 0, 0), (x, 0, 0), (x, y, 0), (0, y, 0),
                (0, 0, z), (x, 0, z), (x, y, z), (0, y, z)]
    if centered:
        vertices = [(a - x / 2, b - y / 2, c) for a, b, c in vertices]
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
             (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    data = b"overflow plate fixture".ljust(80, b"\0") + struct.pack("<I", len(faces))
    for face in faces:
        data += struct.pack("<12fH", 0, 0, 0, *(v for i in face for v in vertices[i]), 0)
    path.write_bytes(data)


def transformed(points, text):
    m = [float(v) for v in text.split()]
    return [tuple(sum(m[i + 3 * j] * p[j] for j in range(3)) + m[9 + i] for i in range(3)) for p in points]


def project_state(project):
    """Read actual geometry, transforms and membership; no ZIP timestamps/UUIDs."""
    with zipfile.ZipFile(project) as archive:
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        names = {obj.get("id"): next(m.get("value") for m in obj.findall("metadata") if m.get("key") == "name")
                 for obj in settings.findall("object")}
        members = [[names[next(m.get("value") for m in inst.findall("metadata") if m.get("key") == "object_id")]
                    for inst in plate.findall("model_instance")] for plate in settings.findall("plate")]
        objects = {obj.get("id"): obj for obj in model.find(CORE + "resources").findall(CORE + "object")}
        instances = []
        for item in model.find(CORE + "build"):
            points = []
            parts = []
            for component in objects[item.get("objectid")].find(CORE + "components"):
                path = component.get(PRODUCTION + "path").lstrip("/")
                mesh_bytes = archive.read(path)
                part_model = ET.fromstring(mesh_bytes)
                part = next(obj for obj in part_model.find(CORE + "resources")
                            if obj.get("id") == component.get("objectid"))
                vertices = [tuple(float(v.get(axis)) for axis in "xyz")
                            for v in part.find(CORE + "mesh").find(CORE + "vertices")]
                points.extend(transformed(vertices, component.get("transform", IDENTITY)))
                parts.append((hashlib.sha256(mesh_bytes).hexdigest(), component.get("transform", IDENTITY)))
            points = transformed(points, item.get("transform", IDENTITY))
            low = [min(p[i] for p in points) for i in range(3)]
            high = [max(p[i] for p in points) for i in range(3)]
            instances.append({"name": names[item.get("objectid")], "parts": parts,
                              "transform": item.get("transform", IDENTITY), "printable": item.get("printable"),
                              "position": [(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, low[2]],
                              "size": [high[i] - low[i] for i in range(3)], "vertices": sorted(points)})
    return {"members": members, "instances": instances}


def offset_project_z(source, target, offsets):
    """Create a fixture with explicit nonzero bottom heights; preserve meshes."""
    ET.register_namespace("", CORE[1:-1])
    ET.register_namespace("p", PRODUCTION[1:-1])
    with zipfile.ZipFile(source) as archive, zipfile.ZipFile(target, "w") as output:
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        names = {obj.get("id"): next(m.get("value") for m in obj.findall("metadata") if m.get("key") == "name")
                 for obj in settings.findall("object")}
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        changed = set()
        for item in model.find(CORE + "build"):
            name = names[item.get("objectid")]
            if name in offsets:
                values = item.get("transform", IDENTITY).split()
                values[11] = str(float(values[11]) + offsets[name])
                item.set("transform", " ".join(values))
                changed.add(name)
        assert changed == set(offsets), changed
        for entry in archive.infolist():
            data = archive.read(entry)
            if entry.filename == "3D/3dmodel.model":
                data = ET.tostring(model, encoding="utf-8", xml_declaration=True)
            output.writestr(entry, data)


def check_transform_vertices(row, actual, vertices):
    """Reconstruct centered source geometry using the public JSON contract."""
    rx, ry, rz = map(math.radians, row["rotation"])
    points = []
    for vertex in vertices:
        x, y, z = (v * s for v, s in zip(vertex, row["scale"]))
        y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
        x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
        x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
        points.append((x, y, z))
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    origin = [(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, low[2]]
    projected = [tuple(p[i] - origin[i] + row["position"][i] for i in range(3)) for p in points]
    physical = actual["vertices"]
    for source, target in ((projected, physical), (physical, projected)):
        assert all(any(max(abs(a - b) for a, b in zip(p, q)) < 1e-4 for q in target) for p in source), (row, source, target)


def write_tetrahedron(path):
    """Use an asymmetric solid: a box cannot expose a lost mirror direction."""
    vertices = [(0, 0, 0), (10, 0, 0), (0, 7, 0), (1, 2, 5)]
    faces = [(0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)]
    data = b"mirror transform fixture".ljust(80, b"\0") + struct.pack("<I", len(faces))
    for face in faces:
        data += struct.pack("<12fH", 0, 0, 0, *(v for i in face for v in vertices[i]), 0)
    path.write_bytes(data)
    return vertices


def set_instance_linear(source, target, diagonal, rotation):
    """Change the saved instance, preserving its mesh and existing translation."""
    ET.register_namespace("", CORE[1:-1])
    ET.register_namespace("p", PRODUCTION[1:-1])
    c, s = math.cos(math.radians(rotation)), math.sin(math.radians(rotation))
    x, y, z = diagonal
    with zipfile.ZipFile(source) as archive, zipfile.ZipFile(target, "w") as output:
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        items = model.find(CORE + "build")
        assert len(items) == 1
        values = items[0].get("transform", IDENTITY).split()
        values[:9] = map(str, (c * x, s * x, 0, -s * y, c * y, 0, 0, 0, z))
        items[0].set("transform", " ".join(values))
        for entry in archive.infolist():
            output.writestr(entry, ET.tostring(model) if entry.filename == "3D/3dmodel.model" else archive.read(entry))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    for name in ("machine", "process", "filament"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--outputdir", type=Path, required=True, help="New evidence directory")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = args.outputdir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    identity = {str(binary): sha256(binary), str(Path(__file__).resolve()): sha256(Path(__file__))}
    for name in ("machine", "process", "filament"):
        source = getattr(args, name).resolve(strict=True)
        identity[str(source)] = sha256(source)
        config = json.loads(source.read_text(encoding="utf-8"))
        if name == "machine":
            config.update(printable_area=["0x0", "100x0", "100x100", "0x100"],
                          printable_height="100", bed_exclude_area=[])
        if name == "process":
            config.update(enable_support="0", enforce_support_layers="0", raft_layers="0",
                          brim_type="no_brim", skirt_loops="0", enable_prime_tower="0", print_sequence="by layer")
        save_json(work / (name + ".json"), config)
    for i in range(5):
        write_box(work / f"normal-{i}.stl", 45, 45, 20)
    write_box(work / "oversized-corner.stl", 150, 150, 20)
    write_box(work / "oversized-center.stl", 300, 300, 20, centered=True)
    write_box(work / "tall.stl", 20, 20, 120)
    results = []

    def run(name, inputs, mode=1, arrange=True, export=True, machine="machine.json", ensure_on_bed=False, extra=(), error=None):
        case = work / name
        case.mkdir()
        (case / "data").mkdir()
        output = case / "output"
        output.mkdir()
        command = [str(binary), "--debug", "4", "--datadir", str(case / "data"), "--outputdir", str(output),
                   "--load-settings", f"{work / machine};{work / 'process.json'}",
                   "--load-filaments", str(work / "filament.json"), "--arrange", "1" if arrange else "0",
                   "--arrange-spacing", "2", "--ignore-wipe-tower", "--export-3mf", "arranged.3mf"]
        if export:
            command += ["--export-model-transforms"]
        if ensure_on_bed:
            command += ["--ensure-on-bed"]
        if mode is not None:
            command += ["--auto-plate", str(mode)]
        command += list(extra)
        command += [str(path) for path in inputs]
        save_json(case / "command.json", command)
        with (case / "run.log").open("wb") as log:
            process = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        report = json.loads((output / "result.json").read_text(encoding="utf-8"))
        result = {"case": name, "process_code": process.returncode, "report_code": report["return_code"]}
        results.append(result)
        save_json(work / "results.json", results)
        if error is not None:
            assert process.returncode != 0 and report["return_code"] == error, result
            assert not (output / "arranged.3mf").exists() and not list(output.glob("*.gcode")), result
            result["passed"] = True
            save_json(work / "results.json", results)
            return None
        assert process.returncode == 0 and report["return_code"] == 0, result
        assert "invalid obj_id" not in (case / "run.log").read_text(errors="replace"), name
        project = output / "arranged.3mf"
        state = project_state(project)
        rows = report.get("model_transforms", [])
        assert bool(rows) == export, (name, rows)
        assert all(set(row) == FIELDS for row in rows), (name, rows)
        result["physical_plate_counts"] = [len(plate) for plate in state["members"]]
        save_json(case / "physical-state.json", state)
        return project, rows, state, result

    def check_output(rows, state, overflow_names, center=(50, 50)):
        overflow = [r for r in rows if r["object_name"] in overflow_names]
        normal = [r for r in rows if r["object_name"] not in overflow_names]
        assert len(overflow) == len(overflow_names), rows
        count = len(state["members"])
        assert [r["plate_index"] for r in overflow] == list(range(count, count + len(overflow))), overflow
        for row in overflow:
            assert all(abs(a - b) < 1e-5 for a, b in zip(row["position"][:2], center)), row
            assert row["position"][2] == 0, row
            assert row["scale"] == [1, 1, 1] and row["rotation"] == [0, 0, 0], row
        for row in normal:
            assert 0 <= row["plate_index"] < count, row
            assert row["object_name"] in state["members"][row["plate_index"]], row
            assert all(0 <= v <= 100 for v in row["position"][:2]), row
            if row["plate_index"] == 0:
                actual = next(i for i in state["instances"] if i["name"] == row["object_name"])
                assert all(abs(a - b) < 1e-5 for a, b in zip(row["position"], actual["position"])), row

    def paired(name, inputs, overflow_names, center=(50, 50), **kwargs):
        project, rows, state, result = run(name, inputs, **kwargs)
        _, _, plain, plain_result = run(name + "-no-json", inputs, export=False, **kwargs)
        # Exporting JSON must not add physical plates, move objects, alter mesh,
        # rotation/scale, printability, or 3MF plate membership.
        assert state == plain, name
        if kwargs.get("mode") == 0:
            # Single-plate mode retains real membership (or the historical
            # plate-0 fallback) and actual XYZ, including overflow/raised models.
            # Reopened projects may still contain multiple physical plates.
            for row in rows:
                actual = next(i for i in state["instances"] if i["name"] == row["object_name"])
                owner = next((i for i, members in enumerate(state["members"])
                              if row["object_name"] in members), 0)
                assert row["plate_index"] == owner, row
                assert all(abs(a - b) < 1e-5 for a, b in zip(row["position"], actual["position"])), row
        else:
            check_output(rows, state, overflow_names, center)
        result["passed"] = plain_result["passed"] = True
        save_json(work / "results.json", results)
        return project, rows, state

    normal = [work / f"normal-{i}.stl" for i in range(4)]
    overflow = [work / name for name in ("oversized-corner.stl", "oversized-center.stl", "tall.stl")]
    overflow_names = {p.name for p in overflow}
    mixed, original_rows, state = paired("mixed", normal + overflow, overflow_names)
    assert len(state["members"]) < len({r["plate_index"] for r in original_rows}), state
    # Overflow instances project to Z=0; ordinary instances retain their actual
    # positive/negative bottoms. All physical transforms stay unchanged.
    offsets = {"normal-0.stl": 7, "normal-1.stl": -2.5, "oversized-corner.stl": -2.5, "oversized-center.stl": 7}
    offset_project = work / "offset-z.3mf"
    offset_project_z(mixed, offset_project, offsets)
    _, rows, offset_state = paired("offset-z", [offset_project], overflow_names, arrange=False)
    for name, z in offsets.items():
        actual = next(i for i in offset_state["instances"] if i["name"] == name)
        row = next(r for r in rows if r["object_name"] == name)
        assert abs(actual["position"][2] - z) < 1e-5, actual
        assert abs(row["position"][2] - (0 if name in overflow_names else z)) < 1e-5, row
    # A model initially above the height limit becomes contained after explicit
    # bed placement. Export must not rely on the earlier outside-instance cache.
    elevated_project = work / "elevated.3mf"
    offset_project_z(mixed, elevated_project, {"normal-0.stl": 85})
    _, rows, bed_state = paired("ensure-on-bed", [elevated_project], overflow_names, arrange=False, ensure_on_bed=True)
    normal_row = next(r for r in rows if r["object_name"] == "normal-0.stl")
    normal_actual = next(i for i in bed_state["instances"] if i["name"] == "normal-0.stl")
    assert normal_row["plate_index"] == 0 and abs(normal_actual["position"][2]) < 1e-5, normal_row
    for name, arrange in (("reopen", False), ("rearrange", True)):
        _, rows, _ = paired(name, [mixed], overflow_names, arrange=arrange)
        if not arrange:
            assert {r["object_name"]: r["plate_index"] for r in rows} == {
                r["object_name"]: r["plate_index"] for r in original_rows}
    paired("omitted-auto-plate", normal + overflow, overflow_names, mode=None)
    single_project, _, _ = paired("explicit-single", normal + overflow, overflow_names, mode=0)
    single_offset = work / "single-offset-z.3mf"
    offset_project_z(single_project, single_offset, offsets)
    _, rows, state = paired("single-offset-z", [single_offset], overflow_names, mode=0, arrange=False)
    for name, z in offsets.items():
        row = next(r for r in rows if r["object_name"] == name)
        assert abs(row["position"][2] - z) < 1e-5, row
    paired("single-reopen-multi-project", [mixed], overflow_names, mode=0, arrange=False)
    paired("single-capacity-overflow", normal + [work / "normal-4.stl"], set(), mode=0)
    # Fixed-position raw inputs must have membership even when no arrangement
    # occurs. Compare single/multi mode without allowing JSON to move anything.
    for mode, prefix in ((0, "single"), (1, "multi")):
        _, rows, _ = paired(prefix + "-fixed", [normal[0]], set(), mode=mode, arrange=False,
                            extra=("--model", normal[0].name, "--model-position", "30,40,0"))
        assert rows[0]["plate_index"] == 0 and rows[0]["position"] == [30, 40, 0], rows
        _, rows, _ = paired(prefix + "-fixed-outside", [normal[0]], {normal[0].name}, mode=mode, arrange=False,
                            extra=("--model", normal[0].name, "--model-position", "125,40,0"))
        if mode == 0:
            assert rows[0]["plate_index"] == 0 and rows[0]["position"] == [125, 40, 0], rows
    paired("all-overflow", overflow, overflow_names)
    _, rows, state = paired("normal-control", normal + [work / "normal-4.stl"], set())
    assert [len(p) for p in state["members"]] == [4, 1] and {r["plate_index"] for r in rows} == {0, 1}
    # A non-square bed verifies center comes from the effective printer profile.
    wide = json.loads((work / "machine.json").read_text())
    wide["printable_area"] = ["0x0", "120x0", "120x80", "0x80"]
    save_json(work / "wide-machine.json", wide)
    paired("wide-bed", overflow, overflow_names, center=(60, 40), machine="wide-machine.json")
    # Bounding-box containment is insufficient for a non-rectangular bed.
    triangle = json.loads((work / "machine.json").read_text())
    triangle["printable_area"] = ["0x0", "100x0", "0x100"]
    save_json(work / "triangle-machine.json", triangle)
    for name, position, outside, mode in (("triangle-inside", "25,25,0", False, 1),
                                         ("triangle-outside", "75,75,0", True, 1),
                                         ("triangle-partly-outside", "50,50,0", True, 1),
                                         ("triangle-single-outside", "75,75,0", True, 0)):
        paired(name, [normal[0]], {normal[0].name} if outside else set(), mode=mode, arrange=False,
               machine="triangle-machine.json", extra=("--model", normal[0].name, "--model-position", position))
    # Compare JSON's reconstructed vertices with the actual saved geometry,
    # not merely with expected Euler values or an axis-aligned bounding box.
    asymmetric = work / "asymmetric.stl"
    write_box(asymmetric, 8, 6, 2)
    # The service uses per-model transforms independently of global options.
    # Exercise both paths without requiring a mixed-transform use case.
    for mode in (0, 1):
        for label, options in (("rotate", ("--model-rotate", "20,30,40")),
                               ("scale", ("--model-scale", "2")),
                               ("rotate-scale", ("--model-rotate", "0,0,90", "--model-scale", "2", "--slice", "0"))):
            name = f"per-model-{mode}-{label}"
            _, rows, state = paired(name, [asymmetric], set(), mode=mode, arrange=False, ensure_on_bed=True,
                                    extra=("--model", asymmetric.name, "--model-position", "50,50,0", *options))
            check_transform_vertices(rows[0], state["instances"][0],
                                     [(x, y, z) for x in (0, 8) for y in (0, 6) for z in (0, 2)])
            assert rows[0]["plate_index"] == 0
            if label == "rotate-scale":
                for case in (name, name + "-no-json"):
                    assert (work / case / "output/plate_1.gcode").stat().st_size > 0
    for label, options in (("scale", ("--scale", "2")),
                           ("rotate", ("--rotate", "90")),
                           ("xyz", ("--rotate-x", "20", "--rotate-y", "30", "--rotate", "40")),
                           ("combined", ("--scale", "2", "--rotate", "90", "--model-scale", "1.5",
                                          "--model-rotate", "10,20,30")),
                           ("oriented", ("--rotate", "30", "--orient", "1")),
                           ("sliced", ("--scale", "2", "--rotate", "90", "--slice", "0"))):
        _, rows, state = paired("global-" + label, [asymmetric], set(), mode=0, arrange=False, ensure_on_bed=True,
                                extra=("--model", asymmetric.name, "--model-position", "50,50,0", *options))
        check_transform_vertices(rows[0], state["instances"][0], [(x, y, z) for x in (0, 8) for y in (0, 6) for z in (0, 2)])
        if label == "sliced":
            for name in ("global-sliced", "global-sliced-no-json"):
                assert (work / name / "output/plate_1.gcode").stat().st_size > 0
    # Repetitions clone object IDs after orientation; both original and copy
    # must retain the same transform history, even for a nontrivial resting pose.
    seed, _, seed_state = paired("transform-seed", [asymmetric], set(), mode=0, arrange=False,
                                  extra=("--model", asymmetric.name, "--model-position", "50,50,0"))
    for label, options in (("repeat-orient", ("--orient", "1", "--rotate-x", "35")),
                           ("repeat-scale", ("--scale", "2"))):
        _, rows, state = paired(label, [seed], set(), mode=0,
                                extra=("--slice", "1", "--repetitions", "2", *options))
        assert len(rows) == 2
        for row, actual in zip(rows, state["instances"]):
            check_transform_vertices(row, actual, seed_state["instances"][0]["vertices"])
    # Nonuniform instance scaling followed by a volume rotation produces shear,
    # which rotation+scale fields cannot encode. Plain 3MF export stays valid.
    nonuniform = work / "nonuniform.3mf"
    with zipfile.ZipFile(seed) as archive, zipfile.ZipFile(nonuniform, "w") as output:
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        item = model.find(CORE + "build")[0]
        values = item.get("transform").split()
        values[0] = "2"
        item.set("transform", " ".join(values))
        for entry in archive.infolist():
            output.writestr(entry, ET.tostring(model) if entry.filename == "3D/3dmodel.model" else archive.read(entry))
    run("unrepresentable-transform", [nonuniform], mode=0, arrange=False, extra=("--rotate", "45"), error=-2)
    _, _, _, result = run("unrepresentable-transform-no-json", [nonuniform], mode=0, arrange=False,
                          extra=("--rotate", "45"), export=False)
    result["passed"] = True
    save_json(work / "results.json", results)
    # Separate objects may have different pre-existing volume rotations. Each
    # needs its own transform delta, rather than a single batch-wide matrix.
    second = work / "asymmetric-second.stl"
    write_box(second, 8, 6, 2)
    two, _, _ = paired("two-transform-seed", [asymmetric, second], set(), mode=0, arrange=False,
                        extra=("--model", asymmetric.name, "--model-position", "30,30,0",
                               "--model", second.name, "--model-position", "70,70,0"))
    rotated = work / "rotated-volume.3mf"
    c, s = math.cos(math.radians(30)), math.sin(math.radians(30))
    with zipfile.ZipFile(two) as archive, zipfile.ZipFile(rotated, "w") as output:
        model = ET.fromstring(archive.read("3D/3dmodel.model"))
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
        component = model.find(CORE + "resources")[1].find(CORE + "components")[0]
        values = component.get("transform").split()
        values[:9] = list(map(str, (c, 0, -s, 0, 1, 0, s, 0, c)))
        component.set("transform", " ".join(values))
        matrix = next(m for m in settings.findall("object")[1].find("part").findall("metadata") if m.get("key") == "matrix")
        matrix.set("value", " ".join(map(str, (c, 0, s, 4, 0, 1, 0, 3, -s, 0, c, 1, 0, 0, 0, 1))))
        for entry in archive.infolist():
            data = archive.read(entry)
            if entry.filename == "3D/3dmodel.model":
                data = ET.tostring(model)
            elif entry.filename == "Metadata/model_settings.config":
                data = ET.tostring(settings)
            output.writestr(entry, data)
    before = project_state(rotated)
    _, rows, state = paired("different-volume-rotations", [rotated], set(), mode=0, arrange=False,
                            extra=("--rotate-x", "35"), ensure_on_bed=True)
    for row, actual, original in zip(rows, state["instances"], before["instances"]):
        check_transform_vertices(row, actual, original["vertices"])
    # Mirror is an instance transform in an existing 3MF, independent of CLI
    # global/per-model options. Check all axis combinations and their composition
    # with existing rotation, scale and orientation in both output modes.
    tetrahedron = work / "mirror-tetrahedron.stl"
    vertices = write_tetrahedron(tetrahedron)
    mirror_seed, _, _ = paired("mirror-seed", [tetrahedron], set(), mode=0, arrange=False,
                               extra=("--model", tetrahedron.name, "--model-position", "50,50,0"))
    mirror_cases = [(f"axes-{mask}", tuple(-1 if mask & (1 << axis) else 1 for axis in range(3)),
                     0, False, ()) for mask in range(8)]
    mirror_cases += [("rotated-scaled", (-1.5, 1.5, 1.5), 37, False, ()),
                     ("nonuniform", (-1.5, 1, 2), 23, False, ()),
                     ("oriented", (-1, 1, 1), 0, False, ("--orient", "1")),
                     ("oriented-nonuniform-odd-mirror", (-1.5, 1, 2), 23, False, ("--orient", "1")),
                     ("oriented-axis-scale", (1.5, 1, 1), 23, False, ("--orient", "1")),
                     ("arranged", (-1, 1, 1), 37, True, ()),
                     ("global", (-1, 1, 1), 0, False, ("--rotate", "25", "--scale", "1.2")),
                     ("sliced", (-1, 1, 1), 0, False, ("--slice", "0"))]
    for label, diagonal, angle, arrange, options in mirror_cases:
        mirrored = work / f"mirror-{label}.3mf"
        set_instance_linear(mirror_seed, mirrored, diagonal, angle)
        for mode in (0, 1):
            name = f"mirror-{label}-{mode}"
            _, rows, state = paired(name, [mirrored], set(), mode=mode, arrange=arrange,
                                    ensure_on_bed=True, extra=options)
            assert len(rows) == len(state["instances"]) == 1
            check_transform_vertices(rows[0], state["instances"][0], vertices)
            if label == "sliced":
                paths = []
                for case in (name, name + "-no-json"):
                    text = (work / case / "output/plate_1.gcode").read_text(encoding="utf-8")
                    assert re.search(r"^G1 .*\b[XY][-\d.]+.*\bE[\d.]+", text, re.MULTILINE), case
                    paths.append([line.split(";", 1)[0].strip() for line in text.splitlines()
                                  if line.startswith(("G0 ", "G1 "))])
                assert paths[0] == paths[1], "Exporting mirror JSON changed the toolpath"
    # Orientation after nonuniform scaling can produce nonorthogonal axes even
    # without an odd reflection. Reject misleading JSON while retaining the
    # actual transformed 3MF when JSON is not requested.
    for label, diagonal in (("positive", (1.5, 1, 2)), ("even-mirror", (1.5, -1, -2))):
        fixture = work / f"oriented-shear-{label}.3mf"
        set_instance_linear(mirror_seed, fixture, diagonal, 23)
        for mode in (0, 1):
            name = f"oriented-shear-{label}-{mode}"
            run(name, [fixture], mode=mode, arrange=False, ensure_on_bed=True, extra=("--orient", "1"), error=-2)
            _, _, state, result = run(name + "-no-json", [fixture], mode=mode, arrange=False,
                                      ensure_on_bed=True, extra=("--orient", "1"), export=False)
            actual = state["instances"][0]
            assert len(state["instances"]) == len(actual["parts"]) == 1
            basis = transformed(transformed([(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)],
                                            actual["parts"][0][1]), actual["transform"])
            axes = [tuple(p[i] - basis[0][i] for i in range(3)) for p in basis[1:]]
            assert any(abs(sum(a * b for a, b in zip(axes[i], axes[j]))) > 1e-4
                       for i, j in ((0, 1), (0, 2), (1, 2))), "Expected an affine transform that rotation/scale cannot encode"
            result["passed"] = True
            save_json(work / "results.json", results)
    # Output-only plate indices are not limited by the physical 36-plate cap.
    many = [work / f"overflow-{i}.stl" for i in range(37)]
    for path in many:
        write_box(path, 150, 150, 20)
    _, rows, state = paired("37-overflow", many, {p.name for p in many})
    assert len({r["plate_index"] for r in rows}) == 37 and len(state["members"]) == 1
    for path in work.iterdir():
        if path.is_file():
            identity[str(path)] = sha256(path)
    assert sha256(binary) == identity[str(binary)], "Binary changed during validation"
    save_json(work / "identity.json", identity)
    save_json(work / "validation.json", {"passed": True, "cases": len(results), "binary": str(binary),
                                         "binary_sha256": sha256(binary), "gui_tested": False})
    print(f"PASS: {len(results)} CLI runs; JSON-only projection and unchanged 3MF; evidence: {work}")


if __name__ == "__main__":
    main()
