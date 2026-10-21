#!/usr/bin/env python3
"""Local CLI feature comparisons with deterministic closed synthetic models.

Requires an already built Orca and local profiles. Does not build, download,
change input presets, connect to a service or send commands to a printer.
Runs 40 slices, including Classic/Arachne, normal/tree support, raft, multi-volume geometry,
wide/narrow U slots, a C-shaped opening, an island inside a hole and a closed 45-degree wedge.
Validates retained two-dimensional planned-path evidence and native G-code estimates,
not physical defects, fused-material thickness, automatic print-quality decisions
or a preferred wall generator. Retired three-dimensional diagnostics must not be emitted.
"""

import argparse
from collections import Counter
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

from test_cli_slice_analysis import (
    ASSETS, ROOT, Suite, count, digest, gcode_facts, nonnegative, read_json, require, save_json,
    validate_core_report_group, validate_plate_bed_footprint, validate_report,
)

SUPPORT_FIXTURES = {"bridge", "cantilever", "new-island"}
SUPPORT_GCODE_ROLES = {"Support", "Support interface", "Support transition"}
MODEL_NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
OPEN_SLOT_AREAS = {"u-slot-wide": 86., "u-slot-narrow": 94.2, "c-slot": 106.5}


def cell_mesh(xs, ys, zs, cells):
    """Boundary of occupied shared-grid cells, without internal triangles."""
    triangles = []
    for i, j, k in sorted(cells):
        x0, x1 = xs[i:i + 2]
        y0, y1 = ys[j:j + 2]
        z0, z1 = zs[k:k + 2]
        faces = (
            ((-1, 0, 0), ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))),
            ((1, 0, 0), ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1))),
            ((0, -1, 0), ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))),
            ((0, 1, 0), ((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0))),
            ((0, 0, -1), ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0))),
            ((0, 0, 1), ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))),
        )
        for (di, dj, dk), quad in faces:
            if (i + di, j + dj, k + dk) not in cells:
                triangles.extend(((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])))
    edges = Counter((a, b) for tri in triangles for a, b in zip(tri, tri[1:] + tri[:1]))
    require(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()),
            "Fixture must be closed with oppositely oriented shared edges")
    return triangles


def wedge_mesh():
    """Closed 4 x 6 x 4 mm wedge: 0 <= Z <= X, with a 45-degree source plane."""
    a, b, c = (0, 0, 0), (4, 0, 0), (4, 0, 4)
    d, e, f = (0, 6, 0), (4, 6, 0), (4, 6, 4)
    return [(a, b, c), (d, f, e), (a, d, e), (a, e, b),
            (b, e, f), (b, f, c), (a, c, f), (a, f, d)]


def write_stl(path, triangles):
    lines = ["solid slice_feature_fixture"]
    for a, b, c in triangles:
        u, v = (tuple(p[d] - a[d] for d in range(3)) for p in (b, c))
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(n * n for n in normal))
        require(length > 0, "Degenerate fixture triangle")
        lines.extend(("facet normal " + " ".join(str(n / length) for n in normal), "outer loop"))
        lines.extend("vertex " + " ".join(map(str, p)) for p in (a, b, c))
        lines.extend(("endloop", "endfacet"))
    lines.append("endsolid slice_feature_fixture")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_volume_3mf(path, volumes):
    """One standard 3MF build item whose mesh components become model-part volumes."""
    model = ET.Element("model", {"unit": "millimeter", "xmlns": MODEL_NS})
    resources = ET.SubElement(model, "resources")
    for index, (name, triangles) in enumerate(volumes, 1):
        obj = ET.SubElement(resources, "object", {"id": str(index), "type": "model", "name": name})
        mesh = ET.SubElement(obj, "mesh")
        vertices = ET.SubElement(mesh, "vertices")
        faces = ET.SubElement(mesh, "triangles")
        vertex_ids = {}
        for triangle in triangles:
            ids = []
            for vertex in triangle:
                if vertex not in vertex_ids:
                    vertex_ids[vertex] = len(vertex_ids)
                    ET.SubElement(vertices, "vertex", dict(zip("xyz", map(str, vertex))))
                ids.append(vertex_ids[vertex])
            ET.SubElement(faces, "triangle", {"v" + str(i + 1): str(value) for i, value in enumerate(ids)})
    assembly_id = str(len(volumes) + 1)
    assembly = ET.SubElement(resources, "object", {"id": assembly_id, "type": "model", "name": "source-volume-sampling"})
    components = ET.SubElement(assembly, "components")
    for index in range(1, len(volumes) + 1):
        ET.SubElement(components, "component", {"objectid": str(index)})
    ET.SubElement(ET.SubElement(model, "build"), "item", {"objectid": assembly_id})
    content_types = ('<?xml version="1.0" encoding="UTF-8"?>'
                     '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                     '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                     '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
                     '</Types>')
    relationships = ('<?xml version="1.0" encoding="UTF-8"?>'
                     '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                     '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
                     'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>'
                     '</Relationships>')
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        # Fixed timestamps keep fixture identity stable across runs.
        for name, data in (("[Content_Types].xml", content_types), ("_rels/.rels", relationships),
                           ("3D/3dmodel.model", ET.tostring(model, encoding="utf-8", xml_declaration=True))):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)


