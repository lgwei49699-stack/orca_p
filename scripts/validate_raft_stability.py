#!/usr/bin/env python3
"""Measure an already built slicer with deterministic, independent-object rafts.

Never builds or sends a print. Each run uses a fresh process and output directory;
this does not measure same-process cache reuse or user cancellation. Input profiles
must be resolved JSON (no inherits). Synthetic fixture overrides are recorded.
"""

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
import zipfile


REPO = Path(__file__).resolve().parents[1]
NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
NUMBER = re.compile(r"([XYZE])\s*([-+]?(?:(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?|nan|inf))", re.IGNORECASE)


def sha256(path):
    with Path(path).open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def save_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def box(x, y, z):
    vertices = [(a, b, c) for c in (0, z) for b in (0, y) for a in (0, x)]
    faces = [(0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6),
             (0, 1, 4), (1, 5, 4), (2, 6, 3), (3, 6, 7),
             (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5)]
    return vertices, faces


def ring():
    # Rectangular annulus: a large hole and many directions, without overlapping shells.
    loop = [(0, 0), (60, 0), (60, 60), (0, 60), (12, 12), (48, 12), (48, 48), (12, 48)]
    vertices = [(x, y, z) for z in (0, 2) for x, y in loop]
    faces = []
    for i in range(4):
        j = (i + 1) % 4
        for quad in [(i, j, j + 8, i + 8), (i + 4, i + 12, j + 12, j + 4),
                     (i + 8, j + 8, j + 12, i + 12), (i, i + 4, j + 4, j)]:
            a, b, c, d = quad
            faces.extend(((a, b, c), (a, c, d)))
    return vertices, faces


def fixture(path, count, shape, bed):
    """Core 3MF, one mesh object and one instance per item, fixed positions."""
    ET.register_namespace("", NS)
    model = ET.Element(f"{{{NS}}}model", unit="millimeter")
    resources = ET.SubElement(model, f"{{{NS}}}resources")
    build = ET.SubElement(model, f"{{{NS}}}build")
    side = 60 if shape in ("flat", "ring") else 8
    mesh_vertices, faces = ring() if shape == "ring" else box(side, side, 2 if count == 1 else 4)
    if shape == "support":
        mesh_vertices, faces = box(20, 20, 16)
        angle = math.radians(25)
        mesh_vertices = [(x, y * math.cos(angle) - z * math.sin(angle),
                          y * math.sin(angle) + z * math.cos(angle)) for x, y, z in mesh_vertices]
    xmin, ymin = (min(v[i] for v in mesh_vertices) for i in (0, 1))
    xmax, ymax = (max(v[i] for v in mesh_vertices) for i in (0, 1))
    columns = math.ceil(math.sqrt(count))
    rows = math.ceil(count / columns)
    pitch = 11.0  # 8 mm legs, 3 mm model gap; raft margins overlap.
    positions = []
    for i in range(count):
        name = f"raft-fixture-{i + 1}"
        obj = ET.SubElement(resources, f"{{{NS}}}object", id=str(i + 1), type="model", name=name)
        mesh = ET.SubElement(obj, f"{{{NS}}}mesh")
        vertices = ET.SubElement(mesh, f"{{{NS}}}vertices")
        for x, y, z in mesh_vertices:
            ET.SubElement(vertices, f"{{{NS}}}vertex", x=str(x), y=str(y), z=str(z))
        triangles = ET.SubElement(mesh, f"{{{NS}}}triangles")
        for a, b, c in faces:
            ET.SubElement(triangles, f"{{{NS}}}triangle", v1=str(a), v2=str(b), v3=str(c))
        x = bed / 2 - (xmax + xmin) / 2 + (i % columns - (columns - 1) / 2) * pitch
        y = bed / 2 - (ymax + ymin) / 2 + (i // columns - (rows - 1) / 2) * pitch
        positions.append([x, y, 0])
        ET.SubElement(build, f"{{{NS}}}item", objectid=str(i + 1), transform=f"1 0 0 0 1 0 0 0 1 {x} {y} 0")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        entries = {
            "3D/3dmodel.model": ET.tostring(model, encoding="utf-8", xml_declaration=True),
            "[Content_Types].xml": b'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>',
            "_rels/.rels": b'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>',
        }
        for name, content in entries.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            archive.writestr(info, content, compress_type=zipfile.ZIP_DEFLATED)
    return positions


def distance_to_segment(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy))) if dx or dy else 0
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def distance_to_rectangle(a, b, rect):
    # Segment/rectangle intersection, then the closest endpoint/edge pair.
    low, high = 0., 1.
    for axis in (0, 1):
        delta = b[axis] - a[axis]
        if delta == 0:
            if not rect[axis] <= a[axis] <= rect[axis + 2]:
                low, high = 1., 0.
                break
        else:
            ts = sorted(((rect[axis] - a[axis]) / delta, (rect[axis + 2] - a[axis]) / delta))
            low, high = max(low, ts[0]), min(high, ts[1])
    if low <= high:
        return 0.
    corners = [(x, y) for x in (rect[0], rect[2]) for y in (rect[1], rect[3])]
    endpoint_distances = [math.hypot(max(rect[0] - p[0], 0, p[0] - rect[2]),
                                     max(rect[1] - p[1], 0, p[1] - rect[3])) for p in (a, b)]
    return min(endpoint_distances + [distance_to_segment(c, a, b) for c in corners])


