#!/usr/bin/env python3
"""Offline local-Z relief trial using Orca's existing height_ranges interface.

Runs fresh Blender detection, then fixed-B and regional-B with identical source
and resolved profiles. Never sends a print, edits a preset or selects a production
artifact. Two slice planes are a sampling objective, not proof of physical quality.
"""
import argparse
from decimal import Decimal
import math
from pathlib import Path
import sys

from evaluate_layer_height_candidate import execute, layer_groups, load_artifacts, common_sections, narrow_paths, terminal_comparison
from layer_surface_proxy import plan_rows
from test_cli_slice_analysis import digest, read_json, save_json

TOLERANCE = 1e-6
MAX_BANDS = 32


def number(value):
    if isinstance(value, list) and len(value) == 1:
        value = value[0]
    if isinstance(value, bool):
        raise ValueError('Boolean is not a dimensional value')
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ValueError('Expected a resolved scalar number') from None
    if not math.isfinite(result):
        raise ValueError('Non-finite dimensional value')
    return result


def make_plan(features, process, machine):
    base = number(process.get('layer_height'))
    first = number(process.get('initial_layer_print_height'))
    minimum, maximum = (number(machine.get(key)) for key in ('min_layer_height', 'max_layer_height'))
    # This first trial intentionally excludes unresolved auto/percent limits,
    # raft, spiral and shrink compensation; do not guess their coordinate meaning.
    if not 0 < minimum <= base <= maximum or first <= 0:
        raise ValueError('Explicit positive machine bounds and base layer height are required')
    if number(process.get('raft_layers')) != 0 or number(process.get('spiral_mode')) != 0:
        raise ValueError('Regional trial currently requires no raft and no spiral mode')
    floor = max(.01, minimum, base / 1.25)
    data = features.get('printGeometry', {})
    if (data.get('scope') != 'horizontal_sections_and_planar_steps'
            or data.get('planarHeightBandScope') != 'single_model_bed_relative_z'
            or data.get('planarHeightScanComplete') is not True
            or data.get('planarHeightBandsComplete') is not True):
        raise ValueError('Complete single-model bed-relative planar bands are required')
    bands = data.get('planarHeightBands')
    if not isinstance(bands, list) or len(bands) > MAX_BANDS:
        raise ValueError('Invalid or excessive planar band list')
    model_height = number(features.get('modelSizeZMm'))
    if model_height <= 0:
        raise ValueError('Positive detected model height is required')
    objectives, skipped = [], []
    seen = set()
    for band in bands:
        if not isinstance(band, dict) or set(band) != {'bottomZMm', 'topZMm'}:
            raise ValueError('Unexpected planar band fields')
        lo, hi = number(band['bottomZMm']), number(band['topZMm'])
        if not 0 <= lo < hi <= model_height + TOLERANCE or (lo, hi) in seen:
            raise ValueError('Out-of-model or duplicate planar band')
        seen.add((lo, hi))
        target = (hi - lo) / 2
        reason = ('FIRST_LAYER_INTERSECTION' if lo < first + TOLERANCE else
                  'BASE_NOMINAL_TARGET_MET' if target >= base - TOLERANCE else
                  'TARGET_BELOW_USABLE_FLOOR' if target < floor - 1e-9 else None)
        row = {'bottom_z_mm': lo, 'top_z_mm': hi, 'target_height_mm': max(target, floor)}
        if reason:
            skipped.append({**row, 'required_height_mm': target, 'reason': reason})
        else:
            objectives.append(row)
    # Partition overlaps: each interval satisfies the most restrictive measured
    # target there. Do not let lexicographic native range order drop a constraint.
    cuts = sorted({row[key] for row in objectives for key in ('bottom_z_mm', 'top_z_mm')})
    ranges = []
    for lo, hi in zip(cuts, cuts[1:]):
        active = [row['target_height_mm'] for row in objectives if row['bottom_z_mm'] < hi and row['top_z_mm'] > lo]
        if not active:
            continue
        height = min(active)
        height_text = format(height, '.12g')
        if ranges and ranges[-1]['max_z'] == lo and ranges[-1]['range_params']['layer_height'] == height_text:
            ranges[-1]['max_z'] = hi
        else:
            ranges.append({'min_z': lo, 'max_z': hi, 'range_params': {'layer_height': height_text}})
    return {'base_mm': base, 'first_mm': first, 'minimum_mm': floor, 'maximum_mm': base,
            'model_height_mm': model_height, 'objectives': objectives, 'skipped': skipped, 'height_ranges': ranges}


