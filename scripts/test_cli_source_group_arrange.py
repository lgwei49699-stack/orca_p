#!/usr/bin/env python3
"""Exercise ordered whole-source arrangement through the CLI, 3MF export and slicing."""

import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from test_cli_oversized_arrange import project_state, save_json, write_box


def verify_color_sources(work, run, box):
    red, black, green = "#FF0000", "#000000", "#00FF00"
    mtl = work / "palette.mtl"
    mtl.write_text("newmtl red\nKd 1 0 0\nnewmtl black\nKd 0 0 0\nnewmtl green\nKa 0 1 0\n"
                   "newmtl empty\nNs 20\nnewmtl unused\nKd 1 1 1\n")

    def obj(label, group, materials):
        name = label + ".obj"
        text = ["mtllib palette.mtl", "v 0 0 0", "v 6 0 0", "v 0 6 0", "v 0 0 6"]
        for face, material in zip(("1 3 2", "1 2 4", "2 3 4", "3 1 4"), materials):
            if material is not None:
                text.append("usemtl " + material)
            text.append("f " + face)
        (work / name).write_text("\n".join(text) + "\n")
        return name, group

    def verify(label, parts, expected, *, colors=None, options=None, **flags):
        opts = dict(options or {})
        for name, color in (colors or {}).items():
            opts[name] = ["--model-color", color]
        _, state = run(label, parts, mode=False, extra=("--arrange-by-color", "1"), model_opts=opts, **flags)
        assert [set(plate) for plate in state["members"]] == [{part[0] for part in plate} for plate in expected], label

    mixed = obj("red-black", 1, ["red", "black", "red", "black"])
    reverse = obj("black-red", 2, ["black", "red", "black", "red"])
    rgb = obj("red-black-green", 3, ["red", "black", "green", "black"])
    rr = obj("red-only", 1, ["red"] * 4)
    other_red = box("other-red", 4, (10, 10, 1))
    pure_green = box("green", 1, (10, 10, 1))
    # A future red partner has priority over the earlier mixed sibling. A green
    # sibling may not join red+black, nor the unrelated red+black+green plate.
    verify("palette-exact", [mixed, reverse, rr, pure_green, rgb, other_red],
           [[mixed, reverse], [rr, other_red], [pure_green], [rgb]],
           colors={pure_green[0]: green, other_red[0]: red}, sliced=True)
    verify("palette-return", [mixed, reverse, rr, pure_green], [[mixed, reverse, rr], [pure_green]], colors={pure_green[0]: green})
    verify("palette-return-later", [rr, mixed, reverse], [[rr, mixed, reverse]])
    # The file palette and each object's actual default slot survive a mixed OBJ/STL batch,
    # even when the last OBJ has only one color. A group label does not bind a filament.
    with zipfile.ZipFile(work / "palette-return" / "layout.3mf") as archive:
        filament_colors = json.loads(archive.read("Metadata/project_settings.config"))["filament_colour"]
        objects = ET.fromstring(archive.read("Metadata/model_settings.config")).findall("object")
        base_colors = {}
        for obj_state in objects:
            name = obj_state.find("metadata[@key='name']").get("value")
            slot = int(obj_state.find("metadata[@key='extruder']").get("value"))
            base_colors[name] = filament_colors[slot - 1]
        assert base_colors[mixed[0]] == red and base_colors[reverse[0]] == black
    # Without source IDs, the same OBJs still use their first face color.
    _, legacy = run("legacy-primary-color", [(mixed[0], None), (reverse[0], None)], mode=False, extra=("--arrange-by-color",))
    assert len(legacy["members"]) == 2
    verify("palette-mixed-tower", [mixed, reverse, rgb], [[mixed, reverse], [rgb]], tower=True)
    oversized = box("palette-overflow", 1, (150, 140, 2))
    rows, _ = run("palette-preview", [oversized, mixed, pure_green], mode=False, project=False, extra=("--arrange-by-color",),
                  model_opts={pure_green[0]: ["--model-color", green]})
    row = next(r for r in rows if r["object_name"] == oversized[0])
    assert row["plate_index"] == 0 and row["position"] == [50, 50, 0]
    # Incomplete coverage, missing material definition, and unknown MTL color
    # belong to the default group. Explicit black is still a known color.
    unknown = box("unknown", 6, (10, 10, 1))
    unresolved = obj("unresolved", 2, ["red", "undefined", "red", "red"])
    partial = obj("before-usemtl", 3, [None, "red", "red", "red"])
    no_rgb = obj("no-kd-or-ka", 4, ["empty"] * 4)
    known_black = box("black", 1, (8, 8, 1))
    verify("palette-unknown", [rr, unknown, unresolved, known_black, partial, no_rgb],
           [[rr], [unknown, unresolved, partial, no_rgb], [known_black]], colors={known_black[0]: black})
    # A small component must not leak out of its own (source, red) bundle.
    a = box("red-anchor", 1, (74, 74, 1))
    b = box("red-small", 2, (10, 10, 1))
    c = box("red-large", 2, (69, 69, 1))
    d = box("red-later", 3, (9, 9, 1))
    verify("palette-atomic", [a, b, c, d], [[a, d], [b, c]], colors={p[0]: red for p in [a, b, c, d]})
    # The final explicit per-material binding can turn a two-color OBJ into
    # one effective color. Group metadata must not replace its slot assignments.
    blue = str(work / "second.json")
    mapped = box("bound-stl", 8, (10, 10, 1))
    verify("palette-bindings", [mixed, mapped], [[mixed, mapped]],
           options={mixed[0]: ["--model-filaments", blue + ";" + blue], mapped[0]: ["--model-color", "#1100BB", "--model-filaments", blue]})
    verify("palette-partial-bound", [partial, mapped], [[partial, mapped]],
           options={part[0]: ["--model-filaments", blue] for part in [partial, mapped]}, sliced=True)
    run("palette-missing-id", [rr, (other_red[0], None)], mode=False, extra=("--arrange-by-color", "1"),
        error="Each STL or OBJ input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    for key in ("machine", "process", "filament"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="orca-source-group-"))
    print(f"Evidence: {work}", flush=True)
    machine = json.loads(args.machine.read_text())
    machine.update(printable_area=["0x0", "100x0", "100x100", "0x100"], printable_height="100", bed_exclude_area=[])
    process = json.loads(args.process.read_text())
    process.update(enable_support="0", enforce_support_layers="0", brim_type="no_brim", raft_layers="0",
                   skirt_loops="0", enable_prime_tower="0", print_sequence="by layer")
    save_json(work / "machine.json", machine)
    save_json(work / "process.json", process)
    save_json(work / "tower.json", dict(process, enable_prime_tower="1", timelapse_type="1",
                                        prime_tower_width="18", prime_volume="10"))
    filament = args.filament.resolve(strict=True)
    save_json(work / "second.json", dict(json.loads(filament.read_text()), filament_colour=["#1100BB"]))
    (work / "data").mkdir()

    def run(label, components, *, mode=True, extra=(), sliced=False, tower=False, error=None, model_opts=None, project=True):
        out = work / label
        out.mkdir()
        flags = ["--load-settings", f"{work / 'machine.json'};{work / ('tower.json' if tower else 'process.json')}",
                 "--load-filaments", str(filament), "--export-model-transforms",
                 "--arrange", "1", "--arrange-spacing", "2", "--allow-multicolor-oneplate"]
        if project:
            flags += ["--export-3mf", "layout.3mf"]
        if mode:
            flags += ["--arrange-by-source-group", "1"]
        if sliced:
            flags += ["--slice", "0"]
        if not tower:
            flags += ["--ignore-wipe-tower"]
        for name, group in components:
            flags += ["--model", name]
            if group is not None:
                flags += ["--model-source-group", str(group)]
            flags += (model_opts or {}).get(name, [])
        cmd = [str(binary), "--datadir", str(work / "data"), "--outputdir", str(out), "--debug", "4",
               *flags, *extra, *(str(work / name) for name, _ in components)]
        save_json(out / "command.json", cmd)
        with (out / "run.log").open("wb") as log:
            status = subprocess.run(cmd, cwd=out, stdout=log, stderr=subprocess.STDOUT, timeout=120).returncode
        log = (out / "run.log").read_text(errors="replace")
        if error:
            assert status > 0 and error in log and not (out / "layout.3mf").exists(), (label, status, log[-1200:])
            return
        assert status == 0, (label, status, log[-1200:])
        result = json.loads((out / "result.json").read_text())
        assert result["return_code"] == 0
        rows = result["model_transforms"]
        assert len({r["instance_id"] for r in rows}) == len(components) == len(rows)
        assert {r["object_name"] for r in rows} == {name for name, _ in components}
        if not project:
            return rows, None
        actual = project_state(out / "layout.3mf")
        # A JSON-only overflow plate cannot appear in real printable membership.
        for idx, plate in enumerate(actual["members"]):
            assert sorted(plate) == sorted(row["object_name"] for row in rows if row["plate_index"] == idx), (label, rows, actual["members"])
        if sliced:
            with zipfile.ZipFile(out / "layout.3mf") as archive:
                gcodes = [name for name in archive.namelist() if name.endswith(".gcode")]
                assert len(gcodes) == len(actual["members"]) and all(b"G1 " in archive.read(name) for name in gcodes)
        if label == "ten-all-fit":
            assert re.findall(r"begin movable=([1-9][0-9]*)", log) == ["10"]
        if tower:
            assert "reserve wipe tower on logical bed" in log and "per-bed wipe tower pass" in log
        print(f"PASS {label}: {actual['members']}", flush=True)
        return rows, actual

    def box(name, group, size):
        filename = name + ".stl"
        write_box(work / filename, *size, centered=True)
        return filename, group

    def members(layout):
        return [set(plate) for plate in layout["members"]]

    # Two components per source, interleaved. Non-monotonic ids catch accidental map ordering.
    large, small = (77, 40, 2), (12, 12, 2)
    groups = [[box(f"g{index}-p{part}", gid, size) for part in (1, 2)]
              for index, (gid, size) in enumerate(zip((8, 2, 9, 1, 7), (large, large, small, large, small)), 1)]
    mixed = [g[p] for p in range(2) for g in groups]
    _, state = run("skip-and-resume", mixed, sliced=True)
    names = [{name for name, _ in g} for g in groups]
    assert members(state) == [names[0] | names[2] | names[4], names[1], names[3]]

    # One small part of source 2 would fit on plate 1. Its large sibling
    # cannot, so both stay together for the next anchor.
    partial = [box("anchor", 3, (76, 76, 2)), box("rejected-small", 2, small),
               box("rejected-large", 2, (70, 70, 2)), box("admitted", 8, small)]
    _, state = run("no-partial-merge", partial)
    assert members(state) == [{partial[0][0], partial[3][0]}, {partial[1][0], partial[2][0]}]

    split = [box(f"split-{i}", 7, (72, 72, 2)) for i in range(3)]
    extra_part = box("other", 4, small)
    _, state = run("exclusive", [*split, extra_part])
    actual = members(state)
    assert len(actual) == 4 and actual[-1] == {extra_part[0]} and all(len(plate) == 1 for plate in actual)

    oversized = box("oversized", 7, (160, 140, 2))
    first = box("same-source", 7, small)
    opts = {oversized[0]: ["--model-rotate", "0,0,37"]}
    rows, state = run("overflow", [first, oversized, extra_part], model_opts=opts)
    assert members(state) == [{first[0]}, {extra_part[0]}]
    row = next(r for r in rows if r["object_name"] == oversized[0])
    assert row["plate_index"] == 2 and row["position"] == [50, 50, 0] and abs(row["rotation"][2] - 37) < 1e-4

    tall = box("too-tall", 7, (12, 12, 110))
    rows, state = run("height-overflow", [first, tall, extra_part])
    assert members(state) == [{first[0]}, {extra_part[0]}]
    row = next(r for r in rows if r["object_name"] == tall[0])
    assert row["plate_index"] == len(state["members"]) and row["position"] == [50, 50, 0]

    # Only the stand-alone preview interleaves overflow. Inserting display plates
    # must not change placement, including XY on a physical bed beyond the first.
    leading = box("leading-overflow", 8, (170, 120, 2))
    anchor = box("first-bed", 1, (70, 70, 2))
    follower = box("later-on-first", 2, small)
    last = box("second-bed", 3, (75, 75, 2))
    layout_inputs = [leading, anchor, oversized, tall, follower, last]
    ordinary_inputs = [leading, oversized, box("second-overflow", 7, (130, 150, 2)), anchor, follower, last]
    for grouped in (True, False):
        case = "preview-order-" + ("grouped" if grouped else "ordinary")
        components = layout_inputs if grouped else [(name, None) for name, _ in ordinary_inputs]
        reference, state = run(case + "-project", components, mode=grouped, model_opts=opts)
        preview, _ = run(case, components, mode=grouped, model_opts=opts, project=False)
        by_name = {r["object_name"]: r for r in reference}
        indices = {r["object_name"]: r["plate_index"] for r in preview}
        assert len(set(indices.values())) == len(state["members"]) + 3
        if grouped:
            assert [indices[name] for name, _ in components] == [0, 1, 2, 3, 1, 4]
        else:
            assert [indices[name] for name, _ in components[:3]] == [0, 1, 2]
        for plate in state["members"]:
            assert len({indices[name] for name in plate}) == 1
        for r in preview:
            assert all(r[k] == by_name[r["object_name"]][k] for k in ("position", "rotation", "scale"))

    rows, _ = run("preview-only-overflow", [leading, oversized], model_opts=opts, project=False)
    assert {r["object_name"]: r["plate_index"] for r in rows} == {leading[0]: 0, oversized[0]: 1}

    tiny = [box(f"tiny-{i}", 20 - i, (5, 5, 1)) for i in range(10)]
    rows, state = run("ten-all-fit", tiny)
    legacy, control = run("ordinary", [(name, None) for name, _ in tiny], mode=False)
    fields = ("plate_index", "position", "rotation", "scale")
    assert len(state["members"]) == 1 and members(state) == members(control)
    assert [[row[k] for k in fields] for row in rows] == [[row[k] for k in fields] for row in legacy]

    # Mix an OBJ and an STL with distinct filament assignments in a single source.
    mesh = work / "eye.obj"
    mesh.write_text("v 0 0 0\nv 6 0 0\nv 0 6 0\nv 0 0 6\nf 1 3 2\nf 1 2 4\nf 2 3 4\nf 3 1 4\n")
    colored = [tiny[0], (mesh.name, tiny[0][1])]
    opts = {tiny[0][0]: ["--model-filaments", str(filament)], mesh.name: ["--model-filaments", str(work / "second.json")]}
    _, state = run("two-materials", colored, model_opts=opts)
    assert members(state) == [{tiny[0][0], mesh.name}]

    def slots(label):
        with zipfile.ZipFile(work / label / "layout.3mf") as archive:
            objects = ET.fromstring(archive.read("Metadata/model_settings.config")).findall("object")
            return {
                obj.find("metadata[@key='name']").attrib["value"]:
                    [m.attrib["value"] for m in obj.iter("metadata") if m.get("key") == "extruder"]
                for obj in objects
            }

    run("ordinary-materials", [(name, None) for name, _ in colored], mode=False, model_opts=opts)
    assigned = slots("two-materials")
    assert assigned == slots("ordinary-materials")
    assert {name: set(values) for name, values in assigned.items()} == {tiny[0][0]: {"1"}, mesh.name: {"2"}}
    tower_parts = [box(f"tower-part-{i}", 7, (53, 53, 2)) for i in range(2)]
    _, state = run("tower-pass", [*tower_parts, extra_part], tower=True)
    assert members(state)[-1] == {extra_part[0]} and len(members(state)) >= 3

    verify_color_sources(work, run, box)

    for label, components, mode, extra, error in (
        ("no-switch", tiny[:1], False, (), "--model-source-group requires"),
        ("missing-id", [tiny[0], (tiny[1][0], None)], True, (), "Each STL or OBJ input"),
        ("group-limit", [*tiny, box("eleventh", 101, small)], True, (), "10 distinct source groups"),
        ("invalid-id", [(tiny[0][0], "0")], True, (), "positive integer"),
        ("color-conflict", tiny[:1], True, ("--arrange-by-color",), "conflicts with"),
        ("manual-conflict", tiny[:1], True, ("--model-arrange-group", "a"), "conflicts with"),
        ("split-conflict", tiny[:1], True, ("--split-by-color",), "conflicts with"),
        ("single-plate", tiny[:1], True, ("--auto-plate", "0"), "global multi-plate"),
    ):
        run(label, components, mode=mode, extra=extra, error=error)
    # The last source cannot reuse a plate owned by the first one, even
    # when the total would exceed the supported number of physical plates.
    limit = [box(f"limit-{i}", 1 if i < 35 else 2, (76, 76, 1)) for i in range(37)]
    run("physical-limit", limit, error="physical plate limit")
    print("PASS invalid flags and plate limit", flush=True)


if __name__ == "__main__":
    main()