def fixtures(output):
    specs = {
        "raised-stroke": ([0, 3, 9, 12], [0, 3.82, 4.18, 8], [0, 1.2, 2.4],
                          {(i, j, 0) for i in range(3) for j in range(3)} | {(1, 1, 1)}),
        "through-hole": ([0, 4, 8, 12], [0, 4, 8, 12], [0, 2.4],
                         {(i, j, 0) for i in range(3) for j in range(3) if (i, j) != (1, 1)}),
        "separated-blocks": ([0, 4, 4.5, 8.5], [0, 8], [0, 2.4], {(0, 0, 0), (2, 0, 0)}),
        "nested-island": ([0, 3, 4, 6, 7, 10], [0, 3, 4, 6, 7, 10], [0, 2.4],
                          {(i, j, 0) for i in range(5) for j in range(5) if i in (0, 4) or j in (0, 4)}
                          | {(2, 2, 0)}),
        "sparse-block": ([0, 12], [0, 8], [0, 4], {(0, 0, 0)}),
        "sparse-top": ([0, 12], [0, 8], [0, 4], {(0, 0, 0)}),
        "bridge": ([0, 4, 12, 16], [0, 6], [0, 4, 5.2],
                   {(0, 0, 0), (2, 0, 0)} | {(i, 0, 1) for i in range(3)}),
        "cantilever": ([0, 4, 12], [0, 6], [0, 4, 5.2],
                       {(0, 0, 0), (0, 0, 1), (1, 0, 1)}),
        # The full-height left pillar fixes the object's bed and keeps every model
        # layer present; the separate right block first appears above Z=4 mm.
        "new-island": ([0, 4, 8, 12], [0, 6], [0, 4, 5.2],
                       {(0, 0, 0), (0, 0, 1), (2, 0, 1)}),
        "new-island-tree": ([0, 4, 8, 12], [0, 6], [0, 4, 5.2],
                            {(0, 0, 0), (0, 0, 1), (2, 0, 1)}),
        "cura-raft": ([0, 12], [0, 8], [0, 4], {(0, 0, 0)}),
        "narrow-neck": ([0, 4, 8, 12], [0, 1.82, 2.18, 4], [0, 2.4],
                        {(i, j, 0) for i in (0, 2) for j in range(3)} | {(1, 1, 0)}),
        "u-slot-wide": ([0, 5, 7, 12], [0, 3, 8], [0, 2.4],
                        {(i, 0, 0) for i in range(3)} | {(0, 1, 0), (2, 1, 0)}),
        "u-slot-narrow": ([0, 5.82, 6.18, 12], [0, 3, 8], [0, 2.4],
                          {(i, 0, 0) for i in range(3)} | {(0, 1, 0), (2, 1, 0)}),
        "c-slot": ([0, 3, 9, 12], [0, 3, 5.75, 6.25, 9, 12], [0, 2.4],
                   {(i, j, 0) for i in range(3) for j in range(5)}
                   - {(1, j, 0) for j in (1, 2, 3)} - {(2, 2, 0)}),
    }
    result = {}
    for name, spec in specs.items():
        path = output / (name + ".stl")
        write_stl(path, cell_mesh(*spec))
        result[name] = path
    # Separate source volumes, not separate shells merged into one STL. The
    # main body holds Z=0 and every model layer even when a thin part is missed.
    sampling = output / "source-volume-sampling.3mf"
    write_volume_3mf(sampling, [
        ("body", cell_mesh([0, 8], [0, 8], [0, 4], {(0, 0, 0)})),
        ("missed-sheet", cell_mesh([10, 14], [0, 4], [1.21, 1.26], {(0, 0, 0)})),
        ("sampled-sheet", cell_mesh([16, 20], [0, 4], [1.28, 1.32], {(0, 0, 0)})),
    ])
    result["source-volume-sampling"] = sampling
    wedge = output / "slope-wedge.stl"
    write_stl(wedge, wedge_mesh())
    result["slope-wedge"] = wedge
    return result


