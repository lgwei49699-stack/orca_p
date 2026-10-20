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


def validate_extrusion_ranges(ranges, expected_count, label):
    for key in ("width_mm", "height_mm", "commanded_feedrate_mm_s", "nominal_volumetric_rate_mm3_s", "segment_length_mm"):
        validate_range(ranges[key], expected_count, label + "." + key)
    lengths = ranges["segment_length_mm"]
    if lengths["count"] > 0 and lengths["invalid_count"] == 0:
        nonnegative(ranges["path_length_mm"], label + ".path_length_mm")
    else:
        require(ranges["path_length_mm"] is None, label + ".path_length_mm must preserve unknowns")


def validate_estimated_motion(value):
    require(value["scope"] == "normal_estimator_finalized_extrusion_blocks", "Unexpected estimated-motion scope")
    require(type(value["complete"]) is bool, "Estimated-motion completeness must be boolean")
    ranges = (value["peak_speed_mm_s"], value["tangential_acceleration_mm_s2"])
    if not value["complete"]:
        require(value["block_count"] is None and all(item["min"] is None and item["max"] is None for item in ranges),
                "Incomplete estimated motion must not expose partial blocks or ranges")
    elif count(value["block_count"], "estimated-motion block count") == 0:
        require(all(item["min"] is None and item["max"] is None for item in ranges),
                "No finalized extrusion blocks have no estimated-motion ranges")
    else:
        for item in ranges:
            low, high = item["min"], item["max"]
            require(type(low) in (int, float) and type(high) in (int, float)
                    and math.isfinite(low) and math.isfinite(high) and low <= high,
                    "Invalid estimated-motion range")
        require(ranges[0]["min"] > 0, "Estimated peak speed must be positive")


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
    travel = metrics["travel"]
    lengths = travel["segment_length_mm"]
    validate_range(lengths, metrics["move_counts"]["travel"], "travel.segment_length_mm")
    if lengths["invalid_count"]:
        require(travel["path_length_mm"] is None, "Invalid travel must leave total unknown")
    else:
        total = nonnegative(travel["path_length_mm"], "travel.path_length_mm")
        if lengths["count"] == 0:
            require(total == 0, "No travel records must have zero total length")
        else:
            require(lengths["min"] * lengths["count"] <= total + 1e-6
                    and total <= lengths["max"] * lengths["count"] + 1e-6, "Travel total outside its recorded range")
    bounds = metrics["seam_position_bounds_mm"]
    require(type(bounds["complete"]) is bool, "Seam completeness must be boolean")
    if not bounds["complete"] or metrics["seam_record_count"] == 0:
        require(bounds["min"] is None and bounds["max"] is None, "Absent/invalid seams must have no bounds")
    else:
        require(isinstance(bounds["min"], list) and isinstance(bounds["max"], list)
                and len(bounds["min"]) == len(bounds["max"]) == 3, "Seam bounds must contain XYZ")
        for low, high in zip(bounds["min"], bounds["max"]):
            require(type(low) in (int, float) and type(high) in (int, float)
                    and math.isfinite(low) and math.isfinite(high) and low <= high, "Invalid signed seam bounds")
    seams = metrics["seam_positions_mm"]
    require(seams["scope"] == "gcode_processor_preview_coordinates_in_move_order", "Unexpected seam coordinate scope")
    require(type(seams["complete"]) is bool and seams["complete"] == bounds["complete"],
            "Seam point completeness disagrees with bounds")
    if not seams["complete"]:
        require(seams["positions"] is None, "Incomplete seam positions must not expose a partial list")
    else:
        positions = seams["positions"]
        require(isinstance(positions, list) and len(positions) == metrics["seam_record_count"], "Seam positions count disagrees")
        require(all(isinstance(point, list) and len(point) == 3
                    and all(type(value) in (int, float) and math.isfinite(value) for value in point)
                    for point in positions), "Invalid seam XYZ coordinates")
        if positions:
            require([min(point[axis] for point in positions) for axis in range(3)] == bounds["min"]
                    and [max(point[axis] for point in positions) for axis in range(3)] == bounds["max"],
                    "Seam positions disagree with bounds")
    for role in metrics["extrusion_by_role"]:
        validate_estimated_motion(role["estimated_motion"])
        flow = role["configured_flow_comparison"]
        require(type(flow["complete"]) is bool, "Flow comparison completeness must be boolean")
        above, ratio = flow["above_cap_path_length_mm"], flow["above_cap_path_length_ratio"]
        if not flow["complete"]:
            require(above is None and ratio is None, "Incomplete flow comparison must not expose partial totals")
            continue
        total = nonnegative(role["extrusion_ranges"]["path_length_mm"], "role path length")
        require(nonnegative(above, "above cap length") <= total, "Above-cap length exceeds total role length")
        if total == 0:
            require(ratio is None, "Zero extrusion length has no flow length ratio")
        else:
            require(nonnegative(ratio, "above cap length ratio") <= 1
                    and math.isclose(ratio, above / total, rel_tol=1e-9, abs_tol=1e-12),
                    "Flow ratio must be length-weighted, not record-weighted")