def observe(report, plan):
    layers = layer_groups(report)
    plan_rows(layers)  # Validates physical bed-zero coordinates and contiguity.
    objectives = []
    for row in plan['objectives']:
        sampled = [layer for layer in layers
                   if row['bottom_z_mm'] + TOLERANCE < layer['slice_z_mm'] < row['top_z_mm'] - TOLERANCE]
        unavailable = []
        for layer in sampled:
            # Non-empty whole sections are a necessary, not sufficient, local
            # feature test. No claim about this feature's XY paths is made.
            fields = (layer.get('section_area_mm2'), layer.get('planned_path_area_mm2'))
            if layer.get('complete') is not True or any(type(v) not in (int, float)
                    or not math.isfinite(v) or v <= 0 for v in fields):
                unavailable.append(layer['layer_index'])
        objectives.append({**row, 'slice_planes_mm': [layer['slice_z_mm'] for layer in sampled],
                           'sample_count': len(sampled), 'unavailable_section_layers': unavailable})
    outside_changes, violations = [], []
    for layer in layers[1:]:
        h = layer['height_mm']
        if not plan['minimum_mm'] - TOLERANCE <= h <= plan['maximum_mm'] + TOLERANCE:
            violations.append(layer['layer_index'])
        overlaps = any(layer['bottom_z_mm'] < r['max_z'] + TOLERANCE
                       and layer['print_z_mm'] > r['min_z'] - TOLERANCE for r in plan['height_ranges'])
        if not overlaps and abs(h - plan['base_mm']) > TOLERANCE:
            outside_changes.append(layer['layer_index'])
    return {'objectives': objectives, 'first_layer_mm': layers[0]['height_mm'], 'model_layer_count': len(layers),
            'nominal_top_mm': layers[-1]['print_z_mm'],
            'nominal_top_error_mm': layers[-1]['print_z_mm'] - plan['model_height_mm'],
            'ordinary_non_base_layers': [row['layer_index'] for row in layers[1:]
                                         if abs(row['height_mm'] - plan['base_mm']) > TOLERANCE],
            'out_of_bounds_layers': violations, 'outside_window_height_changes': outside_changes}


