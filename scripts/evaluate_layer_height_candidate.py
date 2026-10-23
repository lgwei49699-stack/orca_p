#!/usr/bin/env python3
"""Offline, same-input bounded-layer trial. Never submits a print or changes presets.

Runs the fixed B process and one bounded adaptive candidate using the same files
and executable. This is a conservative candidate screen, not a physical quality
optimizer. A supported fallback references the fixed B artifact, never A's model
or template. Inconclusive trials do not automatically select the candidate.
"""

import argparse
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

from test_cli_slice_analysis import digest, gcode_facts, read_json, save_json, validate_report
from layer_surface_proxy import compare_surface
from test_cli_slice_features import fixture_gcode_extrusions


Z_TOLERANCE_MM = 1e-6  # Native coordinate resolution, not a print-quality tolerance.
COMMON_SCOPE = "per_instance_model_paths_before_gcode"
GCODE_Z_TOLERANCE_MM = .001  # Exported XYZ uses millimetres rounded to three decimals.


def finite(value, positive=False):
    return type(value) in (int, float) and math.isfinite(value) and (value > 0 if positive else value >= 0)


def same_number(left, right):
    # Only floating-point representation noise; no material/quality threshold.
    return finite(left) and finite(right) and abs(left - right) <= 64 * max(math.ulp(float(left)), math.ulp(float(right)))


def layer_groups(report):
    objects = report.get("objects", {})
    groups = objects.get("groups")
    if objects.get("source_object_count") != 1 or objects.get("instance_count") != 1 or not isinstance(groups, list) or len(groups) != 1:
        raise ValueError("Only one source model / one instance / one slice group is supported")
    group = groups[0]
    coverage = group.get("planned_layer_coverage", {})
    layers = coverage.get("layers")
    if coverage.get("scope") != COMMON_SCOPE or not isinstance(layers, list) or not layers or group.get("model_layer_count") != len(layers):
        raise ValueError("Missing complete model layer identity")
    previous = -math.inf
    for index, layer in enumerate(layers):
        if (type(layer.get("layer_index")) is not int or layer["layer_index"] != index
                or not finite(layer.get("height_mm"), True) or not finite(layer.get("slice_z_mm"))
                or layer["slice_z_mm"] <= previous):
            raise ValueError("Invalid or non-monotonic model layer identity")
        previous = layer["slice_z_mm"]
    return layers


def common_sections(baseline, candidate):
    """Compare only matching source sections, not layer indices or aggregate sums."""
    rows, left, right = [], 0, 0
    source_keys = ("section_area_mm2", "hole_area_mm2", "section_island_count", "closed_void_region_count")
    metrics = ("completely_unprinted_island_area_mm2", "completely_unprinted_island_count", "hole_intrusion_area_mm2",
               "closed_void_fully_covered_count")
    while left < len(baseline) and right < len(candidate):
        a, b = baseline[left], candidate[right]
        dz = b["slice_z_mm"] - a["slice_z_mm"]
        if abs(dz) <= Z_TOLERANCE_MM:
            if (a.get("complete") is True and b.get("complete") is True
                    and all(same_number(a.get(key), b.get(key)) for key in source_keys)
                    and all(finite(a.get(key)) and finite(b.get(key)) for key in metrics)):
                changes = [{"metric": key, "baseline": a[key], "candidate": b[key],
                            "direction": "same" if same_number(a[key], b[key]) else "increase" if b[key] > a[key] else "decrease"}
                           for key in metrics]
                rows.append({"baseline_layer_index": left, "candidate_layer_index": right,
                             "slice_z_mm": a["slice_z_mm"], "changes": changes})
            left += 1
            right += 1
        elif dz < 0:
            right += 1
        else:
            left += 1
    return rows