def config_values(facts):
    return {line[2:].split(" = ", 1)[0]: line[2:].split(" = ", 1)[1]
            for line in facts["configuration"] if line.startswith("; ") and " = " in line}


def verify_fixture_bed_footprint(name, report, settings):
    """Known bed relationships for these fixtures, not a general adhesion test."""
    bed = report["planned_plate_bed_footprint"]
    validate_plate_bed_footprint(bed)
    require(bed["complete"] is True, f"{name}: fresh fixture must have complete plate bed observations")
    model, support = bed["model_area_mm2"], bed["support_and_raft_area_mm2"]
    require(bed["combined_area_mm2"] > 0, f"{name}: expected positive planned bed area")
    if name == "cura-raft":
        require(settings.get("raft_mode") == "cura_v1" and settings.get("raft_layers") == "3",
                "Known bed assertion requires the requested three-layer Cura raft")
        require(model == 0 and support > 0,
                "The lifted model has no nominal bed paths; the generated raft must have positive bed area")
    else:
        require(settings.get("raft_layers") == "0", "Known model-bed assertion requires no raft")
        require(model > 0, f"{name}: the fixture's on-bed body must have positive model path area")
        if settings.get("enable_support") == "1":
            require(support > 0, f"{name}: these build-plate-only support fixtures must reach the nominal bed")
        else:
            require(settings.get("enable_support") == "0" and support == 0,
                    f"{name}: disabled support without a raft must not add support bed area")
    # Auto brim is allowed to be empty or positive. Do not assume its area
    # from brim_width, or infer a sum when the three categories may overlap.


def verify_support_fixture(name, reports, support_enabled, gcode_roles):
    """Check known section geometry and actual generated support, not three-dimensional anchoring."""
    section_area = {"bridge": 96, "cantilever": 72, "new-island": 48, "new-island-tree": 48}[name]
    for generator, report in reports.items():
        group = report["objects"]["groups"][0]
        layers = group["planned_layer_coverage"]["layers"]
        transitions = [layer for layer in layers if math.isclose(layer["slice_z_mm"], 4.1, abs_tol=1e-6)]
        require(len(transitions) == 1, "Expected a model section at source Z=4.1 mm")
        require(math.isclose(transitions[0]["section_area_mm2"], section_area, abs_tol=0.001),
                f"{name}: unexpected transition section area")
        has_support = bool(gcode_roles[generator] & SUPPORT_GCODE_ROLES)
        support_volumes = [item["estimated_volume_mm3"]["support"] for item in report["gcode"]["extruders"]]
        if support_enabled:
            require(group["support_layer_count"] > 0 and has_support,
                    f"{name}: enabling support must generate actual G-code support paths")
            require(any(value is not None and nonnegative(value, "support volume") > 0 for value in support_volumes),
                    f"{name}: expected positive G-code support volume")
        else:
            require(group["support_layer_count"] == 0 and not has_support,
                    f"{name}: disabled support unexpectedly generated support")
            require(all(value is None or value == 0 for value in support_volumes),
                    f"{name}: disabled support unexpectedly reports positive volume")
        if name == "bridge" and not support_enabled:
            require("Bridge" in gcode_roles[generator],
                    "The two-pier bridge must reach final G-code bridge infill")


def verify_support_geometry_unchanged(off, on):
    keys = ("layer_index", "slice_z_mm", "print_z_mm", "bottom_z_mm", "height_mm",
            "section_area_mm2", "section_island_count", "hole_area_mm2", "closed_void_region_count")
    for generator in ("classic", "arachne"):
        before = off[generator]["objects"]["groups"][0]["planned_layer_coverage"]["layers"]
        after = on[generator]["objects"]["groups"][0]["planned_layer_coverage"]["layers"]
        require([{key: layer[key] for key in keys} for layer in before]
                == [{key: layer[key] for key in keys} for layer in after],
                "Support must not change the retained model section geometry")