def verified_pam_plan(request, response, features, process, machine, filament, expected):
    """Bind a saved offline planner result to freshly detected geometry and exact trial profiles.

    An independent local planner checks the contract before the response's own
    ranges are sent to Orca. No silently re-created local plan masquerades as PAM.
    """
    if (request.get('features') != features or request.get('process') != process
            or request.get('filament') != filament
            or number(request.get('minimumLayerHeightMm')) != number(machine.get('min_layer_height'))
            or number(request.get('maximumLayerHeightMm')) != number(machine.get('max_layer_height'))):
        raise ValueError('PAM request is not bound to this fresh detection / resolved profile')
    if (response.get('processOverrides') != {} or response.get('filamentOverrides') != {}
            or response.get('adaptiveLayerHeightPlan') is not None or response.get('adaptiveLayerHeightQuality') is not None):
        raise ValueError('PAM trial requires resolved common B profiles without additional overrides or adaptive plans')
    candidate = response.get('regionalLayerHeightCandidate')
    status = 'READY_FOR_SLICE_VALIDATION' if expected['height_ranges'] else 'NO_REACHABLE_LOCAL_TARGET'
    if not isinstance(candidate, dict) or candidate.get('version') != 'regional-height-v1' or candidate.get('status') != status:
        raise ValueError('PAM regional candidate version/status does not match the measured targets')
    for key, source in (('baseMm', 'base_mm'), ('firstMm', 'first_mm'), ('minimumMm', 'minimum_mm'),
                        ('maximumMm', 'maximum_mm'), ('modelHeightMm', 'model_height_mm')):
        if abs(number(candidate.get(key)) - expected[source]) > 1e-9:
            raise ValueError('PAM regional candidate bounds do not match the trial')
    objectives = [{'bottomZMm': row['bottom_z_mm'], 'topZMm': row['top_z_mm'],
                   'requiredHeightMm': row.get('required_height_mm', row['target_height_mm']),
                   'status': row.get('reason', 'CANDIDATE')}
                  for row in expected['objectives'] + expected['skipped']]
    objectives.sort(key=lambda row: (row['bottomZMm'], row['topZMm']))
    actual_objectives = candidate.get('objectives')
    if not isinstance(actual_objectives, list) or len(actual_objectives) != len(objectives):
        raise ValueError('PAM candidate changed the measured objectives')
    for actual, target in zip(actual_objectives, objectives):
        if (not isinstance(actual, dict) or actual.get('status') != target['status']
                or any(abs(number(actual.get(key)) - target[key]) > 1e-9
                       for key in ('bottomZMm', 'topZMm', 'requiredHeightMm'))):
            raise ValueError('PAM candidate changed an objective or its skipped reason')
    ranges = candidate.get('ranges')
    if not isinstance(ranges, list) or len(ranges) != len(expected['height_ranges']):
        raise ValueError('PAM candidate changed the regional plan')
    native = []
    for row, target in zip(ranges, expected['height_ranges']):
        if not isinstance(row, dict):
            raise ValueError('Malformed PAM height range')
        low, high, height = (number(row.get(key)) for key in ('minimumZMm', 'maximumZMm', 'layerHeightMm'))
        if any(abs(left - right) > 1e-9 for left, right in (
                (low, target['min_z']), (high, target['max_z']), (height, number(target['range_params']['layer_height'])))):
            raise ValueError('PAM regional range does not match the detected objective')
        native.append({'min_z': low, 'max_z': high, 'range_params': {'layer_height': format(height, '.12g')}})
    coupled = {}
    if native:
        for side in ('top', 'bottom'):
            count = process.get(side + '_shell_layers')
            if count is None:
                continue
            count = number(count)
            if count <= 0 or count != int(count):
                continue
            key = side + '_shell_thickness'
            thickness = None if process.get(key) is None else number(process[key])
            nominal = Decimal(str(count)) * Decimal(str(expected['base_mm']))
            if thickness is None or 0 <= thickness < float(nominal):
                coupled[key] = format(nominal.normalize(), 'f')
        if (process.get('enable_support') in ('1', True, 1)
                and process.get('support_type') in ('tree(auto)', 'tree(manual)')
                and process.get('support_style') in (None, 'default', 'organic')):
            coupled['support_style'] = 'tree_hybrid'
    if candidate.get('processOverrides') != coupled:
        raise ValueError('PAM coupled shell/support parameters do not match this profile')
    return {**expected, 'height_ranges': native, 'candidate_process_overrides': coupled}


