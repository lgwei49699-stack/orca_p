#!/usr/bin/env python3
"""Local CLI feature comparisons with deterministic closed synthetic models.

Requires an already built Orca and local profiles. Does not build, download,
change input presets, connect to a service or send commands to a printer.
Runs 40 slices, including Classic/Arachne, normal/tree support, raft, source-volume sampling,
wide/narrow U slots, a C-shaped opening, an island inside a hole and a closed 45-degree wedge.
Validates support projection, intervening model print-plane overlaps, source-facet layer
alignment and fixed-XY nominal model-path slab depth, not physical obstruction, printed
surface error, fused-material thickness, automatic print-quality decisions or a preferred wall generator.
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
    validate_plate_bed_footprint, validate_report,
)

SUPPORT_FIXTURES = {"bridge", "cantilever", "new-island"}
BRIDGE_ROLES = {"bridge_infill", "internal_bridge_infill", "overhang_perimeter"}
MODEL_NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
OPEN_SLOT_AREAS = {"u-slot-wide": 86., "u-slot-narrow": 94.2, "c-slot": 106.5}
TOPOLOGY_DERIVED_COUNTS = (
    "section_islands_with_multiple_planned_regions_count",
    "planned_regions_overlapping_multiple_section_islands_count",
    "closed_void_fully_covered_count",
    "closed_voids_with_multiple_remaining_regions_count",
)


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


def verify_cross_layer_metrics(group):
    geometry = group["cross_layer_geometry"]
    require(geometry["scope"] == "per_instance_adjacent_model_sections", "Unexpected cross-layer scope")
    layers = geometry["layers"]
    require(len(layers) == group["model_layer_count"] and bool(layers), "Missing cross-layer observations")
    derived = ("previous_layer_index", "previous_slice_z_mm", "previous_print_z_mm", "previous_model_gap_mm",
               "previous_model_overlap_area_mm2", "outside_previous_model_area_mm2",
               "no_previous_overlap_island_count", "no_previous_overlap_island_area_mm2")
    for index, layer in enumerate(layers):
        require(layer["layer_index"] == index, "Nonsequential cross-layer index")
        for key in ("slice_z_mm", "print_z_mm", "bottom_z_mm", "height_mm", "section_area_mm2"):
            nonnegative(layer[key], key)
        if index == 0:
            require(layer["complete"] is False and all(layer[key] is None for key in derived),
                    "First layer has no previous model section; derived values must remain unknown")
            continue
        previous = layers[index - 1]
        require(layer["complete"] is True and layer["previous_layer_index"] == index - 1,
                "Fixture's adjacent model sections must be completely measured")
        require(layer["previous_slice_z_mm"] == previous["slice_z_mm"]
                and layer["previous_print_z_mm"] == previous["print_z_mm"], "Previous section identity differs")
        require(math.isclose(layer["previous_model_gap_mm"], layer["bottom_z_mm"] - previous["print_z_mm"], abs_tol=1e-6),
                "Model layer Z gap is inconsistent")
        overlap = nonnegative(layer["previous_model_overlap_area_mm2"], "previous_model_overlap_area_mm2")
        outside = nonnegative(layer["outside_previous_model_area_mm2"], "outside_previous_model_area_mm2")
        require(math.isclose(overlap + outside, layer["section_area_mm2"], abs_tol=0.001),
                "Cross-layer section area accounting mismatch")
        count(layer["no_previous_overlap_island_count"], "no_previous_overlap_island_count")
        island_area = nonnegative(layer["no_previous_overlap_island_area_mm2"], "no_previous_overlap_island_area_mm2")
        require(island_area <= outside + 0.001, "No-previous-overlap island area exceeds all area outside previous model")
    planned = group["planned_bridge_paths"]
    require(planned["scope"] == "per_instance_model_paths_before_gcode" and planned["complete"] is True,
            "Fixture planned bridge statistics are incomplete")
    require(len(planned["roles"]) == 3 and {role["role"] for role in planned["roles"]} == BRIDGE_ROLES,
            "Expected all three bridge/overhang roles, including zero-path roles")
    for role in planned["roles"]:
        require(role["invalid_path_count"] == 0, "Fixture bridge role has invalid path data")
        count(role["path_count"], "path_count")
        count(role["excluded_path_count"], "excluded_path_count")
        nonnegative(role["path_length_mm"], "path_length_mm")
        nonnegative(role["layer_footprint_area_sum_mm2"], "layer_footprint_area_sum_mm2")
    return layers


def gcode_role_length(report, roles):
    # Read the existing G-code processor's complete role list, not a new parser.
    return sum(nonnegative(item["extrusion_ranges"]["path_length_mm"], "G-code role path_length_mm")
               for item in report["gcode"]["extrusion_by_role"] if item["role"] in roles)


def verify_external_bridge_projection(group):
    """External bridge centerlines versus previous model paths, not physical anchoring or internal bridges."""
    projection = group["external_bridge_projection"]
    require(projection["scope"] == "external_bridge_centerlines_over_previous_model_footprint_before_gcode",
            "Unexpected external bridge projection scope")
    layers = projection["layers"]
    geometry = group["cross_layer_geometry"]["layers"]
    require(isinstance(layers, list) and len(layers) == group["model_layer_count"] == len(geometry),
            "Missing external bridge model-layer observations")
    derived = ("uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm")
    for index, (layer, model_layer) in enumerate(zip(layers, geometry)):
        require(count(layer["layer_index"], "layer_index") == index, "Nonsequential external bridge layer")
        require(type(layer["complete"]) is bool, "External bridge completeness must be a boolean")
        previous_index = layer["previous_layer_index"]
        require(previous_index is None if index == 0 else count(previous_index, "previous_layer_index") == index - 1,
                "External bridge previous-model identity differs")
        for key, expected in (("print_z_mm", model_layer["print_z_mm"]),
                              ("previous_print_z_mm", None if index == 0 else geometry[index - 1]["print_z_mm"])):
            value = layer[key]
            require(value is None or (type(value) in (int, float) and math.isfinite(value)), f"Invalid {key}: {value}")
            require(value == expected or (value is not None and expected is not None
                    and math.isclose(value, expected, abs_tol=1e-6)), "External bridge model Z differs")
        if index == 0:
            require(layer["complete"] is False, "The first model layer has no previous-model footprint, including above a raft")
        if not layer["complete"]:
            require(all(layer[key] is None for key in derived), "Unavailable external bridge observations must remain unknown")
            continue
        require(layer["print_z_mm"] is not None and layer["previous_print_z_mm"] is not None
                and layer["print_z_mm"] > layer["previous_print_z_mm"], "Complete bridge observations require increasing model Z")
        uncovered = count(layer["uncovered_run_count"], "uncovered_run_count")
        two_sided = count(layer["two_sided_run_count"], "two_sided_run_count")
        require(two_sided <= uncovered, "Two-sided runs must be a subset of uncovered runs")
        for number, key in ((uncovered, "max_uncovered_run_mm"), (two_sided, "max_two_sided_run_mm")):
            if number == 0:
                require(layer[key] is None, "No measured runs have no applicable maximum length; do not substitute zero")
            else:
                require(nonnegative(layer[key], key) > 0, "A measured uncovered run must have positive arc length")
        if two_sided:
            require(layer["max_two_sided_run_mm"] <= layer["max_uncovered_run_mm"] + 1e-6,
                    "The longest two-sided run exceeds the longest uncovered run")
    return layers


def verify_support_projection(group):
    """Validate XY allocation, nominal Z gaps and intermediate print planes, not physical support adequacy."""
    projection = group["planned_support_projection"]
    require(projection["scope"] == "per_instance_plate_support_projection_before_gcode", "Unexpected support projection scope")
    instances = projection["instances"]
    require(isinstance(instances, list) and len(instances) == count(group["instance_count"], "instance_count"),
            "Missing support projection instances")
    geometry = group["cross_layer_geometry"]["layers"]
    for instance_index, instance in enumerate(instances):
        require(count(instance["instance_index"], "instance_index") == instance_index, "Wrong support projection instance identity")
        layers = instance["layers"]
        require(isinstance(layers, list) and len(layers) == group["model_layer_count"] == len(geometry),
                "Missing support projection model layers")
        for index, (layer, model_layer) in enumerate(zip(layers, geometry)):
            require(count(layer["layer_index"], "layer_index") == index, "Nonsequential support projection layer")
            require(type(layer["complete"]) is bool, "Support projection completeness must be a boolean")
            for key, source in (("model_bottom_z_mm", "bottom_z_mm"), ("model_print_z_mm", "print_z_mm")):
                value = layer[key]
                require(value is None or (type(value) in (int, float) and math.isfinite(value)), f"Invalid {key}: {value}")
                expected = model_layer[source]
                require(value == expected or (value is not None and expected is not None
                        and math.isclose(value, expected, abs_tol=1e-6)), "Support projection uses different model Z")
            if not layer["complete"]:
                require(all(layer[key] is None for key in ("target_area_mm2", "unmatched_target_area_mm2", "support_levels")),
                        "Incomplete support projection must preserve unknown areas and levels")
                continue
            require(layer["model_bottom_z_mm"] is not None and layer["model_print_z_mm"] is not None,
                    "Complete support projection requires model Z")
            target = nonnegative(layer["target_area_mm2"], "target_area_mm2")
            unmatched = nonnegative(layer["unmatched_target_area_mm2"], "unmatched_target_area_mm2")
            source_target = model_layer["section_area_mm2"] if index == 0 else model_layer["outside_previous_model_area_mm2"]
            require(source_target is not None and target <= source_target + 0.001,
                    "Planned target exceeds the corresponding model-only projection")
            levels = layer["support_levels"]
            require(isinstance(levels, list), "Complete support projection requires a level list")
            previous_z, matched = None, 0.
            for level in levels:
                support_z = nonnegative(level["support_print_z_mm"], "support_print_z_mm")
                require(support_z <= layer["model_print_z_mm"] + 1e-6, "Support above the model print plane is not a candidate")
                require(previous_z is None or support_z < previous_z, "Support Z must be unique and descending")
                gap = level["model_layer_bottom_gap_mm"]
                require(type(gap) in (int, float) and math.isfinite(gap)
                        and math.isclose(gap, layer["model_bottom_z_mm"] - support_z, abs_tol=1e-6),
                        "Support gap must use the model bottom; negative nominal gaps are valid")
                area = nonnegative(level["nearest_projection_area_mm2"], "nearest_projection_area_mm2")
                require(area > 0, "Support levels must contain only positive projected intersections")
                intervening = level["intervening_model_projection"]
                require(isinstance(intervening, dict) and type(intervening["complete"]) is bool,
                        "Intervening model projection requires a boolean completeness flag")
                if support_z >= layer["model_print_z_mm"]:
                    require(not intervening["complete"], "Equal print planes have no intervening-plane observation")
                if intervening["complete"]:
                    intervening_area = nonnegative(intervening["area_mm2"], "intervening model projection area_mm2")
                    require(intervening_area <= area + 0.001,
                            "Intervening model overlap exceeds this support level's allocated target")
                else:
                    require(intervening["area_mm2"] is None,
                            "Incomplete intervening model projection must preserve unknown area")
                matched += area
                previous_z = support_z
            require(math.isclose(matched + unmatched, target, abs_tol=0.001), "Support allocation areas do not sum to target")
            require(target > 0 or (unmatched == 0 and not levels), "An empty target must have no allocated support levels")
    return instances


def verify_joint_lower_path_slab_coverage(group):
    """Nominal lower-source path slabs, not execution order or physical support adequacy."""
    observation = group["joint_lower_path_slab_coverage"]
    require(observation["scope"] == "per_instance_lower_source_print_z_nominal_path_slabs_before_gcode",
            "Unexpected joint lower-path slab scope")
    resolution = nonnegative(observation["z_resolution_mm"], "z_resolution_mm")
    require(resolution == 0.000001, "Joint path slabs must use the declared native coordinate resolution")
    instances = observation["instances"]
    require(isinstance(instances, list) and len(instances) == count(group["instance_count"], "instance_count"),
            "Missing joint lower-path slab instances")
    geometry = group["cross_layer_geometry"]["layers"]
    require(isinstance(geometry, list) and len(geometry) == count(group["model_layer_count"], "model_layer_count"),
            "Missing reference model-layer geometry")
    derived = ("uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm")
    for instance_index, instance in enumerate(instances):
        require(count(instance["instance_index"], "instance_index") == instance_index, "Wrong joint slab instance identity")
        layers = instance["layers"]
        require(isinstance(layers, list) and len(layers) == len(geometry), "Missing joint slab model layers")
        for index, (layer, model_layer) in enumerate(zip(layers, geometry)):
            require(count(layer["layer_index"], "layer_index") == index == count(model_layer["layer_index"], "reference layer_index"),
                    "Joint slab model-layer identity differs")
            require(type(layer["complete"]) is bool, "Joint slab layer completeness must be boolean")
            print_z = layer["print_z_mm"]
            require(print_z is None or (type(print_z) in (int, float) and math.isfinite(print_z)), "Invalid joint slab print Z")
            require(print_z == model_layer["print_z_mm"], "Joint slab print Z differs from its model layer")
            planes = layer["bottom_planes"]
            if planes is None:
                require(layer["complete"] is False, "Unknown target path heights cannot produce a complete layer")
                continue
            require(isinstance(planes, list) and print_z is not None, "Known bottom planes require valid model Z")
            previous_z = None
            for plane in planes:
                bottom = plane["bottom_z_mm"]
                require(type(bottom) in (int, float) and math.isfinite(bottom) and bottom <= print_z,
                        "Nominal path bottom must be finite and no higher than its print plane")
                scaled_bottom = bottom / resolution
                require(math.isfinite(scaled_bottom), "Nominal path bottom cannot be represented at native resolution")
                quantized_bottom = round(scaled_bottom)
                require(previous_z is None or quantized_bottom > previous_z,
                        "Nominal bottom planes must be ascending and unique at native resolution")
                previous_z = quantized_bottom
                require(type(plane["complete"]) is bool, "Joint slab plane completeness must be boolean")
                bridge = plane["external_bridge"]
                require(isinstance(bridge, dict) and type(bridge["complete"]) is bool,
                        "Joint external bridge completeness must be boolean")
                if plane["complete"]:
                    target = nonnegative(plane["target_area_mm2"], "joint slab target_area_mm2")
                    uncovered = nonnegative(plane["uncovered_area_mm2"], "joint slab uncovered_area_mm2")
                    require(uncovered <= target, "Joint slab uncovered area exceeds its target")
                else:
                    require(plane["target_area_mm2"] is None and plane["uncovered_area_mm2"] is None
                            and bridge["complete"] is False, "Unknown joint plane cannot expose partial areas or complete bridges")
                if not bridge["complete"]:
                    require(all(bridge[key] is None for key in derived), "Unknown joint bridge values must remain null")
                    continue
                runs = count(bridge["uncovered_run_count"], "joint bridge uncovered_run_count")
                two_sided = count(bridge["two_sided_run_count"], "joint bridge two_sided_run_count")
                require(two_sided <= runs, "Joint two-sided runs must be a subset of uncovered runs")
                for number, key in ((runs, "max_uncovered_run_mm"), (two_sided, "max_two_sided_run_mm")):
                    if number:
                        require(nonnegative(bridge[key], key) > 0, "A joint uncovered run must have positive length")
                    else:
                        require(bridge[key] is None, "No joint bridge runs have no applicable maximum")
                if two_sided:
                    require(bridge["max_two_sided_run_mm"] <= bridge["max_uncovered_run_mm"],
                            "Joint two-sided maximum exceeds the maximum uncovered run")
            require(layer["complete"] == all(plane["complete"] for plane in planes),
                    "Joint layer completeness must summarize plane areas, not bridge observations")
    return instances


def verify_support_fixture(name, reports, support_enabled):
    overlap, outside, island_count, island_area = {
        "bridge": (48, 48, 0, 0), "cantilever": (24, 48, 0, 0), "new-island": (24, 24, 1, 24),
        "new-island-tree": (24, 24, 1, 24),
    }[name]
    for report in reports.values():
        group = report["objects"]["groups"][0]
        layers = group["cross_layer_geometry"]["layers"]
        transitions = [layer for layer in layers if math.isclose(layer["slice_z_mm"], 4.1, abs_tol=1e-6)]
        require(len(transitions) == 1, "Expected a model section at source Z=4.1 mm")
        transition = transitions[0]
        for key, expected in (("previous_model_overlap_area_mm2", overlap),
                              ("outside_previous_model_area_mm2", outside),
                              ("no_previous_overlap_island_area_mm2", island_area)):
            require(math.isclose(transition[key], expected, abs_tol=0.001), f"{name}: unexpected {key}")
        require(transition["no_previous_overlap_island_count"] == island_count,
                f"{name}: partially overlapping overhangs must not count as whole new islands")
        projected_layers = verify_support_projection(group)[0]["layers"]
        projected = projected_layers[transition["layer_index"]]
        require(projected["complete"] is True and projected["target_area_mm2"] > 0,
                f"{name}: the transition must retain a positive planned target with or without support")
        bridge_projection = verify_external_bridge_projection(group)[transition["layer_index"]]
        require(bridge_projection["complete"] is True and bridge_projection["uncovered_run_count"] > 0,
                f"{name}: external bridge centerlines must retain positive runs outside previous model paths")
        if name in ("new-island", "new-island-tree"):
            require(bridge_projection["two_sided_run_count"] == 0,
                    "A separate new island has no prior model paths at either side, even with generated supports")
        support_length = gcode_role_length(report, {"support_material", "support_material_interface"})
        support_volumes = [item["estimated_volume_mm3"]["support"] for item in report["gcode"]["extruders"]]
        if support_enabled:
            require(group["support_layer_count"] > 0 and support_length > 0,
                    f"{name}: enabling support must generate actual G-code support paths")
            require(any(value is not None and nonnegative(value, "support volume") > 0 for value in support_volumes),
                    f"{name}: expected positive G-code support volume")
            require(bool(projected["support_levels"]), f"{name}: expected a support projection under the new model area")
            if name != "new-island-tree":
                require(any(math.isclose(level["model_layer_bottom_gap_mm"], 0.2, abs_tol=1e-6)
                            for level in projected["support_levels"]), f"{name}: expected the ordinary 0.2 mm top gap")
        else:
            require(group["support_layer_count"] == 0 and support_length == 0,
                    f"{name}: disabled support unexpectedly generated support")
            require(all(value is None or value == 0 for value in support_volumes),
                    f"{name}: disabled support unexpectedly reports positive volume")
            require(all(layer["complete"] is True and layer["support_levels"] == []
                        and math.isclose(layer["unmatched_target_area_mm2"], layer["target_area_mm2"], abs_tol=0.001)
                        for layer in projected_layers), f"{name}: no-support/no-raft targets must remain unmatched")
        if name == "bridge" and not support_enabled:
            require(bridge_projection["two_sided_run_count"] > 0,
                    "The two-pier fixture must contain a run with previous model paths on both sides")
            bridge = next(item for item in group["planned_bridge_paths"]["roles"] if item["role"] == "bridge_infill")
            require(bridge["path_count"] > 0 and bridge["path_length_mm"] > 0
                    and bridge["layer_footprint_area_sum_mm2"] > 0,
                    "The unsupported two-pier bridge must generate planned bridge infill")
            require(gcode_role_length(report, {"bridge_infill"}) > 0,
                    "The two-pier bridge must also reach final G-code bridge infill")


def verify_support_geometry_unchanged(off, on):
    for generator in ("classic", "arachne"):
        before = off[generator]["objects"]["groups"][0]["cross_layer_geometry"]
        after = on[generator]["objects"]["groups"][0]["cross_layer_geometry"]
        require(before == after, "Support must not erase the model-only projection evidence")


def verify_cura_raft(report):
    group = report["objects"]["groups"][0]
    layers = verify_cross_layer_metrics(group)
    require(len(layers) == 20, "Raft must not change the cube's twenty model sampling planes")
    require(all(math.isclose(layer["slice_z_mm"], 0.1 + index * 0.2, abs_tol=1e-6)
                and math.isclose(layer["height_mm"], 0.2, abs_tol=1e-6) for index, layer in enumerate(layers)),
            "Unexpected model sampling planes or source-band heights above raft")
    surfaces = verify_source_surface_layer_alignment(group)
    require(len(surfaces) == 1 and surfaces[0]["volume_index"] == 0 and surfaces[0]["complete"] is True,
            "Cura raft fixture must retain one complete source-surface target")
    # The source fixture is a 0..4 mm box (two triangles on each horizontal
    # end). Its twenty 0.2 mm source bands start at 0 and end at 4, although
    # actual model print planes are raised and later shifted by Cura overlap.
    require(surfaces[0]["horizontal_facet_count"] == 4
            and math.isclose(surfaces[0]["max_horizontal_boundary_offset_mm"], 0, abs_tol=1e-6),
            "Cura raft must not turn aligned source-box ends into print-Z boundary offsets")
    require(layers[0]["bottom_z_mm"] > 0 and layers[1]["complete"] is True,
            "Raft must lift the model without invalidating the second section measurement")
    require(math.isclose(layers[1]["previous_model_gap_mm"], -0.1, abs_tol=1e-6),
            "Cura V1's intentional 0.1 mm Z overlap must remain a measured negative gap")
    require(all(math.isclose(layer["previous_model_gap_mm"], 0, abs_tol=1e-6) for layer in layers[2:]),
            "Only the first-to-second model layer transition should contain the raft overlap")
    require(group["support_layer_count"] > 0
            and gcode_role_length(report, {"support_material", "support_material_interface"}) > 0,
            "Cura raft must generate actual support layers and G-code paths")
    require(any(item["estimated_volume_mm3"]["support"] is not None
                and nonnegative(item["estimated_volume_mm3"]["support"], "raft support volume") > 0
                for item in report["gcode"]["extruders"]), "Cura raft must use positive support material volume")
    first = verify_support_projection(group)[0]["layers"][0]
    require(first["complete"] is True and first["target_area_mm2"] > 0
            and any(math.isclose(level["model_layer_bottom_gap_mm"], 0.2, abs_tol=1e-6) for level in first["support_levels"]),
            "Cura raft must project under the first model layer with its 0.2 mm gap even when enable_support is false")
    require(all(level["intervening_model_projection"] == {"complete": True, "area_mm2": 0}
                for level in first["support_levels"]),
            "The single-object raft fixture has no model print plane between raft and first model layer")


def verify_source_volume_sampling(report):
    group = report["objects"]["groups"][0]
    require(group["model_layer_count"] == 20, "Body must retain twenty model layers for both thin-sheet targets")
    sampling = group["source_volume_sampling"]
    require(sampling["scope"] == "per_instance_source_model_part_on_retained_layers", "Unexpected source-volume scope")
    volumes = sampling["volumes"]
    require(len(volumes) == 3 and [volume["volume_index"] for volume in volumes] == [0, 1, 2],
            "Expected three model-part source volumes in one object, not three objects or one merged volume")
    for volume, (low, high, expected_count) in zip(volumes, ((0, 4, 20), (1.21, 1.26, 0), (1.28, 1.32, 1))):
        require(volume["complete"] is True, "Fresh source-volume fixture must have complete cached measurements")
        require(math.isclose(volume["z_min_mm"], low, abs_tol=1e-6)
                and math.isclose(volume["z_max_mm"], high, abs_tol=1e-6), "Source-volume Z bounds changed")
        for key in ("interior_plane_count", "source_section_layer_count", "final_section_overlap_layer_count",
                    "planned_path_overlap_layer_count"):
            require(count(volume[key], key) == expected_count, f"Volume {volume['volume_index']}: unexpected {key}")
        require(count(volume["boundary_plane_count"], "boundary_plane_count") == 0,
                "Fixture avoids all source-volume Z boundary planes")


def verify_source_surface_layer_alignment(group):
    """Source facet reference offsets only, not the final Boolean surface or print error."""
    alignment = group["source_surface_layer_alignment"]
    require(alignment["scope"] == "source_model_part_facets_and_retained_source_layer_bands",
            "Unexpected source-surface layer-alignment scope")
    volumes = alignment["volumes"]
    require(isinstance(volumes, list), "Source-surface volumes must be a list")
    indexes = [count(volume["volume_index"], "volume_index") for volume in volumes]
    require(indexes == sorted(set(indexes)), "Source-surface volume identities must be unique and ordered")
    pairs = (("non_horizontal_facet_band_count", "max_vertical_projection_normal_offset_mm"),
             ("horizontal_facet_count", "max_horizontal_boundary_offset_mm"))
    for volume in volumes:
        require(type(volume["complete"]) is bool, "Source-surface completeness must be a boolean")
        if group["model_layer_count"] == 0:
            require(volume["complete"] is False, "No retained source layers cannot provide surface alignment")
        if not volume["complete"]:
            require(all(volume[key] is None for pair in pairs for key in pair),
                    "Incomplete source-surface observations must remain unknown")
            continue
        for count_key, maximum_key in pairs:
            measured = count(volume[count_key], count_key)
            if measured == 0:
                require(volume[maximum_key] is None, "No applicable source facets have no maximum offset")
            else:
                # Vertical facets and aligned horizontal facets legitimately measure zero.
                nonnegative(volume[maximum_key], maximum_key)
    return volumes


def verify_wedge_alignment(group):
    volumes = verify_source_surface_layer_alignment(group)
    require(len(volumes) == 1 and volumes[0]["volume_index"] == 0 and volumes[0]["complete"] is True,
            "The wedge must retain one complete source model-part observation")
    volume = volumes[0]
    require(volume["non_horizontal_facet_band_count"] > 0 and volume["horizontal_facet_count"] == 2,
            "The wedge has non-horizontal facets and two horizontal bottom triangles")
    # The actual slicing plane is float(slice_z), while band endpoints remain
    # double. Permit float-rounding tails, not a print-quality acceptance band.
    require(math.isclose(volume["max_vertical_projection_normal_offset_mm"], 0.2 / (2 * math.sqrt(2)), abs_tol=1e-6),
            "45-degree wedge reference normal offset differs from the known 0.2 mm source bands")
    require(math.isclose(volume["max_horizontal_boundary_offset_mm"], 0, abs_tol=1e-6),
            "The wedge bottom must align with the first source-band boundary")
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
    """Check geometric accounting only; fragmented planned regions are not a quality verdict."""
    require(type(layer["complete"]) is bool, "Coverage completeness must be a boolean")
    if not layer["complete"]:
        require(all(layer[key] is None for key in ("section_island_count", "closed_void_region_count", *TOPOLOGY_DERIVED_COUNTS)),
                "Incomplete planned coverage must preserve unknown topology counts")
        return
    islands = count(layer["section_island_count"], "section_island_count")
    voids = count(layer["closed_void_region_count"], "closed_void_region_count")
    fragmented, spanning, filled, divided = (count(layer[key], key) for key in TOPOLOGY_DERIVED_COUNTS)
    require(fragmented <= islands, "Fragmented source-island count exceeds source islands")
    require(islands >= 2 or spanning == 0, "A planned region cannot span multiple source islands when fewer than two exist")
    require(filled + divided <= voids, "Fully covered and divided closed voids must be distinct subsets of source voids")


def verify_enclosed_exterior(layer):
    """A zero residual enclosed blank is not proof that an opening remains usable."""
    regions = layer["planned_enclosed_exterior_region_count"]
    area = layer["planned_enclosed_exterior_area_mm2"]
    require(type(layer["complete"]) is bool, "Coverage completeness must be a boolean")
    if not layer["complete"]:
        require(regions is None and area is None, "Incomplete coverage must preserve unknown enclosed exterior observations")
        return
    count(regions, "planned_enclosed_exterior_region_count")
    nonnegative(area, "planned_enclosed_exterior_area_mm2")
    require((regions == 0) == (area == 0), "Enclosed exterior region count and positive-area observation disagree")


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
        # The measured planned enclosure may be zero or positive for either
        # generator. Neither result alone is a pass/fail judgment about a slot.


def verify_feature_metrics(name, reports):
    by_generator = {}
    for generator, report in reports.items():
        groups = report["objects"]["groups"]
        require(len(groups) == 1 and groups[0]["instance_count"] == 1, "Expected one fixture instance")
        verify_joint_lower_path_slab_coverage(groups[0])
        verify_cross_layer_metrics(groups[0])
        verify_external_bridge_projection(groups[0])
        surfaces = verify_source_surface_layer_alignment(groups[0])
        require([volume["volume_index"] for volume in surfaces]
                == [volume["volume_index"] for volume in groups[0]["source_volume_sampling"]["volumes"]],
                "Source-surface and source-volume observations must identify the same model-part targets")
        require(surfaces and all(volume["complete"] is True for volume in surfaces),
                "Fresh fixtures must have complete source-surface observations")
        if name != "slope-wedge":
            require(all(volume["non_horizontal_facet_band_count"] > 0
                        and volume["max_vertical_projection_normal_offset_mm"] == 0 for volume in surfaces),
                    "Axis-aligned fixture sides have zero Z-projection normal offset, also above a raft")
        instances = verify_support_projection(groups[0])
        require(all(layer["complete"] is True for instance in instances for layer in instance["layers"]),
                "Fresh fixtures must have complete support projection observations, including the first model layer")
        coverage = groups[0]["planned_layer_coverage"]
        require(coverage["scope"] == "per_instance_model_paths_before_gcode", "Unexpected footprint scope")
        layers = coverage["layers"]
        require(len(layers) == groups[0]["model_layer_count"], "Missing layer observations")
        for layer in layers:
            verify_topology_counts(layer)
            verify_enclosed_exterior(layer)
            verify_island_gap(layer)
            require(layer["complete"] and layer["invalid_path_count"] == 0, "Fixture coverage is incomplete")
            require(layer["excluded_path_count"] == 0, "Fixture unexpectedly has excluded paths")
            require(abs(layer["section_area_mm2"] - layer["section_covered_area_mm2"]
                        - layer["section_uncovered_area_mm2"]) < 0.001, "Section area accounting mismatch")
            require(0 <= layer["hole_intrusion_area_mm2"] <= layer["hole_area_mm2"] + 0.001,
                    "Hole intersection must stay inside the target hole area")
        by_generator[generator] = layers
    if name == "raised-stroke":
        classic = [layer for layer in by_generator["classic"] if layer["bottom_z_mm"] >= 1.2 - 1e-6]
        arachne = [layer for layer in by_generator["arachne"] if layer["bottom_z_mm"] >= 1.2 - 1e-6]
        require(len(classic) == len(arachne) == 6, "Raised stroke must span six 0.2 mm layers")
        require(all(layer["completely_unprinted_island_count"] == 1 for layer in classic),
                "Classic fixture should lose the isolated narrow upper sections")
        require(all(layer["completely_unprinted_island_count"] == 0 and layer["section_covered_area_mm2"] > 0
                    for layer in arachne), "Arachne fixture should produce paths in each upper section")
    elif name == "through-hole":
        for layers in by_generator.values():
            require(all(abs(layer["hole_area_mm2"] - 16) < 0.01 for layer in layers),
                    "The 4 x 4 mm hole must remain excluded from material sections")
            require(all(layer["closed_void_region_count"] == 1 and layer["closed_void_fully_covered_count"] == 0
                        and layer["closed_voids_with_multiple_remaining_regions_count"] == 0 for layer in layers),
                    "The fixture's closed hole must remain one partially or wholly open region")
    elif name == "sparse-block":
        # Multiple planned regions are allowed in ordinary sparse infill. Do not
        # infer missing necks or print defects from that count or uncovered area.
        for layers in by_generator.values():
            require(any(layer["section_uncovered_area_mm2"] > 20 and layer["solid_fill_target_area_mm2"] == 0
                        for layer in layers), "Sparse interior must not be classified as a solid-fill target")
            require(all(layer["completely_unprinted_island_count"] == 0 for layer in layers),
                    "Ordinary sparse infill is not a completely unprinted island")
    elif name == "sparse-top":
        for layers in by_generator.values():
            require(any(layer["top_fill_target_area_mm2"] > 0 and layer["top_fill_uncovered_area_mm2"] > 10
                        for layer in layers), "Deliberately sparse top surfaces must retain their uncovered area")
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
            require(all(layer["section_island_count"] == 2
                        and layer["planned_regions_overlapping_multiple_section_islands_count"] == 0 for layer in layers),
                    "Separated blocks must not share a planned material region across their gap")
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
        for report in reports.values():
            verify_cura_raft(report)
    elif name == "source-volume-sampling":
        for report in reports.values():
            verify_source_volume_sampling(report)
    elif name == "slope-wedge":
        for report in reports.values():
            verify_wedge_alignment(report["objects"]["groups"][0])


def raised_stroke_gcode_layers(path):
    """Fixture-only check of exported upper-stroke extrusion, not a general coverage parser."""
    position = dict.fromkeys("XYZE", 0.0)
    xyz_relative, e_relative, role = False, False, ""
    layers = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(";TYPE:"):
            role = line[6:].strip()
        words = line.split(";", 1)[0].strip().split()
        if not words:
            continue
        command = words[0]
        values = {axis: float(value) for axis, value in
                  re.findall(r"([XYZE])(-?(?:\d+(?:\.\d*)?|\.\d+))", " ".join(words[1:]))}
        require(command != "G20", "Fixture parser only supports millimetres")
        if command in ("G90", "G91"):
            xyz_relative = command == "G91"
        elif command in ("M82", "M83"):
            e_relative = command == "M83"
        elif command == "G92":
            position.update(values)
        elif command in ("G0", "G1", "G2", "G3"):
            target = dict(position)
            for axis, value in values.items():
                relative = e_relative if axis == "E" else xyz_relative
                target[axis] = position[axis] + value if relative else value
            if role == "Outer wall" and target["E"] > position["E"] and 1.2 + 1e-6 < target["Z"] <= 2.4 + 1e-6:
                require(command not in ("G2", "G3"), "Unexpected extruding arc in straight-stroke fixture")
                if math.hypot(target["X"] - position["X"], target["Y"] - position["Y"]) > 1e-6:
                    layers.add(round(target["Z"], 6))
            position = target
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
                reports, configurations = {}, {}
                for generator in ("classic", "arachne"):
                    report, facts = run_case(suite, model, generator, read_json(args.process), support_enabled)
                    reports[generator] = report
                    configurations[generator] = config_values(facts)
                changed = {key for key in configurations["classic"].keys() | configurations["arachne"].keys()
                           if configurations["classic"].get(key) != configurations["arachne"].get(key)}
                require(changed == {"wall_generator"}, f"Unexpected effective parameter differences: {changed}")
                verify_feature_metrics(name, reports)
                if support_enabled is not None:
                    verify_support_fixture(name, reports, support_enabled)
                elif name == "new-island-tree":
                    verify_support_fixture(name, reports, True)
                by_support[support_enabled] = reports
                configs_by_support[support_enabled] = configurations
                label = name if support_enabled is None else name + "-support-" + ("on" if support_enabled else "off")
                summary["cases"][label] = {"model_sha256": digest(model), "changed_parameters": sorted(changed),
                                           "reports": reports}
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