def verify_cura_raft(report, gcode_roles):
    group = report["objects"]["groups"][0]
    layers = group["planned_layer_coverage"]["layers"]
    require(group["model_layer_count"] == len(layers) == 20, "Raft must not change the twenty model sampling planes")
    require(all(math.isclose(layer["slice_z_mm"], 0.1 + index * 0.2, abs_tol=1e-6)
                and math.isclose(layer["height_mm"], 0.2, abs_tol=1e-6) for index, layer in enumerate(layers)),
            "Unexpected model sampling planes or source-band heights above raft")
    require(layers[0]["bottom_z_mm"] > 0 and all(layer["complete"] is True for layer in layers),
            "Raft must lift the model without invalidating core section measurements")
    require(math.isclose(layers[1]["bottom_z_mm"] - layers[0]["print_z_mm"], -0.1, abs_tol=1e-6),
            "Cura V1's intentional 0.1 mm Z overlap must retain its existing layer coordinates")
    require(all(math.isclose(layer["bottom_z_mm"] - previous["print_z_mm"], 0, abs_tol=1e-6)
                for previous, layer in zip(layers[1:-1], layers[2:])),
            "Only the first-to-second model layer transition should contain the raft overlap")
    require(group["support_layer_count"] > 0
            and bool(gcode_roles & SUPPORT_GCODE_ROLES),
            "Cura raft must generate actual support layers and G-code paths")
    require(any(item["estimated_volume_mm3"]["support"] is not None
                and nonnegative(item["estimated_volume_mm3"]["support"], "raft support volume") > 0
                for item in report["gcode"]["extruders"]), "Cura raft must use positive support material volume")


def verify_wedge_alignment(group):
    """Known two-dimensional source sections, not source-facet or printed-surface error."""
    layers = group["planned_layer_coverage"]["layers"]
    require(group["model_layer_count"] == len(layers) == 20, "The wedge must retain twenty 0.2 mm model layers")
    for index, layer in enumerate(layers):
        z = 0.1 + index * 0.2
        require(math.isclose(layer["slice_z_mm"], z, abs_tol=1e-6), "Unexpected wedge source sampling plane")
        require(math.isclose(layer["section_area_mm2"], 6 * (4 - z), abs_tol=0.001),
                "The wedge section differs from its analytical uncompensated area")
        require(layer["section_island_count"] == 1 and layer["closed_void_region_count"] == 0,
                "Each wedge source section must be one material island without a hole")


def verify_topology_counts(layer):
    """Retain whole-island omission and full closed-hole coverage, not fragment diagnostics."""
    keys = ("section_island_count", "closed_void_region_count", "closed_void_fully_covered_count")
    require(type(layer["complete"]) is bool, "Coverage completeness must be a boolean")
    if not layer["complete"]:
        require(all(layer[key] is None for key in keys), "Incomplete coverage must preserve unknown core counts")
        return
    count(layer["section_island_count"], "section_island_count")
    voids = count(layer["closed_void_region_count"], "closed_void_region_count")
    require(count(layer["closed_void_fully_covered_count"], "closed_void_fully_covered_count") <= voids,
            "Fully covered holes must remain a subset of closed section holes")


def verify_island_gap(layer):
    """Distance between distinct section islands, not a single island's slot or neck width."""
    gap = layer["section_min_island_gap_mm"]
    require(type(layer["complete"]) is bool, "Coverage completeness must be a boolean")
    if not layer["complete"]:
        require(gap is None, "Incomplete coverage must preserve unknown island distance")
    elif count(layer["section_island_count"], "section_island_count") < 2:
        require(gap is None, "Fewer than two section islands have no inter-island distance")
    else:
        nonnegative(gap, "section_min_island_gap_mm")


def verify_island_fixture_layers(name, layers):
    gap, area, hole_area, voids = {"separated-blocks": (0.5, 64., 0., 0),
                                   "nested-island": (1., 88., 12., 1)}[name]
    require(len(layers) == 12, f"{name}: expected twelve model layers")
    for index, layer in enumerate(layers):
        require(math.isclose(layer["slice_z_mm"], 0.1 + index * 0.2, abs_tol=1e-6), f"{name}: unexpected sampling plane")
        require(layer["section_island_count"] == 2 and layer["closed_void_region_count"] == voids,
                f"{name}: expected the fixture's separate material islands and bounded voids")
        require(math.isclose(layer["section_area_mm2"], area, abs_tol=0.001)
                and math.isclose(layer["hole_area_mm2"], hole_area, abs_tol=0.001), f"{name}: source section geometry changed")
        require(math.isclose(layer["section_min_island_gap_mm"], gap, abs_tol=1e-6),
                f"{name}: unexpected nearest distance between complete section boundaries")