def assess(baseline, candidate, plan, final_extents=None):
    a, b = observe(baseline, plan), observe(candidate, plan)
    failures = []
    if abs(a['first_layer_mm'] - plan['first_mm']) > TOLERANCE or abs(b['first_layer_mm'] - plan['first_mm']) > TOLERANCE:
        failures.append('FIRST_LAYER_CHANGED')
    if a['ordinary_non_base_layers']:
        failures.append('FIXED_CONTROL_NOT_FIXED')
    if b['out_of_bounds_layers']:
        failures.append('REGIONAL_BOUNDS_VIOLATION')
    if b['outside_window_height_changes']:
        failures.append('REFINEMENT_OUTSIDE_TARGET_WINDOWS')
    if any(row['sample_count'] < 2 for row in b['objectives']):
        failures.append('LOCAL_SAMPLING_TARGET_NOT_MET')
    if any(row['unavailable_section_layers'] for observed in (a, b) for row in observed['objectives']):
        failures.append('TARGET_SECTION_EVIDENCE_UNAVAILABLE')
    if abs(b['nominal_top_error_mm']) > abs(a['nominal_top_error_mm']) + TOLERANCE:
        failures.append('NOMINAL_TOP_ERROR_INCREASED')
    terminal = terminal_comparison(baseline, candidate, plan['model_height_mm'], final_extents)
    if not terminal['complete']:
        failures.append('MODEL_PATH_EXTENT_UNAVAILABLE')
    elif terminal['path_top_error_increased']:
        failures.append('MODEL_PATH_TOP_ERROR_INCREASED')
    if final_extents is not None:
        if not terminal['final_gcode']['complete']:
            failures.append('FINAL_MODEL_PATH_EXTENT_UNAVAILABLE')
        elif terminal['final_gcode']['path_top_error_increased']:
            failures.append('FINAL_GCODE_MODEL_TOP_ERROR_INCREASED')
    comparisons = common_sections(layer_groups(baseline), layer_groups(candidate))
    if any(change['direction'] == 'increase' and change['metric'] in (
            'completely_unprinted_island_count', 'closed_void_fully_covered_count')
            for row in comparisons for change in row['changes']):
        failures.append('NEW_MISSING_ISLAND_OR_CLOSED_VOID')
    old_narrow, new_narrow = narrow_paths(baseline), narrow_paths(candidate)
    if not old_narrow or not new_narrow:
        failures.append('NOMINAL_WIDTH_HEIGHT_UNAVAILABLE')
    elif any(length > 0 and old_narrow.get(key, 0) == 0 for key, length in new_narrow.items()):
        failures.append('NEW_NOMINAL_WIDTH_BELOW_HEIGHT')
    improved = sum(right['sample_count'] >= 2 > left['sample_count'] for left, right in zip(a['objectives'], b['objectives']))
    status = 'REJECTED' if failures else 'LOCAL_SAMPLING_VERIFIED' if improved else 'NO_SAMPLING_GAIN'
    return {'status': status, 'failures': failures, 'baseline': a, 'candidate': b,
            'improved_objectives': improved, 'comparable_sections': len(comparisons), 'terminal_paths': terminal,
            'same_section_observations': comparisons,
            'physical_quality_verified': False, 'selected_production_artifact': None,
            'limitations': ['Slice planes do not prove the local XY feature has an extrusion path',
                            'Different slice phases above a range can change surfaces and supports',
                            'Only same-Z sections are comparable; no automatic production selection']}


def assembly(model, ranges):
    return {'plates': [{'plate_name': 'local-height-trial', 'need_arrange': True,
                        'objects': [{'path': str(model), 'count': 1, 'filaments': [1], 'height_ranges': ranges}]}]}


