#!/usr/bin/env python3
"""Exercise opt-in slicing metrics with an already built Orca CLI.

Uses isolated working/data/output directories and retains all evidence in a new
directory. Never builds, downloads, sends a print, or changes input presets.
The default model and resolved profiles are local evaluation fixtures; supply
--model/--machine/--process/--filament when they are not available.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path, PureWindowsPath
import re
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "planning/cache/cli-assets"
WALL_CLOCK_FIELDS = {"prepare_time", "export_time", "sliced_time", "sliced_time_with_cache"}
QUALITY_CLAIMS = {"print_ready", "printable", "quality_passed", "print_quality_passed"}
REMOVED_GROUP_METRICS = {
    "nominal_target_path_connectivity", "source_volume_sampling", "source_surface_layer_alignment",
    "solid_fill_target_depth", "planned_material_depth", "joint_lower_path_slab_coverage",
    "planned_support_projection", "cross_layer_geometry", "external_bridge_projection",
    "planned_perimeter_loops", "planned_bridge_paths", "planned_model_bed_footprint_area_mm2_per_instance",
}
CORE_LAYER_FIELDS = {
    "layer_index", "slice_z_mm", "print_z_mm", "bottom_z_mm", "height_mm", "complete",
    "excluded_path_count", "invalid_path_count", "section_area_mm2", "planned_path_area_mm2",
    "section_island_count", "section_min_island_gap_mm",
    "parallel_boundary_clearance", "completely_unprinted_island_count", "completely_unprinted_island_area_mm2",
    "hole_area_mm2", "hole_intrusion_area_mm2", "closed_void_region_count", "closed_void_fully_covered_count",
    "solid_fill_target_area_mm2", "solid_fill_uncovered_area_mm2",
}
CORE_GCODE_FIELDS = {"normal_estimate", "extruders", "quality_paths", "small_perimeter_selection"}
CORE_NORMAL_FIELDS = {"total_time_s", "layer_sequence_count"}
CORE_MATERIAL_FIELDS = {
    "extruder_id", "configured_filament_max_volumetric_speed_mm3_s", "estimated_peak_flow",
    "estimated_flow_cap_distance", "layer_time_threshold_observation", "estimated_volume_mm3",
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def read_json(path):
    def reject_constant(value):
        raise ValueError(f"Non-finite JSON value: {value}")
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)


def stable_legacy(value):
    """Ignore elapsed execution time, not estimated printing time or material."""
    if isinstance(value, dict):
        return {key: stable_legacy(item) for key, item in value.items() if key not in WALL_CLOCK_FIELDS}
    if isinstance(value, list):
        return [stable_legacy(item) for item in value]
    return value


def gcode_facts(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    starts = [i for i, line in enumerate(lines) if line.strip() == "; CONFIG_BLOCK_START"]
    ends = [i for i, line in enumerate(lines) if line.strip() == "; CONFIG_BLOCK_END"]
    require(len(starts) == len(ends) == 1 and starts[0] < ends[0], f"Missing/ambiguous config block: {path}")
    # Compare every executable command, including material/temperature controls
    # and machine macros, not just endpoints or the total G-code byte count.
    commands = [line.split(";", 1)[0].strip() for line in lines]
    commands = [line for line in commands if line]
    require(commands, f"No executable G-code: {path}")
    layer_markers = sum(line.strip() in (";LAYER_CHANGE", "; CHANGE_LAYER", ";CHANGE_LAYER") for line in lines)
    require(layer_markers > 0, f"No recognized layer markers: {path}")
    declared_layers = [int(match.group(1)) for line in lines
                       if (match := re.match(r";\s*total (?:layer number:|layers count\s*=)\s*(\d+)\s*$", line))]
    require(declared_layers and all(count == layer_markers for count in declared_layers),
            f"Layer markers/header/footer differ: {path}, {layer_markers}, {declared_layers}")
    density_lines = [line.split(":", 1)[1] for line in lines if line.startswith("; filament_density:")]
    require(len(density_lines) == 1, f"Missing/ambiguous filament density metadata: {path}")
    densities = [float(value.strip()) for value in density_lines[0].split(",")]
    require(densities and all(math.isfinite(value) and value > 0 for value in densities),
            f"This material cross-check needs positive configured densities: {path}")
    footer_mass = [float(match.group(1)) for line in lines
                   if (match := re.match(r";\s*total filament used \[g\]\s*=\s*([\d.]+)\s*$", line))]
    require(len(footer_mass) == 1, f"Missing/ambiguous total filament metadata: {path}")
    return {"commands": commands, "configuration": lines[starts[0]:ends[0] + 1], "layers": layer_markers,
            "filament_densities": densities, "footer_mass_g": footer_mass[0],
            "commands_sha256": hashlib.sha256("\n".join(commands).encode()).hexdigest()}


def validate_plain_metrics(value, location="report"):
    if isinstance(value, dict):
        for key, item in value.items():
            require(not (key in QUALITY_CLAIMS and item is True), f"Unsupported print-quality claim: {location}.{key}")
            validate_plain_metrics(item, f"{location}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            validate_plain_metrics(item, f"{location}[{index}]")
    elif isinstance(value, float):
        require(math.isfinite(value), f"Non-finite metric: {location}")


def nonnegative(value, label):
    require(type(value) in (int, float) and math.isfinite(value) and value >= 0, f"Invalid {label}: {value}")
    return value


def count(value, label):
    require(type(value) is int and value >= 0, f"Invalid {label}: {value}")
    return value


def validate_range(value, expected_count, label):
    valid = count(value["count"], label + ".count")
    invalid = count(value["invalid_count"], label + ".invalid_count")
    require(valid + invalid == expected_count, f"Range sample count disagrees: {label}")
    if valid:
        low = nonnegative(value["min"], label + ".min")
        high = nonnegative(value["max"], label + ".max")
        require(low <= high, f"Inverted range: {label}")
    else:
        require(value["min"] is None and value["max"] is None, f"Empty range must use null bounds: {label}")


def validate_estimated_peak_flow(value, configured_cap):
    require(value["scope"] == "normal_estimator_finalized_linear_e_blocks", "Unexpected estimated-peak-flow scope")
    require(type(value["complete"]) is bool, "Estimated-peak-flow completeness must be boolean")
    bounds, ratio = value["peak_volumetric_rate_mm3_s"], value["max_to_configured_cap_ratio"]
    if not value["complete"]:
        require(value["block_count"] is None and bounds["min"] is None and bounds["max"] is None and ratio is None,
                "Incomplete estimated peak flow must not expose partial blocks, ranges or ratio")
    elif count(value["block_count"], "estimated-peak-flow block count") == 0:
        require(bounds["min"] is None and bounds["max"] is None and ratio is None,
                "No finalized extrusion blocks have no estimated-peak-flow range or ratio")
    else:
        low = nonnegative(bounds["min"], "estimated peak flow min")
        high = nonnegative(bounds["max"], "estimated peak flow max")
        require(low <= high, "Inverted estimated-peak-flow range")
        if type(configured_cap) in (int, float) and math.isfinite(configured_cap) and configured_cap > 0:
            expected_ratio = high / configured_cap
            if math.isfinite(expected_ratio):
                require(math.isclose(nonnegative(ratio, "estimated peak flow to cap ratio"), expected_ratio,
                                     rel_tol=1e-9, abs_tol=1e-12), "Estimated peak flow ratio disagrees with this material's cap")
            else:
                require(ratio is None, "Non-finite material cap division must leave estimated peak flow ratio unknown")
        else:
            require(ratio is None, "Unavailable material cap must leave estimated peak flow ratio unknown")


def validate_estimated_flow_cap_distance(value, configured_cap):
    require(value["scope"] == "normal_estimator_finalized_linear_e_block_phases", "Unexpected estimated-flow-cap-distance scope")
    require(type(value["complete"]) is bool, "Estimated-flow-cap-distance completeness must be boolean")
    total = value["participating_path_length_mm"]
    above = value["above_cap_path_length_mm"]
    ratio = value["above_cap_path_length_ratio"]
    if not value["complete"]:
        require(total is None and above is None and ratio is None,
                "Incomplete estimated flow cap distance must not expose partial distances or ratio")
        return
    require(type(configured_cap) in (int, float) and math.isfinite(configured_cap) and configured_cap > 0,
            "Complete estimated flow cap distance needs this material's positive configured cap")
    total = nonnegative(total, "estimated flow cap participating path length")
    above = nonnegative(above, "estimated flow cap above-cap path length")
    require(above <= total, "Estimated above-cap distance exceeds participating distance")
    if total == 0:
        require(ratio is None, "No participating extrusion distance has no estimated above-cap ratio")
    else:
        require(nonnegative(ratio, "estimated flow cap distance ratio") <= 1 and ratio == above / total,
                "Estimated above-cap ratio must use this material's participating distance")


def validate_gcode_observations(metrics):
    require(set(metrics) == CORE_GCODE_FIELDS, "G-code core fields are missing or include retired diagnostics")
    selected = metrics["small_perimeter_selection"]
    require(selected["scope"] == "native_small_perimeter_selection_before_speed_overrides"
            and type(selected["complete"]) is bool, "Unexpected small-perimeter selection scope")
    fields = ("selected_loop_count", "selected_path_count", "selected_path_length_mm")
    speed = selected["requested_speed_mm_s"]
    if selected["complete"]:
        loops = count(selected["selected_loop_count"], "selected small loops")
        paths_count = count(selected["selected_path_count"], "selected small paths")
        length = nonnegative(selected["selected_path_length_mm"], "selected small path length")
        require(paths_count >= loops, "Small-perimeter paths cannot be fewer than loops")
        if loops:
            require(length > 0 and 0 < nonnegative(speed["min"], "small speed") <= nonnegative(speed["max"], "small speed"),
                    "Selected small perimeters need positive length and speed")
        else:
            require(paths_count == 0 and length == 0 and speed["min"] is None and speed["max"] is None,
                    "No selected small perimeters must not invent a speed")
    else:
        require(all(selected[key] is None for key in fields) and speed["min"] is None and speed["max"] is None,
                "Unknown small-perimeter selection must not expose successful counts")
    paths = metrics["quality_paths"]
    require(paths["scope"] == "final_gcode_positive_extrusion_moves" and isinstance(paths["rows"], list),
            "Unexpected quality path scope")
    identities = set()
    for row in paths["rows"]:
        identity = (count(row["extruder_id"], "quality path material"), row["role"])
        require(identity not in identities, "Duplicate material/role quality evidence")
        identities.add(identity)
        require(count(row["move_count"], "quality path moves") > 0 and type(row["complete"]) is bool,
                "Quality evidence must have contributing moves and explicit completeness")
        if row["complete"]:
            require(nonnegative(row["path_length_mm"], "quality path length") > 0, "Empty quality path")
            for key in ("commanded_speed_mm_s", "fan_speed_percent", "nominal_width_mm", "nominal_height_mm"):
                low = nonnegative(row[key]["min"], key)
                high = nonnegative(row[key]["max"], key)
                require(low <= high and (high <= 100 if key == "fan_speed_percent" else low > 0),
                        "Invalid quality path range")
        else:
            require(row["path_length_mm"] is None, "Incomplete path length must remain unknown")
            for key in ("commanded_speed_mm_s", "fan_speed_percent", "nominal_width_mm", "nominal_height_mm"):
                require(row[key]["min"] is None and row[key]["max"] is None, "Incomplete ranges must remain unknown")
        if "width_height_observation" in row:
            validate_width_height_observation(row)
    require(set(metrics["normal_estimate"]) == CORE_NORMAL_FIELDS,
            "Normal estimate must contain only time and the internal cooling sequence count")
    require(isinstance(metrics["extruders"], list) and metrics["extruders"], "Missing material observations")
    for item in metrics["extruders"]:
        require(set(item) == CORE_MATERIAL_FIELDS, "Material core fields are missing or include retired diagnostics")


def validate_plate_bed_footprint(value):
    require(value["scope"] == "planned_path_nominal_bottom_on_bed_before_gcode", "Unexpected plate bed-footprint scope")
    require(type(value["complete"]) is bool, "Bed-footprint completeness must be boolean")
    keys = ("model_area_mm2", "support_and_raft_area_mm2", "brim_area_mm2", "combined_area_mm2")
    require(set(value) == {"scope", "complete", *keys},
            "Bed footprint must contain only scope, completeness and the four existing areas")
    if not value["complete"]:
        require(all(value[key] is None for key in keys), "Incomplete bed footprint must not expose partial areas")
        return
    model, support, brim, combined = [nonnegative(value[key], "bed footprint " + key) for key in keys]
    tolerance = max(1., model + support + brim) * 1e-9
    require(max(model, support, brim) <= combined + tolerance
            and combined <= model + support + brim + tolerance, "Bed footprint union contradicts category areas")


def validate_core_report_group(group):
    require(not REMOVED_GROUP_METRICS.intersection(group),
            "Removed diagnostic groups must not be emitted, even as empty summaries")
    coverage = group["planned_layer_coverage"]
    require(coverage["scope"] == "per_instance_model_paths_before_gcode", "Unexpected footprint scope")
    require(len(coverage["layers"]) == count(group["model_layer_count"], "model_layer_count"),
            "Missing core layer observations")
    for index, layer in enumerate(coverage["layers"]):
        require(set(layer) == CORE_LAYER_FIELDS, "Core layer fields are missing or include retired diagnostics")
        require(type(layer["layer_index"]) is int and layer["layer_index"] == index, "Nonsequential layer index")
        require(type(layer["complete"]) is bool, "Coverage completeness must be boolean")
    if "solid_fill_peak" in coverage:
        validate_solid_fill_peak(coverage["solid_fill_peak"], coverage["layers"])
    for hole, key in ((False, "unprinted_peak"), (True, "hole_intrusion_peak")):
        if key in coverage:
            validate_residual_peak(coverage[key], coverage["layers"], hole)


def validate_width_height_observation(row):
    value = row["width_height_observation"]
    require(value["scope"] == "same_move_nominal_width_height_non_bridge", "Unexpected bead observation scope")
    require(type(value["complete"]) is bool and value["complete"] == row["complete"], "Bead completeness mismatch")
    applicable = row["role"] not in ("bridge", "internal_bridge", "overhang_wall")
    keys = ("below_height_move_count", "below_height_path_length_mm", "min_ratio", "width_at_min_mm", "height_at_min_mm", "print_z_at_min_mm")
    if not row["complete"] or not applicable:
        require(value["applicable"] is (applicable if row["complete"] else None)
                and all(value[key] is None for key in keys), "Unavailable bead observation must remain unknown")
        return
    require(value["applicable"] is True, "Non-bridge bead observation must be applicable")
    width, height, ratio = [nonnegative(value[key], key) for key in ("width_at_min_mm", "height_at_min_mm", "min_ratio")]
    require(width > 0 and height > 0 and ratio > 0 and math.isclose(ratio, width / height, rel_tol=1e-6, abs_tol=1e-9),
            "Bead ratio does not refer to the same move")
    require(row["nominal_width_mm"]["min"] <= width <= row["nominal_width_mm"]["max"]
            and row["nominal_height_mm"]["min"] <= height <= row["nominal_height_mm"]["max"], "Bead pair outside observed ranges")
    moves = count(value["below_height_move_count"], "narrow bead moves")
    length = nonnegative(value["below_height_path_length_mm"], "narrow bead length")
    require(moves <= row["move_count"] and length <= row["path_length_mm"] and (moves > 0) == (ratio < 1)
            and (length > 0) == (moves > 0), "Narrow bead length/count mismatch")
    require(value["print_z_at_min_mm"] is None or (type(value["print_z_at_min_mm"]) in (float, int)
            and math.isfinite(value["print_z_at_min_mm"])), "Invalid bead Z")


def validate_residual_peak(value, layers, hole):
    scope = "largest_hole_intrusion_layer_exclusive_path_kinds" if hole else "largest_unprinted_layer_region_slices"
    total = "intrusion_area_mm2" if hole else "unprinted_area_mm2"
    layer_key = "hole_intrusion_area_mm2" if hole else "completely_unprinted_island_area_mm2"
    keys = ("wall_area_mm2", "gap_fill_area_mm2", "bridge_area_mm2", "other_fill_area_mm2", "mixed_area_mm2") if hole else (
        "retained_region_area_mm2", "absent_region_area_mm2")
    require(value["scope"] == scope, "Unexpected residual peak scope")
    complete = bool(layers) and all(layer["complete"] for layer in layers)
    require(type(value["complete"]) is bool and value["complete"] == complete, "Residual peak completeness mismatch")
    if not complete:
        require(all(value[k] is None for k in (*keys, total, "layer_index", "print_z_mm")), "Partial residual cannot be certified")
        return
    areas = [nonnegative(layer[layer_key], layer_key) for layer in layers]
    maximum = nonnegative(value[total], total)
    pieces = [nonnegative(value[k], k) for k in keys]
    require(maximum == max(areas) and math.isclose(sum(pieces), maximum, abs_tol=1e-6, rel_tol=1e-6), "Residual partition mismatch")
    if maximum == 0:
        require(all(p == 0 for p in pieces) and value["layer_index"] is None and value["print_z_mm"] is None, "Empty residual has a location")
    else:
        index = count(value["layer_index"], "residual peak index")
        require(index == areas.index(maximum) and value["print_z_mm"] == layers[index]["print_z_mm"], "Residual peak identity mismatch")


def validate_solid_fill_peak(value, layers):
    require(value["scope"] == "largest_solid_uncovered_layer_exclusive_target_kinds", "Unexpected solid gap peak scope")
    complete = bool(layers) and all(layer["complete"] for layer in layers)
    require(type(value["complete"]) is bool and value["complete"] == complete, "Solid gap maximum completeness disagrees")
    keys = ("surface_area_mm2", "internal_solid_area_mm2", "bridge_area_mm2", "mixed_area_mm2")
    if not complete:
        require(all(value[key] is None for key in (*keys, "layer_index", "print_z_mm", "uncovered_area_mm2")),
                "Incomplete maximum cannot expose a partial attribution")
        return
    areas = [nonnegative(layer["solid_fill_uncovered_area_mm2"], "solid gap") for layer in layers]
    maximum = nonnegative(value["uncovered_area_mm2"], "solid gap maximum")
    require(maximum == max(areas), "Solid gap attribution is not this group's maximum")
    pieces = [nonnegative(value[key], key) for key in keys]
    require(math.isclose(sum(pieces), maximum, rel_tol=1e-6, abs_tol=1e-6), "Exclusive gap categories do not sum to total")
    if maximum == 0:
        require(value["layer_index"] is None and value["print_z_mm"] is None and all(piece == 0 for piece in pieces),
                "Fully covered targets cannot invent a problem layer")
    else:
        index = count(value["layer_index"], "solid gap peak index")
        require(index < len(layers) and index == areas.index(maximum), "Solid gap maximum must use the first peak layer")
        require(value["print_z_mm"] == layers[index]["print_z_mm"], "Solid gap maximum Z mismatch")


def validate_layer_time_threshold_observation(value, sequence_count):
    require(value["scope"] == "final_gcode_layer_sequences_per_material", "Unexpected layer-time observation scope")
    require(type(value["complete"]) is bool, "Layer-time observation completeness must be boolean")
    enabled, threshold = value["slow_down_enabled"], value["configured_threshold_s"]
    require(enabled is None or type(enabled) is bool, "Cooling slow-down setting must be boolean or unknown")
    if threshold is not None:
        nonnegative(threshold, "configured layer-time threshold")
    applicable = value["applicable"]
    active = value["active_layer_sequence_count"]
    below = value["below_threshold_layer_sequence_count"]
    ratio = value["below_threshold_layer_ratio"]
    if not value["complete"]:
        require(applicable is None and active is None and below is None and ratio is None,
                "Incomplete layer-time observation must not expose partial counts or applicability")
        return
    require(type(enabled) is bool and threshold is not None, "Complete observation needs the existing material settings")
    require(count(active, "active layer sequence count") <= count(sequence_count, "layer sequence count"),
            "Active material sequences exceed the final G-code sequence count")
    require(type(applicable) is bool and applicable == (enabled and threshold > 0 and active > 0),
            "Layer-time applicability disagrees with the material setting or active sequences")
    if not applicable:
        require(below is None and ratio is None, "Inapplicable layer-time observation must not imply zero below-threshold layers")
    else:
        require(count(below, "below-threshold layer sequence count") <= active, "Below-threshold sequences exceed active sequences")
        require(nonnegative(ratio, "below-threshold layer ratio") <= 1
                and math.isclose(ratio, below / active, rel_tol=1e-9, abs_tol=1e-12),
                "Layer-time ratio must use this material's distinct active sequences")


def validate_metric_values(report, plate, facts):
    validate_plate_bed_footprint(report["planned_plate_bed_footprint"])
    objects = report["objects"]
    groups = objects["groups"]
    require(count(objects["slice_object_group_count"], "slice_object_group_count") == len(groups) > 0,
            "Slice object group count disagrees")
    require(objects["source_object_count"] == len({group["source_object_id"] for group in groups}),
            "Source object count disagrees")
    require(objects["instance_count"] == sum(count(group["instance_count"], "instance_count") for group in groups),
            "Instance count disagrees")
    for index, group in enumerate(groups):
        require(group["group_index"] == index, "Nonsequential slice group index")
        layers = count(group["model_layer_count"], "model_layer_count")
        validate_core_report_group(group)
        count(group["support_layer_count"], "support_layer_count")
        validate_range(group["model_layer_height_mm"], layers, "model_layer_height_mm")
        first_section = group["first_model_layer_section_area_mm2_per_instance"]
        if first_section is not None:
            nonnegative(first_section, "first_model_layer_section_area_mm2_per_instance")

    metrics = report["gcode"]
    validate_gcode_observations(metrics)
    normal = metrics["normal_estimate"]
    require(abs(nonnegative(normal["total_time_s"], "total_time_s") - plate["total_time"]) <= 0.5001,
            "Estimated time differs from rounded legacy plate time")
    sequence_count = count(normal["layer_sequence_count"], "layer_sequence_count")
    require(sequence_count == facts["layers"], "Estimated layer sequence count differs from G-code layer markers")
    extruders = metrics["extruders"]
    ids = [count(item["extruder_id"], "extruder_id") for item in extruders]
    require(len(set(ids)) == len(ids), "Duplicate extruder metrics")
    total_mass, support_mass = 0., 0.
    total_volume_entries = 0
    for item in extruders:
        validate_estimated_peak_flow(item["estimated_peak_flow"], item["configured_filament_max_volumetric_speed_mm3_s"])
        validate_estimated_flow_cap_distance(item["estimated_flow_cap_distance"], item["configured_filament_max_volumetric_speed_mm3_s"])
        validate_layer_time_threshold_observation(item["layer_time_threshold_observation"], sequence_count)
        cap = item["configured_filament_max_volumetric_speed_mm3_s"]
        if cap is not None:
            require(nonnegative(cap, "configured material flow cap") > 0, "Unlimited cap must be null")
        index = item["extruder_id"]
        require(index < len(facts["filament_densities"]), "Extruder has no G-code density metadata")
        volumes = item["estimated_volume_mm3"]
        for key in ("total", "other", "support", "wipe_tower", "flush"):
            if volumes[key] is not None:
                nonnegative(volumes[key], "estimated_volume_mm3." + key)
        # Missing map entries stay null in the report. Summation below follows
        # the legacy volume-map totals; it does not rewrite unknowns as zeros.
        if volumes["total"] is not None:
            total_volume_entries += 1
            total_mass += volumes["total"] * facts["filament_densities"][index] * 0.001
        if volumes["support"] is not None:
            support_mass += volumes["support"] * facts["filament_densities"][index] * 0.001
    require(total_volume_entries > 0, "No recorded material volume")
    # Legacy JSON and G-code footer weights are rounded to 0.01 g.
    require(abs(total_mass - plate["total_filament_g"]) <= 0.0051, "Volume/density disagrees with legacy total material weight")
    require(abs(total_mass - facts["footer_mass_g"]) <= 0.0051, "Volume/density disagrees with G-code material footer")
    require(abs(support_mass - plate["support_filament_g"]) <= 0.0051, "Volume/density disagrees with legacy support weight")
    require(isinstance(report["limitations"], list) and bool(report["limitations"]), "Missing analysis limitations")


def validate_report(report, output, plate, facts):
    require(type(report.get("schema_version")) is int and report["schema_version"] == 1, "Wrong analysis schema_version")
    require(report.get("scope") == "sliced_print_metrics", "Wrong analysis scope")
    require(type(report.get("plate_index")) is int and report["plate_index"] == plate["id"],
            "Analysis does not identify its sliced plate")
    for group in report["objects"]["groups"]:
        validate_core_report_group(group)
    name = report.get("gcode_file")
    require(isinstance(name, str) and bool(name), "Missing gcode_file")
    foreign = PureWindowsPath(name)
    require(not Path(name).is_absolute() and not foreign.is_absolute() and not foreign.drive
            and ".." not in foreign.parts, "gcode_file must remain inside this output directory")
    gcode = (output / name).resolve(strict=True)
    require(gcode.is_relative_to(output.resolve()) and gcode.is_file(), "gcode_file escapes output or is not a file")
    require(gcode == facts["path"], "Analysis points at a different G-code file")
    validate_plain_metrics(report)
    validate_metric_values(report, plate, facts)


class Suite:
    def __init__(self, args, output):
        self.args, self.output = args, output
        self.results = []
        self.baseline = None
        self.optimized_baseline = None

    def run_process(self, command, case, cwd):
        save_json(case / "command.json", command)
        with (case / "stdout.log").open("wb") as stdout, (case / "stderr.log").open("wb") as stderr:
            with subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr,
                                  start_new_session=os.name != "nt") as process:
                try:
                    return process.wait(timeout=self.args.timeout)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        process.kill()
                    else:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    process.wait(timeout=10)
                    raise

    def run(self, name, *, flags=(), slicing=True, output_arg=True, success=True,
            report_expected=False, existing_report=False, source=None, help_only=False, optimization=False):
        case = self.output / name
        case.mkdir()
        cwd, data, output = (case / child for child in ("working-directory", "data", "output"))
        for directory in (cwd, data, output):
            directory.mkdir()
        sentinel = b'{"sentinel":"existing report must not be replaced"}\n'
        protected_report = None
        if existing_report:
            plate_id = self.baseline["plates"][0]["id"] if self.baseline else 1
            protected_report = output / f"plate_{plate_id}.analysis.json"
            protected_report.write_bytes(sentinel)
        command = [str(self.args.binary), "--datadir", str(data), "--debug", "1"]
        if help_only:
            command.append("--help")
        else:
            if slicing:
                command.extend(("--load-settings", f"{self.args.machine};{self.args.process}",
                                "--load-filaments", str(self.args.filament),
                                "--arrange", "1", "--ensure-on-bed", "--slice", "0"))
            if output_arg:
                command.extend(("--outputdir", str(output)))
            command.extend(flags)
            if slicing:
                command.append(str(source if source is not None else self.args.model))
        record = {"case": name, "passed": False}
        started = time.monotonic()
        try:
            code = self.run_process(command, case, cwd)
            record["exit_code"] = code
            require(code >= 0 and not 0xC0000000 <= code <= 0xCFFFFFFF,
                    f"CLI crashed instead of returning a controlled result: {code}")
            require((code == 0) == success, f"Unexpected exit code: {code}")
            published = set(case.rglob("*.analysis.json"))
            if protected_report:
                require(protected_report.read_bytes() == sentinel, "Existing analysis report was overwritten")
                require(published == {protected_report}, "Rejected invocation published another analysis report")
            elif not report_expected:
                require(not published, "Opt-out/rejected invocation published an analysis report")
            if help_only:
                help_text = (case / "stdout.log").read_text(errors="replace") + (case / "stderr.log").read_text(errors="replace")
                require("slice-analysis-report" in help_text, "Help does not describe the new option")
                require("optimize-model-detail-paths" in help_text, "Help does not describe the independent optimization option")
            elif success and slicing:
                legacy = read_json(output / "result.json")
                require(legacy.get("return_code") == 0, "Legacy result did not succeed")
                plates = legacy.get("sliced_plates")
                require(isinstance(plates, list) and bool(plates), "No sliced plates in result.json")
                plate_ids = [plate["id"] for plate in plates]
                require(all(type(index) is int and index > 0 for index in plate_ids)
                        and len(set(plate_ids)) == len(plate_ids), "Invalid/duplicate sliced plate IDs")
                expected = {output / f"plate_{index}.analysis.json" for index in plate_ids}
                if report_expected:
                    require(published == expected, "Analysis reports do not match successful sliced plates")
                gcode = {}
                for plate in plates:
                    index = plate["id"]
                    path = output / f"plate_{index}.gcode"
                    facts = gcode_facts(path)
                    facts["path"] = path.resolve()
                    gcode[index] = facts
                    if report_expected:
                        validate_report(read_json(output / f"plate_{index}.analysis.json"), output, plate, facts)
                comparable = stable_legacy(legacy)
                if name == "default-off":
                    self.baseline = {"legacy": comparable, "gcode": gcode, "plates": plates}
                else:
                    require(self.baseline is not None, "Control case failed; cannot establish unchanged slicing")
                    require(gcode.keys() == self.baseline["gcode"].keys(), "Sliced plate set changed")
                    for index, facts in gcode.items():
                        original = self.baseline["gcode"][index]
                        require(facts["configuration"] == original["configuration"], f"Effective configuration changed for plate {index}")
                        require(facts["layers"] == original["layers"], f"Layer count changed for plate {index}")
                    if optimization and not report_expected:
                        self.optimized_baseline = {"legacy": comparable, "gcode": gcode, "plates": plates}
                    else:
                        baseline = self.optimized_baseline if optimization else self.baseline
                        require(baseline is not None, "Optimization control case failed; cannot compare report toggle")
                        require(comparable == baseline["legacy"], "Legacy result fields/values changed within the same optimization mode")
                        for index, facts in gcode.items():
                            require(facts["commands"] == baseline["gcode"][index]["commands"],
                                    f"Executable G-code changed within the same optimization mode for plate {index}")
                record["plates"] = [{"id": index, "layers": facts["layers"],
                                     "commands_sha256": facts["commands_sha256"]} for index, facts in gcode.items()]
                require(not (cwd / "result.json").exists(), "Successful slice wrote result.json outside outputdir")
            record["passed"] = True
        except Exception as error:
            record["error"] = f"{type(error).__name__}: {error}"
        record["elapsed_seconds"] = round(time.monotonic() - started, 3)
        self.results.append(record)
        save_json(case / "test-result.json", record)
        print(f"{'PASS' if record['passed'] else 'FAIL'} {name}" +
              (f": {record['error']}" if "error" in record else ""), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="New evidence directory; parent must exist")
    parser.add_argument("--model", type=Path, default=ASSETS / "models/stl/cube.stl")
    parser.add_argument("--machine", type=Path, default=ASSETS / "profiles/machine/ep7_0.4.json")
    parser.add_argument("--process", type=Path, default=ASSETS / "profiles/process/ep7_standard_0.20.json")
    parser.add_argument("--filament", type=Path, default=ASSETS / "profiles/filament/generic_pla_0.4.json")
    parser.add_argument("--timeout", type=int, default=180, help="Per-process timeout in seconds (1..1800)")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 1800:
        parser.error("--timeout must be between 1 and 1800 seconds")
    for name in ("binary", "model", "machine", "process", "filament"):
        path = getattr(args, name).expanduser().resolve()
        if not path.is_file():
            parser.error(f"Missing local {name}: {path}; provide --{name}. Nothing will be downloaded or built.")
        setattr(args, name, path)
    output = args.output_dir.expanduser().absolute()
    if output.exists() or output.is_symlink():
        parser.error(f"--output-dir must not already exist: {output}")
    if not output.parent.is_dir():
        parser.error(f"--output-dir parent does not exist: {output.parent}")
    protected = {getattr(args, name): digest(getattr(args, name))
                 for name in ("binary", "model", "machine", "process", "filament")}
    repository_result = ROOT / "result.json"
    result_before = digest(repository_result) if repository_result.is_file() else None
    output.mkdir()
    output = output.resolve()
    suite = Suite(args, output)
    suite.run("help", help_only=True, slicing=False)
    suite.run("default-off")
    suite.run("explicit-off", flags=("--slice-analysis-report", "0"))
    suite.run("enabled", flags=("--slice-analysis-report",), report_expected=True)
    suite.run("optimization-explicit-off", flags=("--optimize-model-detail-paths", "0"))
    suite.run("optimization-off-report-on", flags=("--optimize-model-detail-paths", "0", "--slice-analysis-report"), report_expected=True)
    # Core corrections may change paths even on simple geometry. Compare the
    # report toggle within each mode, while keeping effective configs identical.
    suite.run("optimization-on-report-off", flags=("--optimize-model-detail-paths",), optimization=True)
    suite.run("optimization-on-report-on", flags=("--optimize-model-detail-paths", "--slice-analysis-report"),
              report_expected=True, optimization=True)
    suite.run("reject-optimization-without-slice", slicing=False, flags=("--optimize-model-detail-paths",), success=False)
    suite.run("reject-optimization-load-cache", flags=("--optimize-model-detail-paths", "--load-slicedata", str(output / "cache")), success=False)
    suite.run("reject-optimization-export-cache", flags=("--optimize-model-detail-paths", "--export-slicedata", str(output / "cache")), success=False)
    suite.run("reject-flag-only", slicing=False, output_arg=False,
              flags=("--slice-analysis-report",), success=False)
    suite.run("reject-without-slice", slicing=False, flags=("--slice-analysis-report",), success=False)
    suite.run("reject-no-outputdir", output_arg=False, flags=("--slice-analysis-report",), success=False)
    suite.run("reject-empty-outputdir", output_arg=False,
              flags=("--slice-analysis-report", "--outputdir", ""), success=False)
    suite.run("reject-existing-report", flags=("--slice-analysis-report",), existing_report=True, success=False)
    suite.run("reject-missing-input", flags=("--slice-analysis-report",),
              source=output / "does-not-exist.stl", success=False)
    integrity_errors = []
    for path, expected in protected.items():
        if not path.is_file() or digest(path) != expected:
            integrity_errors.append(f"Protected input changed: {path}")
    result_after = digest(repository_result) if repository_result.is_file() else None
    if result_before != result_after:
        integrity_errors.append(f"Repository result.json changed: {repository_result}")
    passed = not integrity_errors and all(result["passed"] for result in suite.results)
    save_json(output / "summary.json", {
        "test": "cli_slice_analysis", "passed": passed, "created_at": datetime.now(timezone.utc).isoformat(),
        "binary": str(args.binary), "binary_sha256": protected[args.binary],
        "input_sha256": {str(path): value for path, value in protected.items()},
        "timeout_seconds": args.timeout, "cases": suite.results, "integrity_errors": integrity_errors,
        "comparison": {"legacy_ignored_wall_clock_fields": sorted(WALL_CLOCK_FIELDS),
                       "gcode": "all executable commands within each optimization mode; complete CONFIG_BLOCK across modes; comment timestamps excluded"},
        "scope": {"built": False, "downloaded": False, "print_sent": False,
                  "gui_verified": False, "physical_quality_verified": False},
    })
    for error in integrity_errors:
        print(f"FAIL integrity: {error}", file=sys.stderr)
    print(f"Evidence: {output}", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