def verify_parallel_clearance(layers, name=None):
    """Strict parallel nearest boundary pairs, not general slot/neck thickness."""
    known = {"u-slot-wide": ("min_void_width_mm", 2.), "u-slot-narrow": ("min_void_width_mm", 0.36),
             "c-slot": ("min_void_width_mm", 0.5), "narrow-neck": ("min_material_width_mm", 0.36)}
    for layer in layers:
        value = layer["parallel_boundary_clearance"]
        require(value["scope"] == "same_section_island_parallel_voronoi_sites", "Unexpected parallel-boundary scope")
        require(type(value["complete"]) is bool, "Parallel-boundary completeness must be boolean")
        keys = ("min_material_width_mm", "min_void_width_mm")
        if name in known:
            require(value["complete"] is True, f"{name}: known parallel-boundary fixture must be fully observed")
        if not value["complete"]:
            require(all(value[key] is None for key in keys), "Incomplete parallel observation must remain unknown")
            continue
        require(layer["complete"] is True, "Invalid layer cannot provide complete parallel-boundary measurements")
        for key in keys:
            if value[key] is not None:
                require(nonnegative(value[key], key) > 0, "Applicable parallel width must be positive")
        if name in known:
            key, expected = known[name]
            require(value[key] is not None and math.isclose(value[key], expected, abs_tol=2e-6),
                    f"{name}: wrong known parallel-boundary width")


def verify_open_slot_layers(name, layers):
    require(len(layers) == 12, f"{name}: expected twelve model layers")
    for index, layer in enumerate(layers):
        require(math.isclose(layer["slice_z_mm"], 0.1 + index * 0.2, abs_tol=1e-6), f"{name}: unexpected sampling plane")
        require(math.isclose(layer["section_area_mm2"], OPEN_SLOT_AREAS[name], abs_tol=0.001),
                f"{name}: source section differs from the known uncompensated geometry")
        require(layer["section_island_count"] == 1 and layer["closed_void_region_count"] == 0 and layer["hole_area_mm2"] == 0,
                f"{name}: the source opening must not be reported as a closed polygon hole")
        # Source opening geometry alone is not a pass/fail judgment about a printed slot.