def narrow_paths(report):
    """A nominal width/height warning, deliberately not a physical failure label."""
    value = report.get("gcode", {}).get("quality_paths", {})
    if value.get("scope") != "final_gcode_positive_extrusion_moves" or not isinstance(value.get("rows"), list):
        return None
    result = {}
    for row in value["rows"]:
        if row.get("role") in ("bridge", "internal_bridge", "overhang_wall"):
            continue
        observed = row.get("width_height_observation", {})
        identity = (row.get("extruder_id"), row.get("role"))
        if (type(identity[0]) is not int or not isinstance(identity[1], str) or identity in result
                or row.get("complete") is not True or observed.get("complete") is not True
                or observed.get("applicable") is not True
                or observed.get("scope") != "same_move_nominal_width_height_non_bridge"
                or not finite(observed.get("below_height_path_length_mm"))):
            return None
        result[identity] = observed["below_height_path_length_mm"]
    return result


def terminal_paths(report, source_height_mm):
    """Model-path extent, not nominal plan top, support top or local XY fidelity."""
    result = {"scope": "complete_model_layer_planned_path_extent", "complete": False}
    if not finite(source_height_mm, True):
        return {**result, "reason": "SOURCE_HEIGHT_UNAVAILABLE"}
    layers = layer_groups(report)
    previous, active, empty = -math.inf, [], []
    for row in layers:
        z, area, section = (row.get(key) for key in ("print_z_mm", "planned_path_area_mm2", "section_area_mm2"))
        if (row.get("complete") is not True or not finite(z, True) or z <= previous
                or not finite(area) or not finite(section)
                or row.get("invalid_path_count") != 0 or row.get("excluded_path_count") != 0):
            return {**result, "reason": "MODEL_PATH_EXTENT_UNAVAILABLE"}
        previous = z
        if area > 0:
            active.append(row)
        elif section > 0:
            empty.append(row["layer_index"])
    if not active:
        return {**result, "reason": "NO_MODEL_PATH"}
    last = active[-1]
    return {**result, "complete": True, "source_height_mm": source_height_mm,
            "nominal_plan_top_mm": layers[-1]["print_z_mm"], "last_path_print_z_mm": last["print_z_mm"],
            "last_path_layer_index": last["layer_index"], "path_top_error_mm": last["print_z_mm"] - source_height_mm,
            "source_sections_without_paths": empty,
            "terminal_source_sections_without_paths": [i for i in empty if i > last["layer_index"]]}


def gcode_model_extent(path):
    """Read final spatial positive-E moves; restricted to local single-tool Orca output."""
    result = {"scope": "single_tool_final_gcode_model_extrusion_extent", "complete": False}
    model_roles = {"Inner wall", "Outer wall", "Overhang wall", "Internal solid infill", "Top surface",
                   "Bottom surface", "Bridge", "Internal Bridge", "Sparse infill", "Gap infill"}
    excluded_roles = {"", "Custom", "Support", "Support interface", "Support transition", "Skirt", "Brim", "Wipe tower"}
    try:
        # Do not reinterpret work offsets, multi-tool coordinates, inch units or
        # volumetric extrusion as comparable model coordinates/filament E.
        for line in path.read_text(encoding="utf-8").splitlines():
            code = line.split(';', 1)[0].strip()
            command = code.split()[0] if code else ''
            if (command in {'G10', 'G11', 'G20', 'G53', 'G54', 'G55', 'G56', 'G57', 'G58', 'G59', 'G92.1',
                            'M200', 'M206', 'M290'} or re.fullmatch(r'T\d+', command) and int(command[1:]) != 0
                    or command == 'G92' and re.search(r'[XYZ]', code)):
                raise ValueError('Unsupported coordinate/extrusion mode: ' + command)
            if command == 'M221':
                multiplier = re.search(r'\bS([-+]?(?:\d+(?:\.\d*)?|\.\d+))', code)
                if multiplier is None or not finite(float(multiplier[1]), True):
                    raise ValueError('Unavailable or zero extrusion multiplier')
        zs, moves = set(), 0
        for role, _, start, end in fixture_gcode_extrusions(path):
            if not all(math.isfinite(value) for value in (*start.values(), *end.values())):
                raise ValueError('Non-finite motion')
            if role in excluded_roles:
                continue
            if role not in model_roles:
                raise ValueError('Unknown extrusion role: ' + role)
            zs.add(end['Z'])
            moves += 1
        if not zs:
            raise ValueError('No model extrusion moves')
        return {**result, "complete": True, "first_path_print_z_mm": min(zs), "last_path_print_z_mm": max(zs),
                "model_extrusion_z_count": len(zs), "model_extrusion_move_count": moves}
    except (OSError, ValueError, AssertionError) as error:
        return {**result, "reason": str(error)}