def validate_plate_bed_regions(value):
    categories = ("model", "support_and_raft", "brim", "combined")
    if not any(category + "_regions" in value for category in categories):
        return  # Older reports predate the optional connected-region observation.
    for category in categories:
        regions = value[category + "_regions"]
        if not value["complete"]:
            require(regions is None, "Incomplete bed footprint must not expose partial connected regions")
            continue
        require(isinstance(regions, list), "Complete bed-footprint regions must be an array")
        total = nonnegative(value[category + "_area_mm2"], category + " bed-footprint area")
        require(bool(regions) == (total > 0), "Bed-footprint region presence disagrees with the category area")
        for region in regions:
            require(nonnegative(region["area_mm2"], "bed-footprint region area") > 0,
                    "Connected bed-footprint regions must have positive area")
            low, high = region["bounds_mm"]["min"], region["bounds_mm"]["max"]
            require(isinstance(low, list) and isinstance(high, list) and len(low) == len(high) == 2,
                    "Bed-footprint region bounds must contain XY")
            require(all(type(a) in (int, float) and type(b) in (int, float)
                        and math.isfinite(a) and math.isfinite(b) and a < b for a, b in zip(low, high)),
                    "Invalid signed bed-footprint region bounds")
    # Region order is not an identity, and reordering area additions can alter
    # their last floating-point bits. Do not require a bit-identical area sum.


def validate_plate_bed_footprint(value):
    require(value["scope"] == "planned_path_nominal_bottom_on_bed_before_gcode", "Unexpected plate bed-footprint scope")
    require(type(value["complete"]) is bool, "Bed-footprint completeness must be boolean")
    validate_plate_bed_regions(value)
    keys = ("model_area_mm2", "support_and_raft_area_mm2", "brim_area_mm2", "combined_area_mm2")
    if not value["complete"]:
        require(all(value[key] is None for key in keys), "Incomplete bed footprint must not expose partial areas")
        return
    model, support, brim, combined = [nonnegative(value[key], "bed footprint " + key) for key in keys]
    tolerance = max(1., model + support + brim) * 1e-9
    require(max(model, support, brim) <= combined + tolerance
            and combined <= model + support + brim + tolerance, "Bed footprint union contradicts category areas")


def validate_perimeter_loops(value):
    require(value["scope"] == "per_instance_closed_model_perimeter_centerlines_before_gcode", "Unexpected loop scope")
    require(type(value["complete"]) is bool, "Loop completeness must be boolean")
    ranges = (value["loop_length_mm"], value["turn_angle_degrees"])
    if not value["complete"]:
        require(value["loop_count"] is None and all(item["min"] is None and item["max"] is None for item in ranges),
                "Incomplete loops must not expose partial totals")
    elif count(value["loop_count"], "loop_count") == 0:
        require(all(item["min"] is None and item["max"] is None for item in ranges), "Empty loops have no range")
    else:
        for item in ranges:
            require(nonnegative(item["min"], "loop range min") <= nonnegative(item["max"], "loop range max"),
                    "Inverted loop range")
        require(ranges[0]["min"] > 0 and ranges[1]["max"] <= 180, "Invalid loop length or directional turn")


def validate_solid_fill_target_depth(value, model_layer_count):
    require(value["scope"] == "per_instance_continuous_solid_fill_surface_slabs", "Unexpected solid-fill depth scope")
    require(type(value["complete"]) is bool, "Solid-fill depth completeness must be boolean")
    require(nonnegative(value["z_resolution_mm"], "solid-fill depth Z resolution") > 0,
            "Solid-fill depth Z resolution must be positive")
    if count(model_layer_count, "model_layer_count") == 0:
        require(not value["complete"], "No retained model layers must leave solid-fill depth unknown")
    for side in ("top", "bottom"):
        target = value[side]
        area, depth = target["target_area_sum_mm2"], target["continuous_depth_mm"]
        if not value["complete"]:
            require(area is None and depth["min"] is None and depth["max"] is None,
                    "Incomplete solid-fill depth must not expose partial targets or ranges")
        elif nonnegative(area, side + " target area sum") == 0:
            require(depth["min"] is None and depth["max"] is None, "No solid-fill targets have no depth range")
        else:
            low = nonnegative(depth["min"], side + " continuous depth min")
            high = nonnegative(depth["max"], side + " continuous depth max")
            require(0 < low <= high, "Solid-fill target depth must be positive and ordered")