def verify_feature_metrics(name, reports, gcode_roles):
    by_generator = {}
    for generator, report in reports.items():
        groups = report["objects"]["groups"]
        require(len(groups) == 1 and groups[0]["instance_count"] == 1, "Expected one fixture instance")
        validate_core_report_group(groups[0])
        coverage = groups[0]["planned_layer_coverage"]
        require(coverage["scope"] == "per_instance_model_paths_before_gcode", "Unexpected footprint scope")
        layers = coverage["layers"]
        require(len(layers) == groups[0]["model_layer_count"], "Missing layer observations")
        for layer in layers:
            verify_topology_counts(layer)
            verify_island_gap(layer)
            require(layer["complete"] and layer["invalid_path_count"] == 0, "Fixture coverage is incomplete")
            require(layer["excluded_path_count"] == 0, "Fixture unexpectedly has excluded paths")
            nonnegative(layer["planned_path_area_mm2"], "planned path area")
            require(0 <= layer["hole_intrusion_area_mm2"] <= layer["hole_area_mm2"] + 0.001,
                    "Hole intersection must stay inside the target hole area")
        by_generator[generator] = layers
    if name == "raised-stroke":
        classic = [layer for layer in by_generator["classic"] if layer["bottom_z_mm"] >= 1.2 - 1e-6]
        arachne = [layer for layer in by_generator["arachne"] if layer["bottom_z_mm"] >= 1.2 - 1e-6]
        require(len(classic) == len(arachne) == 6, "Raised stroke must span six 0.2 mm layers")
        require(all(layer["completely_unprinted_island_count"] == 1 for layer in classic),
                "Classic fixture should lose the isolated narrow upper sections")
        require(all(layer["completely_unprinted_island_count"] == 0 and layer["planned_path_area_mm2"] > 0
                    for layer in arachne), "Arachne fixture should produce paths in each upper section")
    elif name == "through-hole":
        for layers in by_generator.values():
            require(all(abs(layer["hole_area_mm2"] - 16) < 0.01 for layer in layers),
                    "The 4 x 4 mm hole must remain excluded from material sections")
            require(all(layer["closed_void_region_count"] == 1 and layer["closed_void_fully_covered_count"] == 0
                        for layer in layers), "The fixture's closed hole must not be fully covered by planned paths")
    elif name == "sparse-block":
        # Sparse infill is not a solid-fill target or a completely unprinted island.
        for layers in by_generator.values():
            require(any(layer["section_area_mm2"] > 0 and layer["solid_fill_target_area_mm2"] == 0
                        for layer in layers), "Sparse interior must not be classified as a solid-fill target")
            require(all(layer["completely_unprinted_island_count"] == 0 for layer in layers),
                    "Ordinary sparse infill is not a completely unprinted island")
    elif name == "sparse-top":
        for layers in by_generator.values():
            # The last model layer is this fixture's top surface. The retained
            # solid target covers it without a separate top-surface difference.
            require(layers[-1]["solid_fill_target_area_mm2"] > 0
                    and layers[-1]["solid_fill_uncovered_area_mm2"] > 10,
                    "Deliberately sparse final top surface must retain its uncovered solid area")
            require(all(layer["completely_unprinted_island_count"] == 0 for layer in layers),
                    "Deliberately sparse top surfaces are not completely unprinted islands")
    elif name == "separated-blocks":
        for layers in by_generator.values():
            # The established CLI fixture has zero XY size compensation. Orca
            # restores uncompensated lslices for elephant-foot layers, so this
            # section metric stays 0.5 mm even with the existing 0.15 mm setting.
            verify_island_fixture_layers(name, layers)
            require(all(layer["hole_area_mm2"] == 0 and layer["completely_unprinted_island_count"] == 0
                        for layer in layers), "Both separated blocks should have paths without enclosed holes")
    elif name == "nested-island":
        for layers in by_generator.values():
            verify_island_fixture_layers(name, layers)
    elif name == "narrow-neck":
        for layers in by_generator.values():
            require(len(layers) == 12
                    and all(math.isclose(layer["slice_z_mm"], 0.1 + index * 0.2, abs_tol=1e-6)
                            for index, layer in enumerate(layers)), "Narrow-neck fixture must retain twelve model sampling planes")
            # Do not assume a particular generator loses or restores the neck.
            # The complete measured counts above are retained as A/B evidence.
    elif name in OPEN_SLOT_AREAS:
        for layers in by_generator.values():
            verify_open_slot_layers(name, layers)
    elif name == "cura-raft":
        for generator, report in reports.items():
            verify_cura_raft(report, gcode_roles[generator])
    elif name == "source-volume-sampling":
        for layers in by_generator.values():
            require(len(layers) == 20 and any(layer["planned_path_area_mm2"] > 0 for layer in layers),
                    "The multi-volume fixture must retain its main body layers and planned paths")
    elif name == "slope-wedge":
        for report in reports.values():
            verify_wedge_alignment(report["objects"]["groups"][0])


def fixture_gcode_extrusions(path):
    """Yield actual positive-E spatial moves from local single-tool fixtures.

    TYPE comments alone, travel, retracts and pure-E recovery are not extrusion
    evidence. Arc presence is supported, but no chord length or area is inferred.
    This is a fixture check, not another production G-code statistics engine.
    """
    position = dict.fromkeys("XYZE", 0.0)
    xyz_relative, e_relative, role = False, False, ""
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(";TYPE:"):
            role = line[6:].strip()
        words = line.split(";", 1)[0].strip().split()
        if not words:
            continue
        command = words[0]
        values = {axis: float(value) for axis, value in
                  re.findall(r"([XYZEIJR])([-+]?(?:\d+(?:\.\d*)?|\.\d+))", " ".join(words[1:]))}
        require(command != "G20", "Fixture parser only supports millimetres")
        if command in ("G90", "G91"):
            xyz_relative = command == "G91"
        elif command in ("M82", "M83"):
            e_relative = command == "M83"
        elif command == "G92":
            position.update({axis: value for axis, value in values.items() if axis in position})
        elif command in ("G0", "G1", "G2", "G3"):
            target = dict(position)
            for axis, value in values.items():
                if axis not in position:
                    continue
                relative = e_relative if axis == "E" else xyz_relative
                target[axis] = position[axis] + value if relative else value
            spatial = math.dist([position[axis] for axis in "XYZ"], [target[axis] for axis in "XYZ"]) > 1e-6
            arc = command in ("G2", "G3") and any(abs(values.get(axis, 0)) > 1e-6 for axis in "IJR")
            if target["E"] > position["E"] and (spatial or arc):
                yield role, command, position, target
            position = target


def gcode_extrusion_roles(path):
    return {role for role, _, _, _ in fixture_gcode_extrusions(path)}