def terminal_comparison(baseline, candidate, source_height_mm, final_extents=None):
    a, b = terminal_paths(baseline, source_height_mm), terminal_paths(candidate, source_height_mm)
    complete = a["complete"] and b["complete"]
    result = {"complete": complete, "baseline": a, "candidate": b,
            "path_top_error_increased": (abs(b["path_top_error_mm"]) > abs(a["path_top_error_mm"]) + Z_TOLERANCE_MM)
            if complete else None}
    final_extents = final_extents or {}
    final = {"complete": False, "path_top_error_increased": None}
    old, new = (final_extents.get(key) or {} for key in ('baseline', 'candidate'))
    if (finite(source_height_mm, True) and old.get('complete') is True and new.get('complete') is True
            and all(finite(row.get('last_path_print_z_mm'), True) for row in (old, new))):
        final.update(complete=True, baseline=old, candidate=new,
                     path_top_error_increased=abs(new['last_path_print_z_mm'] - source_height_mm)
                     > abs(old['last_path_print_z_mm'] - source_height_mm) + GCODE_Z_TOLERANCE_MM)
        final['matches_planned_extent'] = complete and all(
            abs(planned['last_path_print_z_mm'] - actual['last_path_print_z_mm']) <= GCODE_Z_TOLERANCE_MM
            for planned, actual in ((a, old), (b, new)))
    result['final_gcode'] = final
    return result