def validate_planned_material_depth(value, model_layer_count):
    require(value["scope"] == "per_instance_fixed_xy_model_path_slabs_before_gcode", "Unexpected planned-material depth scope")
    require(type(value["complete"]) is bool, "Planned-material depth completeness must be boolean")
    require(nonnegative(value["z_resolution_mm"], "planned-material depth Z resolution") > 0,
            "Planned-material depth Z resolution must be positive")
    if count(model_layer_count, "model_layer_count") == 0:
        require(not value["complete"], "No retained model layers must leave planned-material depth unknown")
    for side in ("top", "bottom"):
        target = value[side]
        area, uncovered = target["target_area_sum_mm2"], target["uncovered_start_area_sum_mm2"]
        depth = target["continuous_depth_mm"]
        if not value["complete"]:
            require(area is None and uncovered is None and depth["min"] is None and depth["max"] is None,
                    "Incomplete planned-material depth must not expose partial targets, uncovered areas or ranges")
            continue
        nonnegative(area, side + " planned-material target area sum")
        require(nonnegative(uncovered, side + " uncovered start area sum") <= area,
                "Uncovered start area exceeds the planned-material target area")
        if area == 0:
            require(depth["min"] is None and depth["max"] is None, "No planned-material targets have no depth range")
            continue
        low = nonnegative(depth["min"], side + " planned-material continuous depth min")
        high = nonnegative(depth["max"], side + " planned-material continuous depth max")
        require(low <= high, "Planned-material depth range must be ordered")
        if uncovered > 0:
            require(low == 0, "Uncovered target area must contribute a known zero minimum depth")
        if uncovered == area:
            require(high == 0, "Entirely uncovered targets must have zero minimum and maximum depth")