def raised_stroke_gcode_layers(path):
    """Final upper-stroke extrusion evidence, independent of report statistics."""
    layers = set()
    for role, command, position, target in fixture_gcode_extrusions(path):
        if role == "Outer wall" and 1.2 + 1e-6 < target["Z"] <= 2.4 + 1e-6:
            require(command not in ("G2", "G3"), "Unexpected extruding arc in straight-stroke fixture")
            if math.hypot(target["X"] - position["X"], target["Y"] - position["Y"]) > 1e-6:
                layers.add(round(target["Z"], 6))
    return sorted(layers)


def fixture_process_settings(name, generator, process_config, support_enabled=None):
    settings = dict(process_config, wall_generator=generator)
    if name == "sparse-block":
        settings.update(top_shell_layers="0", bottom_shell_layers="0", sparse_infill_density="15%")
    elif name == "sparse-top":
        settings.update(top_surface_density="25%")
    elif name == "cura-raft":
        settings.update(raft_mode="cura_v1", raft_layers="3", raft_base_layers="1", raft_interface_layers="1",
                        raft_surface_layers="1", raft_base_layer_height="0.3", raft_interface_layer_height="0.3",
                        raft_surface_layer_height="0.2", raft_airgap="0.2", raft_layer_0_z_overlap="0.1",
                        raft_segmented="0", enable_support="0", support_type="normal(auto)", support_style="default",
                        layer_height="0.2", initial_layer_print_height="0.2", adaptive_layer_height="0")
    elif name == "source-volume-sampling":
        settings.update(layer_height="0.2", initial_layer_print_height="0.2", adaptive_layer_height="0",
                        raft_mode="legacy", raft_layers="0", enable_support="0")
    elif name == "new-island-tree":
        settings.update(enable_support="1", support_type="tree(auto)", support_style="tree_slim",
                        support_on_build_plate_only="1", bridge_no_support="0", raft_mode="legacy", raft_layers="0")
    elif name in OPEN_SLOT_AREAS or name in ("nested-island", "slope-wedge"):
        # Keep the production closing radius explicit. Even the 0.36 mm slot is
        # wider than twice 0.049 mm; disable size compensation only for these
        # fixtures so source sections retain their analytical geometry.
        settings.update(layer_height="0.2", initial_layer_print_height="0.2", adaptive_layer_height="0",
                        raft_mode="legacy", raft_layers="0", enable_support="0", slicing_mode="regular",
                        slice_closing_radius="0.049", elefant_foot_compensation="0",
                        xy_contour_compensation="0", xy_hole_compensation="0")
    if support_enabled is not None:
        settings.update(enable_support="1" if support_enabled else "0", support_type="normal(auto)",
                        support_style="default", support_on_build_plate_only="1", bridge_no_support="0", raft_layers="0")
    return settings