def assess(baseline, candidate, plan, *, identical_commands=False, source_height_mm=None, final_extents=None):
    """Caller must first establish immutable inputs and equal effective configs."""
    base = plan["base"]
    a, b = layer_groups(baseline), layer_groups(candidate)
    if not all(abs(layer["height_mm"] - base) <= Z_TOLERANCE_MM for layer in a[1:]):
        raise ValueError("Control does not use the declared fixed B layer height")
    reasons, cautions = [], []
    out_of_bounds = sum(layer["height_mm"] < plan["minimum"] - Z_TOLERANCE_MM
                        or layer["height_mm"] > plan["maximum"] + Z_TOLERANCE_MM for layer in b[1:])
    refined = sum(layer["height_mm"] < base - Z_TOLERANCE_MM for layer in b[1:])
    if out_of_bounds:
        reasons.append({"code": "LAYER_PLAN_VIOLATION", "count": out_of_bounds})
    if abs(a[0]["height_mm"] - b[0]["height_mm"]) > Z_TOLERANCE_MM:
        reasons.append({"code": "FIRST_MODEL_LAYER_CHANGED"})
    sections = common_sections(a, b)
    regressions = [{**{key: section[key] for key in ("baseline_layer_index", "candidate_layer_index", "slice_z_mm")}, **change}
                   for section in sections for change in section["changes"] if change["direction"] == "increase"]
    lost_features = [row for row in regressions if row["metric"] in
                     ("completely_unprinted_island_count", "closed_void_fully_covered_count")]
    if lost_features:
        reasons.append({"code": "NEW_UNREPRESENTED_SECTION_FEATURE", "observations": lost_features})
    if any(row["metric"] not in ("completely_unprinted_island_count", "closed_void_fully_covered_count") for row in regressions):
        cautions.append("SOURCE_SECTION_AREA_INCREASE_REQUIRES_LOCAL_REVIEW")
    old_narrow, new_narrow = narrow_paths(baseline), narrow_paths(candidate)
    if old_narrow is None or new_narrow is None:
        cautions.append("NOMINAL_WIDTH_HEIGHT_EVIDENCE_UNAVAILABLE")
    else:
        # Only a transition from no such observation to a new observation is a
        # fallback trigger. Raw lengths from different layer plans are not a score.
        new_roles = [{"extruder_id": key[0], "role": key[1], "length_mm": length}
                     for key, length in new_narrow.items() if length > 0 and old_narrow.get(key, 0) == 0]
        if new_roles:
            reasons.append({"code": "NEW_NOMINAL_WIDTH_BELOW_HEIGHT", "observations": new_roles})
    coverage = {"baseline_model_layers": len(a), "candidate_model_layers": len(b),
                "comparable_sections": len(sections), "uncompared_baseline_layers": len(a) - len(sections),
                "uncompared_candidate_layers": len(b) - len(sections)}
    if len(sections) != len(a) or len(sections) != len(b):
        cautions.append("PARTIAL_SOURCE_SECTION_COMPARISON")
    terminal = terminal_comparison(baseline, candidate, source_height_mm, final_extents)
    if not terminal["complete"]:
        cautions.append("MODEL_PATH_EXTENT_UNAVAILABLE")
    elif terminal["path_top_error_increased"]:
        reasons.append({"code": "MODEL_PATH_TOP_ERROR_INCREASED", "observations": terminal})
    if terminal["complete"] and terminal["candidate"]["terminal_source_sections_without_paths"]:
        cautions.append("TERMINAL_SOURCE_SECTION_HAS_NO_MODEL_PATH")
    if not terminal['final_gcode']['complete']:
        cautions.append('FINAL_MODEL_PATH_EXTENT_UNAVAILABLE')
    else:
        if terminal['final_gcode']['path_top_error_increased']:
            reasons.append({'code': 'FINAL_GCODE_MODEL_TOP_ERROR_INCREASED', 'observations': terminal['final_gcode']})
        if not terminal['final_gcode']['matches_planned_extent']:
            cautions.append('FINAL_GCODE_AND_PLANNED_EXTENT_DIFFER')
    # Both changed layer plans and a clean motion report can still worsen the
    # physical surface. Do not manufacture an automatic quality pass.
    cautions.append("LOCAL_SURFACE_FIDELITY_AND_PHYSICAL_QUALITY_NOT_VERIFIED")
    if reasons:
        status, selected = "FALLBACK_FIXED_B", "fixed_b"
    elif identical_commands:
        status, selected = "NO_EXECUTABLE_CHANGE", "fixed_b"
    else:
        status, selected = "REVIEW_REQUIRED", None
    return {"status": status, "selected_variant": selected, "physical_quality_verified": False,
            "reasons": reasons, "cautions": cautions, "model_layer_plan": {
                "first_model_height_mm": b[0]["height_mm"], "ordinary_refined_count": refined,
                "ordinary_refined_ratio": refined / (len(b) - 1) if len(b) > 1 else 0.,
                "ordinary_out_of_bounds_count": out_of_bounds},
            "section_comparison": coverage, "common_sections": sections, "terminal_paths": terminal,
            "estimates": {"baseline_time_s": baseline.get("gcode", {}).get("normal_estimate", {}).get("total_time_s"),
                          "candidate_time_s": candidate.get("gcode", {}).get("normal_estimate", {}).get("total_time_s")}}


def execute(command, directory, timeout):
    save_json(directory / "command.json", command)
    with (directory / "stdout.log").open("wb") as stdout, (directory / "stderr.log").open("wb") as stderr:
        with subprocess.Popen(command, cwd=directory, stdout=stdout, stderr=stderr,
                              start_new_session=os.name != "nt") as process:
            try:
                return process.wait(timeout=timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                if os.name == "nt":
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=10)
                raise