def validate_nominal_target_path_connectivity(value, model_layer_count):
    require(value["scope"] == "per_instance_compensated_region_section_and_model_path_slabs",
            "Unexpected nominal connectivity scope")
    require(value["coordinate_frame"] == "print_object_local_xy_and_print_z",
            "Unexpected nominal connectivity coordinate frame")
    require(type(value["complete"]) is bool, "Nominal connectivity completeness must be boolean")
    require(nonnegative(value["z_resolution_mm"], "nominal connectivity Z resolution") == 1e-6,
            "Nominal connectivity Z resolution must be exactly 0.000001 mm")
    if count(model_layer_count, "model_layer_count") == 0:
        require(not value["complete"], "No retained model layers must leave nominal connectivity unknown")
    components, joins = value["target_components"], value["cross_target_joins"]
    if not value["complete"]:
        require(components is None and joins is None, "Incomplete nominal connectivity must not expose partial arrays")
        return
    require(isinstance(components, list) and isinstance(joins, list), "Complete nominal connectivity requires arrays")

    def bounds_3d(bounds):
        low, high = bounds["min"], bounds["max"]
        require(isinstance(low, list) and isinstance(high, list) and len(low) == len(high) == 3,
                "Nominal connectivity bounds must have three XYZ coordinates")
        require(all(type(item) in (int, float) and math.isfinite(item) for item in low + high),
                "Nominal connectivity bounds must be finite numeric coordinates")
        require(all(a < b for a, b in zip(low, high)), "Nominal connectivity bounds must have positive XYZ extents")
        return low, high

    for index, component in enumerate(components):
        require(count(component["component_index"], "target component index") == index,
                "Nonsequential nominal target component index")
        low, high = bounds_3d(component["bounds_mm"])
        volume = nonnegative(component["target_volume_mm3"], "nominal target volume")
        require(volume > 0, "Nominal target volume must be positive")
        uncovered = nonnegative(component["uncovered_volume_mm3"], "nominal uncovered volume")
        require(uncovered <= volume, "Nominal uncovered volume exceeds target volume")
        count(component["clipped_path_component_count"], "clipped path component count")
        missing = component["missing_slab_regions"]
        require(isinstance(missing, list) and bool(missing) == (uncovered > 0),
                "Missing slab regions disagree with nominal uncovered volume")
        for region in missing:
            require(nonnegative(region["volume_mm3"], "missing slab region volume") > 0,
                    "Missing slab region volume must be positive")
            region_low, region_high = bounds_3d(region["bounds_mm"])
            require(all(a <= b <= c <= d for a, b, c, d in zip(low, region_low, region_high, high)),
                    "Missing slab region bounds exceed their target component bounds")
        # Volumes may accumulate in different orders; do not impose bitwise
        # equality with the region sum or infer filled volume from bounding boxes.

    path_indices = set()
    for join in joins:
        path_index = count(join["path_component_index"], "joined path component index")
        require(path_index not in path_indices, "Duplicate joined path component index")
        path_indices.add(path_index)
        targets = join["target_component_indices"]
        require(isinstance(targets, list) and len(targets) >= 2, "A cross-target join needs at least two targets")
        for index in targets:
            require(count(index, "joined target component index") < len(components), "Joined target component is out of range")
            require(components[index]["clipped_path_component_count"] > 0,
                    "A joined target must contain a clipped path component")
        require(len(set(targets)) == len(targets), "Duplicate target reference in cross-target join")


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
        validate_perimeter_loops(group["planned_perimeter_loops"])
        require(group["group_index"] == index, "Nonsequential slice group index")
        layers = count(group["model_layer_count"], "model_layer_count")
        validate_solid_fill_target_depth(group["solid_fill_target_depth"], layers)
        validate_planned_material_depth(group["planned_material_depth"], layers)
        if "nominal_target_path_connectivity" in group:
            validate_nominal_target_path_connectivity(group["nominal_target_path_connectivity"], layers)
        count(group["support_layer_count"], "support_layer_count")
        validate_range(group["model_layer_height_mm"], layers, "model_layer_height_mm")
        footprint = group["planned_model_bed_footprint_area_mm2_per_instance"]
        if footprint is not None:
            nonnegative(footprint, "planned_model_bed_footprint_area_mm2_per_instance")

    metrics = report["gcode"]
    validate_gcode_observations(metrics)
    normal = metrics["normal_estimate"]
    require(abs(nonnegative(normal["total_time_s"], "total_time_s") - plate["total_time"]) <= 0.5001,
            "Estimated time differs from rounded legacy plate time")
    nonnegative(normal["prepare_time_s"], "prepare_time_s")
    sequence_count = count(normal["layer_sequence_count"], "layer_sequence_count")
    require(sequence_count == facts["layers"], "Estimated layer sequence count differs from G-code layer markers")
    validate_range(normal["layer_duration_s"], sequence_count, "layer_duration_s")
    require(len(normal["layer_duration_sequence_s"]) == sequence_count, "Layer time sequence length disagrees")
    for duration in normal["layer_duration_sequence_s"]:
        if duration is not None:
            nonnegative(duration, "layer duration")
    require(count(metrics["seam_record_count"], "seam_record_count") <= metrics["move_counts"]["other"],
            "Seam records must remain a subset of the legacy other count")
    moves = metrics["move_counts"]
    require(count(moves["total"], "move_counts.total") == sum(
        count(moves[key], "move_counts." + key) for key in ("extrude", "travel", "retract", "unretract", "wipe", "other")),
        "Move counts do not add up")
    require(moves["extrude"] > 0, "Successful slice has no reported extrusion records")
    validate_extrusion_ranges(metrics["extrusion_ranges"], moves["extrude"], "extrusion_ranges")
    by_role = metrics["extrusion_by_role"]
    require(isinstance(by_role, list) and bool(by_role), "Missing per-role extrusion ranges")
    role_ids = [count(item["role_id"], "extrusion_by_role.role_id") for item in by_role]
    require(len(set(role_ids)) == len(role_ids), "Duplicate extrusion role metrics")
    role_records = 0
    for item in by_role:
        require(isinstance(item["role"], str) and bool(item["role"]), "Missing extrusion role name")
        ranges = item["extrusion_ranges"]
        records = (count(ranges["width_mm"]["count"], "role.width.count")
                   + count(ranges["width_mm"]["invalid_count"], "role.width.invalid_count"))
        validate_extrusion_ranges(ranges, records, "extrusion_by_role." + item["role"])
        role_records += records
    require(role_records == moves["extrude"], "Per-role extrusion counts disagree with total Extrude records")
    # Model walls/fill and custom priming retain separate named role records.
    # Do not require custom extrusion in every input, or constrain its inferred
    # height to model layer heights (even for the default cube fixture).
    extruders = metrics["extruders"]
    ids = [count(item["extruder_id"], "extruder_id") for item in extruders]
    require(len(set(ids)) == len(ids), "Duplicate extruder metrics")
    extrusion_count = 0
    total_mass, support_mass = 0., 0.
    total_volume_entries = 0
    for item in extruders:
        validate_estimated_peak_flow(item["estimated_peak_flow"], item["configured_filament_max_volumetric_speed_mm3_s"])
        validate_estimated_flow_cap_distance(item["estimated_flow_cap_distance"], item["configured_filament_max_volumetric_speed_mm3_s"])
        validate_layer_time_threshold_observation(item["layer_time_threshold_observation"], sequence_count)
        ranges = item["extrusion_ranges"]
        records = count(ranges["width_mm"]["count"], "width.count") + count(ranges["width_mm"]["invalid_count"], "width.invalid_count")
        validate_extrusion_ranges(ranges, records, "extruder.extrusion_ranges")
        cap = item["configured_filament_max_volumetric_speed_mm3_s"]
        ratio = item["max_nominal_flow_to_cap_ratio"]
        flow = ranges["nominal_volumetric_rate_mm3_s"]
        if cap is not None:
            require(nonnegative(cap, "configured material flow cap") > 0, "Unlimited cap must be null")
        if cap is not None and flow["count"] > 0 and flow["invalid_count"] == 0:
            require(abs(nonnegative(ratio, "flow to cap ratio") - flow["max"] / cap) < 1e-9,
                    "Flow to cap ratio disagrees with current material ranges")
        else:
            require(ratio is None, "Unknown cap or flow must not produce a ratio")
        extrusion_count += records
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
    require(extrusion_count == moves["extrude"], "Per-extruder extrusion counts disagree")
    require(total_volume_entries > 0, "No recorded material volume")
    # Legacy JSON and G-code footer weights are rounded to 0.01 g.
    require(abs(total_mass - plate["total_filament_g"]) <= 0.0051, "Volume/density disagrees with legacy total material weight")
    require(abs(total_mass - facts["footer_mass_g"]) <= 0.0051, "Volume/density disagrees with G-code material footer")
    require(abs(support_mass - plate["support_filament_g"]) <= 0.0051, "Volume/density disagrees with legacy support weight")
    for role in metrics["filament_by_role"]:
        count(role["role_id"], "role_id")
        require(isinstance(role["role"], str) and bool(role["role"]), "Missing role name")
        for key in ("estimated_length_m", "estimated_mass_g"):
            if role[key] is not None:
                nonnegative(role[key], key)
    require(isinstance(report["limitations"], list) and bool(report["limitations"]), "Missing analysis limitations")