def run_case(suite, model, generator, process_config, support_enabled=None):
    name = model.stem + "-" + generator
    if support_enabled is not None:
        name += "-support-" + ("on" if support_enabled else "off")
    case = suite.output / name
    case.mkdir()
    for child in ("output", "data", "cwd"):
        (case / child).mkdir()
    settings = fixture_process_settings(model.stem, generator, process_config, support_enabled)
    process = case / "process.json"
    save_json(process, settings)
    command = [str(suite.args.binary), "--datadir", str(case / "data"), "--debug", "1",
               "--load-settings", f"{suite.args.machine};{process}", "--load-filaments", str(suite.args.filament),
               "--arrange", "1", "--ensure-on-bed", "--slice", "0", "--outputdir", str(case / "output"),
               "--slice-analysis-report", str(model)]
    code = suite.run_process(command, case, case / "cwd")
    require(code == 0, f"{name}: CLI exited {code}; see {case}")
    legacy = read_json(case / "output/result.json")
    require(legacy["return_code"] == 0 and len(legacy["sliced_plates"]) == 1, f"{name}: expected one successful plate")
    facts = gcode_facts(case / "output/plate_1.gcode")
    facts["path"] = case / "output/plate_1.gcode"
    if model.stem == "cura-raft":
        values = config_values(facts)
        require(values.get("raft_mode") == "cura_v1"
                and math.isclose(float(values.get("raft_layer_0_z_overlap", "nan")), 0.1, abs_tol=1e-6),
                "The exported effective config must retain the requested Cura V1 raft overlap")
    elif model.stem in OPEN_SLOT_AREAS or model.stem in ("nested-island", "slope-wedge"):
        values = config_values(facts)
        for key, expected in (("slice_closing_radius", 0.049), ("elefant_foot_compensation", 0),
                              ("xy_contour_compensation", 0), ("xy_hole_compensation", 0)):
            require(math.isclose(float(values.get(key, "nan")), expected, abs_tol=1e-9),
                    f"{name}: unexpected effective {key}")
    elif model.stem == "new-island-tree":
        values = config_values(facts)
        require(values.get("enable_support") == "1" and values.get("support_type") == "tree(auto)"
                and values.get("support_style") == "tree_slim" and values.get("raft_layers") == "0",
                "The new island must use actual slim tree support without a raft")
    report = read_json(case / "output/plate_1.analysis.json")
    validate_report(report, case / "output", legacy["sliced_plates"][0], facts)
    verify_fixture_bed_footprint(model.stem, report, config_values(facts))
    verify_parallel_clearance(report["objects"]["groups"][0]["planned_layer_coverage"]["layers"], model.stem)
    if model.stem == "raised-stroke":
        layers = raised_stroke_gcode_layers(facts["path"])
        require(layers == ([] if generator == "classic" else [1.4, 1.6, 1.8, 2.0, 2.2, 2.4]),
                f"{name}: final G-code does not confirm the expected upper stroke layers: {layers}")
    print(f"PASS {name}", flush=True)
    return report, facts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="New evidence directory; parent must exist")
    parser.add_argument("--machine", type=Path, default=ASSETS / "profiles/machine/ep7_0.4.json")
    parser.add_argument("--process", type=Path, default=ASSETS / "profiles/process/ep7_standard_0.20.json")
    parser.add_argument("--filament", type=Path, default=ASSETS / "profiles/filament/generic_pla_0.4.json")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 1800:
        parser.error("--timeout must be 1..1800 seconds")
    for key in ("binary", "machine", "process", "filament"):
        path = getattr(args, key).expanduser().resolve()
        if not path.is_file():
            parser.error(f"Missing {key}: {path}")
        setattr(args, key, path)
    output = args.output_dir.expanduser().absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error("Output must be a new directory with an existing parent")
    output.mkdir()
    output = output.resolve()
    protected = {str(path): digest(path) for path in (args.binary, args.machine, args.process, args.filament)}
    if (ROOT / "result.json").is_file():
        protected[str(ROOT / "result.json")] = digest(ROOT / "result.json")
    suite = Suite(args, output)
    summary = {"passed": False, "cases": {}, "protected_inputs": protected,
               "scope": "Local planned-path and cost evidence only; no physical printing or automatic quality verdict"}
    try:
        for name, model in fixtures(output).items():
            by_support, configs_by_support = {}, {}
            for support_enabled in ((False, True) if name in SUPPORT_FIXTURES else (None,)):
                reports, configurations, roles = {}, {}, {}
                for generator in ("classic", "arachne"):
                    report, facts = run_case(suite, model, generator, read_json(args.process), support_enabled)
                    reports[generator] = report
                    configurations[generator] = config_values(facts)
                    roles[generator] = gcode_extrusion_roles(facts["path"])
                changed = {key for key in configurations["classic"].keys() | configurations["arachne"].keys()
                           if configurations["classic"].get(key) != configurations["arachne"].get(key)}
                require(changed == {"wall_generator"}, f"Unexpected effective parameter differences: {changed}")
                verify_feature_metrics(name, reports, roles)
                if support_enabled is not None:
                    verify_support_fixture(name, reports, support_enabled, roles)
                elif name == "new-island-tree":
                    verify_support_fixture(name, reports, True, roles)
                by_support[support_enabled] = reports
                configs_by_support[support_enabled] = configurations
                label = name if support_enabled is None else name + "-support-" + ("on" if support_enabled else "off")
                summary["cases"][label] = {"model_sha256": digest(model), "changed_parameters": sorted(changed),
                                           "reports": reports,
                                           "actual_gcode_extrusion_roles": {key: sorted(value) for key, value in roles.items()}}
            if name in SUPPORT_FIXTURES:
                verify_support_geometry_unchanged(by_support[False], by_support[True])
                for generator in ("classic", "arachne"):
                    before, after = configs_by_support[False][generator], configs_by_support[True][generator]
                    changed = {key for key in before.keys() | after.keys() if before.get(key) != after.get(key)}
                    require(changed == {"enable_support"}, f"Unexpected support parameter differences: {changed}")
        for path, expected in protected.items():
            require(digest(Path(path)) == expected, f"Input changed: {path}")
        summary["passed"] = True
    finally:
        save_json(output / "summary.json", summary)
    print(f"Evidence: {output}", flush=True)


if __name__ == "__main__":
    main()