def trial(args):
    names = ('binary', 'model', 'machine', 'process', 'filament', 'blender', 'feature_script')
    if getattr(args, 'pam_request', None) is not None:
        names += ('pam_request', 'pam_decision')
    identities = {str(getattr(args, key)): digest(getattr(args, key)) for key in names}
    # The analyzer imports adjacent modules; bind their identities as well.
    for name in ('print_feature_geometry.py', 'wall_feature_geometry.py'):
        path = args.feature_script.parent / name
        identities[str(path)] = digest(path)
    args.output_dir.mkdir()
    result = {'scope': 'offline_single_model_regional_height_trial', 'input_sha256': identities,
              'print_sent': False, 'ordinary_default_behavior_changed': False, 'runs': {}, 'completed': False,
              'tool_sha256': {name: digest(Path(__file__).with_name(name)) for name in
                             ('evaluate_relief_height_candidate.py', 'evaluate_layer_height_candidate.py',
                              'layer_surface_proxy.py', 'test_cli_slice_analysis.py', 'test_cli_slice_features.py')}}
    try:
        process, machine, filament = (read_json(getattr(args, key)) for key in ('process', 'machine', 'filament'))
        for key in ('filament_shrink', 'filament_shrinkage_compensation_z'):
            value = filament.get(key)
            if isinstance(value, list) and len(value) == 1:
                value = value[0]
            if value not in ('100%', '100', 100):
                raise ValueError('Regional trial requires explicit 100% XY/Z shrink compensation')
        width = number(process.get('outer_wall_line_width'))
        if width <= 0:
            raise ValueError('Resolved positive outer_wall_line_width is required')
        detection = args.output_dir / 'detection'
        detection.mkdir()
        request_path, features_path = detection / 'request.json', detection / 'features.json'
        save_json(request_path, {'models': [{'modelPath': str(args.model), 'modelScale': 1,
                                            'rotateX': 0, 'rotateY': 0, 'rotateZ': 0}],
                                 'outerWallLineWidthMm': width, 'timeoutSeconds': args.timeout})
        code = execute([str(args.blender), '--background', '--factory-startup', '--python-use-system-env',
                        '--python-exit-code', '1', '--python', str(args.feature_script), '--',
                        str(request_path), str(features_path)], detection, args.timeout)
        features = read_json(features_path)
        if code != 0 or features.get('status') != 'success':
            raise ValueError('Fresh feature detection failed')
        plan = make_plan(features['features'], process, machine)
        if getattr(args, 'pam_request', None) is not None:
            plan = verified_pam_plan(read_json(args.pam_request), read_json(args.pam_decision),
                                     features['features'], process, machine, filament, plan)
            result['plan_source'] = 'pam_response_verified_against_fresh_detection'
        else:
            result['plan_source'] = 'offline_reference_planner'
        result['plan'] = plan
        process_path = args.process
        if plan.get('candidate_process_overrides'):
            process_path = args.output_dir / 'common-process.json'
            save_json(process_path, {**process, **plan['candidate_process_overrides']})
            identities[str(process_path)] = digest(process_path)
        if plan['height_ranges']:
            artifacts = {}
            for name, ranges in (('fixed_b', []), ('regional_candidate', plan['height_ranges'])):
                directory = args.output_dir / name
                directory.mkdir()
                assembly_path = directory / 'assembly.json'
                save_json(assembly_path, assembly(args.model, ranges))
                command = [str(args.binary), '--datadir', str(directory / 'data'), '--debug', '1', '--slice', '0',
                           '--load-settings', f'{args.machine};{process_path}', '--load-filaments', str(args.filament),
                           '--slice-analysis-report', '--outputdir', str(directory / 'output'),
                           '--load-assemble-list', str(assembly_path)]
                if args.optimize_model_detail_paths:
                    command.extend(('--optimize-model-detail-paths', '1'))
                code = execute(command, directory, args.timeout)
                result['runs'][name] = {'exit_code': code}
                if code != 0:
                    raise ValueError(f'{name} slicing failed: {code}')
                report, facts, identity = load_artifacts(directory)
                result['runs'][name].update(identity)
                artifacts[name] = report, facts
            a, b = artifacts['fixed_b'], artifacts['regional_candidate']
            if a[1]['configuration'] != b[1]['configuration']:
                raise ValueError('Effective configurations differ outside the object height-range plan')
            result['assessment'] = assess(a[0], b[0], plan, final_extents={
                'baseline': a[1].get('model_path_extent'), 'candidate': b[1].get('model_path_extent')})
        else:
            result['assessment'] = {'status': 'NO_REACHABLE_LOCAL_TARGET', 'physical_quality_verified': False,
                                    'selected_production_artifact': None}
        if any(digest(Path(path)) != identity for path, identity in identities.items()):
            raise ValueError('Input or executable changed during the trial')
        result['completed'] = True
    except Exception as exc:
        result.update(error=f'{type(exc).__name__}: {exc}', assessment=None)
    save_json(args.output_dir / 'trial.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('binary', 'model', 'machine', 'process', 'filament', 'blender', 'feature-script'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--pam-request', type=Path, help='Saved offline PAM planner request (paired with --pam-decision)')
    parser.add_argument('--pam-decision', type=Path, help='Saved offline planner result, not a live production response; verified before use')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--optimize-model-detail-paths', action='store_true')
    args = parser.parse_args()
    if (args.pam_request is None) != (args.pam_decision is None):
        parser.error('Supply both --pam-request and --pam-decision, or neither')
    names = ('binary', 'model', 'machine', 'process', 'filament', 'blender', 'feature_script')
    if args.pam_request is not None:
        names += ('pam_request', 'pam_decision')
    for key in names:
        path = getattr(args, key).expanduser().resolve()
        if not path.is_file():
            parser.error(f'Missing {key}: {path}')
        setattr(args, key, path)
    args.output_dir = args.output_dir.expanduser().absolute()
    if (args.output_dir.exists() or args.output_dir.is_symlink() or not args.output_dir.parent.is_dir()
            or args.model.suffix.lower() != '.stl' or not 1 <= args.timeout <= 1800):
        parser.error('Use one STL, timeout 1–1800 seconds and a fresh output directory')
    result = trial(args)
    print(result.get('error') or result['assessment']['status'])
    print(args.output_dir / 'trial.json')
    return 0 if result['completed'] else 1


if __name__ == '__main__':
    sys.exit(main())