def validate_report(report, output, plate, facts):
    require(type(report.get("schema_version")) is int and report["schema_version"] == 1, "Wrong analysis schema_version")
    require(report.get("scope") == "sliced_print_metrics", "Wrong analysis scope")
    require(type(report.get("plate_index")) is int and report["plate_index"] == plate["id"],
            "Analysis does not identify its sliced plate")
    # This entry validates fresh CLI output, unlike the historical metric reader.
    for group in report["objects"]["groups"]:
        require("nominal_target_path_connectivity" in group, "Current CLI output lacks nominal_target_path_connectivity")
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
            report_expected=False, existing_report=False, source=None, help_only=False):
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
                    require(comparable == self.baseline["legacy"], "Legacy result fields/values changed")
                    require(gcode.keys() == self.baseline["gcode"].keys(), "Sliced plate set changed")
                    for index, facts in gcode.items():
                        original = self.baseline["gcode"][index]
                        require(facts["commands"] == original["commands"], f"Executable G-code changed for plate {index}")
                        require(facts["configuration"] == original["configuration"], f"Effective configuration changed for plate {index}")
                        require(facts["layers"] == original["layers"], f"Layer count changed for plate {index}")
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
                       "gcode": "all executable commands and complete CONFIG_BLOCK; comment timestamps excluded"},
        "scope": {"built": False, "downloaded": False, "print_sent": False,
                  "gui_verified": False, "physical_quality_verified": False},
    })
    for error in integrity_errors:
        print(f"FAIL integrity: {error}", file=sys.stderr)
    print(f"Evidence: {output}", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