def audit_gcode(path, bed, exclusions):
    """Straight XY extrusions, including half-width; custom G-code is excluded.

    Arc fitting is explicitly disabled in the fixture; an unexpected extruding
    arc is an error, never silently skipped. Exact duplicates do not prove absence
    of partial overlaps; shared ownership is also covered by the C++ raft tests.
    """
    with path.open(errors="strict") as stream:
        return audit_gcode_stream(stream, path, bed, exclusions)


def audit_gcode_stream(stream, path, bed, exclusions):
    position = dict(X=0., Y=0., Z=0., E=0.)
    absolute_xyz, absolute_e = True, True
    role, width, layer = "Custom", None, -1
    counts, segments, config = Counter(), Counter(), {}
    deposited = []
    outside, forbidden, duplicates = [], [], 0
    for lineno, line in enumerate(stream, 1):
        line = line.strip()
        if line == ";LAYER_CHANGE":
            layer += 1
        elif line.startswith(";TYPE:"):
            role = line[6:]
        elif line.startswith(";WIDTH:"):
            width = float(line[7:])
        elif line.startswith("; ") and " = " in line:
            key, value = line[2:].split(" = ", 1)
            config[key] = value
        code = line.split(";", 1)[0].strip()
        if not code:
            continue
        cmd = code.split()[0]
        args = {k.upper(): float(v) for k, v in NUMBER.findall(code)}
        if not all(math.isfinite(v) for v in args.values()):
            raise ValueError(f"non-finite coordinate at {path}:{lineno}")
        if cmd in ("G90", "G91"):
            absolute_xyz = cmd == "G90"
        elif cmd in ("M82", "M83"):
            absolute_e = cmd == "M82"
        elif cmd == "G92":
            position.update(args)
        elif cmd == "G28":
            if layer >= 0 and role != "Custom":
                raise ValueError("Unexpected homing during generated paths")
            position.update(X=0., Y=0., Z=0.)
        elif cmd in ("G0", "G1", "G2", "G3"):
            end = dict(position)
            for key, value in args.items():
                end[key] = value if (absolute_e if key == "E" else absolute_xyz) else position[key] + value
            a, b = (position["X"], position["Y"]), (end["X"], end["Y"])
            if layer >= 0 and role != "Custom" and end["E"] > position["E"] + 1e-9 and (a != b or cmd in ("G2", "G3")):
                if cmd not in ("G0", "G1") or width is None or not math.isfinite(width) or width <= 0:
                    raise ValueError(f"unsupported extrusion at {path}:{lineno}")
                counts[role] += 1
                deposited.append((role, tuple(round(position[k], 6) for k in ("X", "Y", "Z")),
                                  tuple(round(end[k], 6) for k in ("X", "Y", "Z")), round(width, 6),
                                  round(end["E"] - position["E"], 6)))
                radius = width / 2
                if any(min(a[i], b[i]) - radius < bed[i] - .003 or max(a[i], b[i]) + radius > bed[i + 2] + .003 for i in (0, 1)):
                    outside.append(lineno)
                if any(distance_to_rectangle(a, b, rect) < radius - .003 for rect in exclusions):
                    forbidden.append(lineno)
                if role in ("Support", "Support interface") and end["Z"] <= 1.3 + 1e-5:
                    key = (round(end["Z"], 5), tuple(sorted((a, b))))
                    duplicates += segments[key] > 0
                    segments[key] += 1
            position = end
    return {"roles": dict(counts), "extrusion_segments": sum(counts.values()), "layers": layer + 1,
            "outside_segments": len(outside), "excluded_segments": len(forbidden),
            "first_invalid_lines": (outside + forbidden)[:10], "exact_raft_duplicate_segments": duplicates,
            "deposited_paths_sha256": hashlib.sha256(repr(sorted(deposited)).encode()).hexdigest(),
            "effective_config": config}