def load_artifacts(directory):
    output = directory / "output"
    legacy = read_json(output / "result.json")
    plates = legacy.get("sliced_plates")
    if legacy.get("return_code") != 0 or not isinstance(plates, list) or len(plates) != 1 or plates[0].get("id") != 1:
        raise ValueError("Trial must generate exactly one successful plate")
    gcode = (output / "plate_1.gcode").resolve(strict=True)
    if sorted(path.name for path in output.glob("*.gcode")) != ["plate_1.gcode"]:
        raise ValueError("Unexpected extra G-code artifacts")
    facts = gcode_facts(gcode)
    facts["path"] = gcode
    report_path = output / "plate_1.analysis.json"
    report = read_json(report_path)
    validate_report(report, output, plates[0], facts)
    facts['model_path_extent'] = gcode_model_extent(gcode)
    return report, facts, {"gcode": str(gcode), "gcode_sha256": digest(gcode),
                           "analysis": str(report_path), "analysis_sha256": digest(report_path)}


def surface_evidence(model, baseline, candidate, configuration):
    values = {}
    source_scale_keys = ("filament_shrink", "filament_shrinkage_compensation_z")
    for line in configuration:
        if line.startswith("; ") and " = " in line:
            key, value = line[2:].split(" = ", 1)
            if key not in source_scale_keys:
                continue
            if key in values:
                return {"complete": False, "physical_quality_verified": False, "error": "Ambiguous effective configuration"}
            values[key] = value.strip()
    # The proxy uses the original facet normals and dimensions. Anisotropic
    # shrink compensation changes both; missing identity must not mean 100%.
    if any(values.get(key) not in ("100%", "100") for key in source_scale_keys):
        return {"complete": False, "physical_quality_verified": False,
                "error": "Surface proxy requires explicit single-material 100% XY and Z shrink compensation"}
    return compare_surface(model, layer_groups(baseline), layer_groups(candidate))


def trial(args):
    protected = {str(getattr(args, key)): digest(getattr(args, key))
                 for key in ("binary", "model", "machine", "process", "filament")}
    process = read_json(args.process)
    try:
        base = float(process["layer_height"])
    except (KeyError, ValueError, TypeError):
        raise ValueError("A resolved process with one positive layer_height is required")
    if (not finite(base, True) or not (0 < args.minimum <= args.maximum <= base)
            or args.minimum < base / 1.25 - 1e-9 or not 0 <= args.quality <= 1):
        raise ValueError("Trial bounds must follow quality-bounded-v1; this tool does not test arbitrary coarse profiles")
    args.output_dir.mkdir()
    script_dir = Path(__file__).resolve().parent
    result = {"version": 1, "scope": "offline_same_input_bounded_layer_trial", "input_sha256": protected,
              "tool_sha256": {name: digest(script_dir / name) for name in
                              ("evaluate_layer_height_candidate.py", "layer_surface_proxy.py", "test_cli_slice_analysis.py",
                               "test_cli_slice_features.py")},
              "plan": {"base": base, "minimum": args.minimum, "maximum": args.maximum, "quality": args.quality},
              "ordinary_default_behavior_changed": False, "print_sent": False, "runs": {}, "assessment": None}
    artifacts = {}
    try:
        for name in ("fixed_b", "adaptive_candidate"):
            directory = args.output_dir / name
            directory.mkdir()
            command = [str(args.binary), "--datadir", str(directory / "data"), "--debug", "1", "--slice", "0",
                       "--load-settings", f"{args.machine};{args.process}", "--load-filaments", str(args.filament),
                       "--arrange", "1", "--ensure-on-bed", "--slice-analysis-report", "--outputdir", str(directory / "output")]
            if args.optimize_model_detail_paths:
                command.extend(("--optimize-model-detail-paths", "1"))
            if name == "adaptive_candidate":
                command.extend(("--variable-layer-height-mode", "adaptive", "--variable-layer-height-quality-speed", str(args.quality),
                                "--variable-layer-height-min", str(args.minimum), "--variable-layer-height-max", str(args.maximum)))
            command.append(str(args.model))
            code = execute(command, directory, args.timeout)
            result["runs"][name] = {"exit_code": code}
            if code != 0:
                raise RuntimeError(f"{name} failed with exit {code}; no quality conclusion or automatic artifact selection")
            report, facts, identity = load_artifacts(directory)
            result["runs"][name].update(identity)
            artifacts[name] = (report, facts)
        a, b = artifacts["fixed_b"], artifacts["adaptive_candidate"]
        if a[1]["configuration"] != b[1]["configuration"]:
            raise ValueError("Effective G-code configs differ; cannot attribute effects to layer planning")
        if any(not Path(path).is_file() or digest(Path(path)) != expected for path, expected in protected.items()):
            raise ValueError("Input/executable identity changed while running; discard selection")
        result["surface_proxy"] = surface_evidence(args.model, a[0], b[0], a[1]["configuration"])
        result["assessment"] = assess(a[0], b[0], result["plan"], identical_commands=a[1]["commands"] == b[1]["commands"],
                                      source_height_mm=result["surface_proxy"].get("source_height_mm")
                                      if result["surface_proxy"].get("complete") is True else None,
                                      final_extents={'baseline': a[1].get('model_path_extent'),
                                                     'candidate': b[1].get('model_path_extent')})
        # Geometry is an explanatory proxy, not an approval to replace a print.
        if any(not Path(path).is_file() or digest(Path(path)) != expected for path, expected in protected.items()):
            raise ValueError("Input/executable identity changed during surface comparison; discard selection")
        selected = result["assessment"]["selected_variant"]
        result["selected_artifact"] = result["runs"][selected] if selected else None
        result["completed"] = True
    except Exception as error:
        result.update(completed=False, error=f"{type(error).__name__}: {error}", selected_artifact=None)
    save_json(args.output_dir / "trial.json", result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "model", "machine", "process", "filament"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="A new local evidence directory; never overwritten")
    parser.add_argument("--minimum", type=float, required=True)
    parser.add_argument("--maximum", type=float, required=True)
    parser.add_argument("--quality", type=float, default=.5)
    parser.add_argument("--timeout", type=int, default=180, help="Per-candidate timeout in seconds")
    parser.add_argument("--optimize-model-detail-paths", action="store_true", help="Enable the same independent opt-in in BOTH candidates")
    args = parser.parse_args(argv)
    for key in ("binary", "model", "machine", "process", "filament"):
        path = getattr(args, key).expanduser().resolve()
        if not path.is_file():
            parser.error(f"Missing local {key}: {path}")
        setattr(args, key, path)
    if args.model.suffix.lower() != ".stl":
        parser.error("This trial is restricted to one STL, without project-embedded overrides")
    if not 1 <= args.timeout <= 1800 or not all(math.isfinite(value) for value in (args.minimum, args.maximum, args.quality)):
        parser.error("Invalid timeout or non-finite plan")
    args.output_dir = args.output_dir.expanduser().absolute()
    if args.output_dir.exists() or args.output_dir.is_symlink() or not args.output_dir.parent.is_dir():
        parser.error("Output must be a new directory with an existing parent")
    try:
        result = trial(args)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    assessment = result.get("assessment")
    if assessment:
        print(f"{assessment['status']}: selected={assessment['selected_variant']}; "
              f"reasons={[reason['code'] for reason in assessment['reasons']]}; "
              f"comparison={assessment['section_comparison']}")
    else:
        print(result.get("error"))
    print(f"Evidence: {args.output_dir / 'trial.json'}")
    return 0 if result["completed"] else 1


if __name__ == "__main__":
    sys.exit(main())