def run_process(command, directory, timeout):
    started = time.monotonic()
    timed_out = False
    with (directory / "process.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while True:
                pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    break
                if time.monotonic() - started > timeout:
                    timed_out = True
                    os.killpg(process.pid, signal.SIGKILL)
                    _, status, usage = os.wait4(process.pid, 0)
                    break
                time.sleep(.005)
            process.returncode = os.waitstatus_to_exitcode(status)
        finally:
            if process.returncode is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
    return {"exit_code": process.returncode, "timed_out": timed_out,
            "elapsed_s": time.monotonic() - started,
            "peak_rss_bytes": usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024)}


def read_profile(path):
    data = json.loads(path.read_text())
    if data.get("inherits"):
        raise ValueError(f"Profile must be resolved: {path}")
    return data


def rectangle(points):
    points = [tuple(map(float, p.split("x"))) for p in points]
    rect = [min(p[0] for p in points), min(p[1] for p in points), max(p[0] for p in points), max(p[1] for p in points)]
    if set(points) != {(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3])}:
        raise ValueError("This fixture audit only supports rectangular beds/exclusions")
    return rect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--profiles", type=Path, default=REPO / "planning/cache/cli-assets/profiles")
    parser.add_argument("--output", type=Path, help="New evidence directory, must not exist")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--case", action="append", help="Run only a named case; repeat for several")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    cases = []
    for machine, shape in (("ep3", "flat"), ("ep7", "ring")):
        for mode in ("off", "segment", "smooth", "both"):
            cases.append((f"{machine}-{shape}-{mode}", machine, 1, shape, mode, "none"))
    for machine, count in (("ep3", 10), ("ep3", 25), ("ep7", 25), ("ep7", 50)):
        for mode in ("off", "both"):
            cases.append((f"{machine}-{count}-{mode}", machine, count, "legs", mode, "none"))
    for machine in ("ep3", "ep7"):
        for support in ("grid", "organic", "tree_hybrid"):
            cases.append((f"{machine}-{support}-off", machine, 1, "support", "off", support))
            cases.append((f"{machine}-{support}", machine, 1, "support", "both", support))
    if args.list:
        print("\n".join(c[0] for c in cases))
        return 0
    if args.repeats < 1 or args.timeout <= 0:
        parser.error("repeats and timeout must be positive")
    if args.case:
        unknown = set(args.case) - {c[0] for c in cases}
        if unknown:
            parser.error(f"Unknown cases: {sorted(unknown)}")
        cases = [c for c in cases if c[0] in args.case]
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="orca-raft-stability-"))
    if args.output:
        output.mkdir(parents=True, exist_ok=False)
    identity = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
                "platform": platform.platform(), "binary": str(binary), "binary_sha256": sha256(binary),
                "source_profiles": {}, "script_sha256": sha256(__file__), "poll_interval_s": .005,
                "scope": "fresh-process synthetic slicing; no GUI, printing, cache or cancellation proof"}
    summary = []
    print(f"Evidence: {output}", flush=True)
    for label, printer, count, shape, mode, support in cases:
        case = output / label
        case.mkdir()
        stem = "ep3_plus" if printer == "ep3" else "ep7"
        paths = [args.profiles / f"machine/{stem}_0.4.json", args.profiles / f"process/{stem}_standard_0.20.json",
                 args.profiles / "filament/generic_pla_0.4.json"]
        machine, process, filament = [read_profile(p) for p in paths]
        identity["source_profiles"].update({str(p.resolve()): sha256(p) for p in paths})
        bed = rectangle(machine["printable_area"])
        exclude = machine.get("bed_exclude_area", [])
        exclusions = [rectangle(exclude)] if exclude and len(set(exclude)) > 1 else []
        overrides = {"raft_mode": "cura_v1", "raft_base_layers": "1", "raft_interface_layers": "2", "raft_surface_layers": "2",
                     "raft_base_layer_height": "0.3", "raft_interface_layer_height": "0.3", "raft_surface_layer_height": "0.2",
                     "raft_base_margin": "3", "raft_interface_margin": "2", "raft_surface_margin": "1.5",
                     "raft_segmented": "1" if mode in ("segment", "both") else "0", "raft_chunk_size": "20",
                     "raft_chunk_gap": "0.8", "raft_smoothing": "1" if mode in ("smooth", "both") else "0",
                     "enable_support": "0" if support == "none" else "1", "support_style": "grid" if support == "none" else support,
                     "support_type": "normal(auto)" if support in ("none", "grid") else "tree(auto)",
                     "brim_type": "no_brim", "skirt_loops": "0", "enable_arc_fitting": "0"}
        process.update(overrides)
        for filename, data in (("machine.json", machine), ("process.json", process), ("filament.json", filament), ("overrides.json", overrides)):
            save_json(case / filename, data)
        positions = fixture(case / "input.3mf", count, shape, bed[2])
        save_json(case / "input-sha256.json", {p.name: sha256(p) for p in case.iterdir() if p.suffix in (".json", ".3mf")})
        save_json(case / "fixture.json", {"objects": count, "instances_per_object": 1, "positions": positions,
                                         "shape": shape, "bed": bed, "exclusions": exclusions})
        runs = []
        for repeat in range(args.repeats):
            run = case / f"run-{repeat + 1}"
            run.mkdir()
            command = [str(binary), "--slice", "0", "--debug", "3", "--arrange", "0", "--auto-plate", "0",
                       "--ensure-on-bed", "--export-model-transforms", "--load-settings", str(case / "machine.json"),
                       "--load-settings", str(case / "process.json"), "--load-filaments", str(case / "filament.json"),
                       "--outputdir", str(run), str(case / "input.3mf")]
            save_json(run / "command.json", command)
            execution = run_process(command, run, args.timeout)
            failures = []
            try:
                result = json.loads((run / "result.json").read_text())
                if execution["exit_code"] or result.get("return_code") != 0 or len(result.get("sliced_plates", [])) != 1:
                    failures.append("CLI failed or expected plate missing")
                transforms = result.get("model_transforms", [])
                if len(transforms) != count or len({t.get("object_name") for t in transforms}) != count:
                    failures.append("object/instance count changed")
                if any(t.get("plate_index") != 0 for t in transforms):
                    failures.append("object left the expected plate")
                if any(p.get("warning_message") for p in result.get("sliced_plates", [])):
                    failures.append("slice warning")
                audit = audit_gcode(run / "plate_1.gcode", bed, exclusions)
                save_json(run / "gcode-audit.json", audit)
                effective = audit["effective_config"]
                for key in overrides:
                    if effective.get(key) != overrides[key]:
                        failures.append(f"effective {key}: {effective.get(key)!r} != {overrides[key]!r}")
                if not audit["roles"].get("Outer wall") or audit["outside_segments"] or audit["excluded_segments"]:
                    failures.append("empty or out-of-bed deposited paths")
                if support == "none" and audit["exact_raft_duplicate_segments"]:
                    failures.append("duplicate raft segments")
                execution.update(gcode_bytes=(run / "plate_1.gcode").stat().st_size,
                                 gcode_sha256=sha256(run / "plate_1.gcode"),
                                 deposited_paths_sha256=audit["deposited_paths_sha256"],
                                 extrusion_segments=audit["extrusion_segments"], layers=audit["layers"])
            except (OSError, ValueError, KeyError) as error:
                failures.append(str(error))
            log = (run / "process.log").read_text(errors="replace")
            events = [line.split("[Raft]", 1)[1] for line in log.splitlines() if "[Raft]" in line]
            execution["raft_events"] = events
            execution["stage_ms"] = {}
            for key in ("planning_ms", "support_mask_ms", "paths_validation_ms"):
                values = [float(v) for v in re.findall(rf"\b{key}=([\d.eE+-]+)", log)]
                if any(not math.isfinite(v) or v < 0 for v in values):
                    failures.append(f"invalid stage timing: {key}")
                execution["stage_ms"][key] = sum(values) if values else None
            findings = []
            if mode != "off" and not any("status=applied" in e for e in events):
                if any("status=unchanged, reason=" in e for e in events):
                    findings.append("enhancement fell back; compare with the off control and review reason")
                else:
                    failures.append("requested enhancement has no applied or fallback record")
            execution["failures"] = failures
            execution["findings"] = findings
            save_json(run / "execution.json", execution)
            runs.append(execution)
            print(f"{label} {repeat + 1}/{args.repeats}: {execution['elapsed_s']:.2f}s, {execution['peak_rss_bytes'] / 2**20:.1f} MiB, failures={failures}, findings={findings}", flush=True)
        summary.append({"case": label, "runs": runs, "median_s": statistics.median(r["elapsed_s"] for r in runs),
                        "range_s": [min(r["elapsed_s"] for r in runs), max(r["elapsed_s"] for r in runs)]})
        save_json(output / "summary.json", summary)
        save_json(output / "identity.json", identity)
    if sha256(binary) != identity["binary_sha256"]:
        raise RuntimeError("Binary changed during validation")
    controls = {case["case"]: case for case in summary}
    comparisons = []
    for case in summary:
        if any(r["findings"] for r in case["runs"]):
            control = controls.get(case["case"] + "-off")
            fingerprints = {r.get("deposited_paths_sha256") for r in case["runs"]}
            control_fingerprints = {r.get("deposited_paths_sha256") for r in control["runs"]} if control else set()
            comparisons.append({"case": case["case"], "control_available": control is not None,
                                "all_generated_extrusions_equal_control": len(fingerprints) == 1 and None not in fingerprints and fingerprints == control_fingerprints,
                                "comparison": "sorted role, directed endpoints/physical Z, width and extrusion delta rounded to 6 decimals; not full G-code identity"})
    save_json(output / "fallback-comparisons.json", comparisons)
    if any(r["failures"] for c in summary for r in c["runs"]):
        return 1
    # An intentional conservative fallback is distinct from corrupted/empty
    # output, but is not counted as successful enhancement either.
    return 2 if any(r["findings"] for c in summary for r in c["runs"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
