"""Offline audit of explicit regional plans; never selects production G-code.

Reports must already be bound to identical model/pose and resolved profiles by
the caller. This checks actual native layer placement, not just requested height.
"""
import math

from evaluate_layer_height_candidate import layer_groups, common_sections, narrow_paths
from layer_surface_proxy import plan_rows

EPSILON = 1e-6


def check_scope(baseline, candidate, plan):
    if plan.get('version') != 'regional-slope-v1':
        raise ValueError('Expected explicit regional-slope-v1 plan')
    ranges = plan.get('ranges')
    if not isinstance(ranges, list) or len(ranges) > 128:
        raise ValueError('Missing ranges')
    base, first, floor = (plan.get(k) for k in ('baseMm', 'firstMm', 'minimumMm'))
    if any(type(v) not in (int, float) or not math.isfinite(v) or v <= 0 for v in (base, first, floor)) or floor > base:
        raise ValueError('Invalid layer bounds')
    last = first
    for r in ranges:
        if not isinstance(r, dict):
            raise ValueError('Invalid regional range')
        lo, hi, h = (r.get(k) for k in ('minimumZMm', 'maximumZMm', 'layerHeightMm'))
        if (any(type(v) not in (int, float) or not math.isfinite(v) for v in (lo, hi, h))
                or lo < last - EPSILON or lo >= hi or not floor - EPSILON <= h < base):
            raise ValueError('Invalid or overlapping regional plan')
        if (abs((hi - lo) / h - round((hi - lo) / h)) > EPSILON
                or any(abs((z - first) / base - round((z - first) / base)) > EPSILON for z in (lo, hi))):
            raise ValueError('Plan does not preserve baseline phase')
        last = hi
    a, b = layer_groups(baseline), layer_groups(candidate)
    plan_rows(a); plan_rows(b)  # Reject incomplete/invalid physical layer coordinates.
    failures = []
    if any(abs(row['height_mm'] - base) > EPSILON for row in a[1:]):
        failures.append('CONTROL_NOT_FIXED')
    if any(abs(rows[0]['height_mm'] - first) > EPSILON for rows in (a, b)):
        failures.append('FIRST_LAYER_CHANGED')
    if abs(a[-1]['print_z_mm'] - b[-1]['print_z_mm']) > EPSILON:
        failures.append('TERMINAL_LAYER_CHANGED')

    def overlaps(row, r):
        return row['bottom_z_mm'] < r['maximumZMm'] - EPSILON and row['print_z_mm'] > r['minimumZMm'] + EPSILON

    def outside(rows):
        return [row for row in rows if not any(overlaps(row, r) for r in ranges)]

    ao, bo = outside(a), outside(b)
    keys = ('bottom_z_mm', 'print_z_mm', 'slice_z_mm', 'height_mm')
    if len(ao) != len(bo) or any(abs(left[k] - right[k]) > EPSILON for left, right in zip(ao, bo) for k in keys):
        failures.append('OUTSIDE_LAYER_PHASE_CHANGED')
    observations = []
    for r in ranges:
        actual = [row for row in b if overlaps(row, r)]
        required = round((r['maximumZMm'] - r['minimumZMm']) / r['layerHeightMm'])
        correct = (len(actual) == required and bool(actual)
            and abs(actual[0]['bottom_z_mm'] - r['minimumZMm']) <= EPSILON
            and abs(actual[-1]['print_z_mm'] - r['maximumZMm']) <= EPSILON
            and all(abs(row['height_mm'] - r['layerHeightMm']) <= EPSILON for row in actual))
        if not correct:
            failures.append('REGIONAL_PLAN_NOT_REALIZED')
        observations.append({**r, 'expected_layers': required, 'actual_layers': len(actual), 'realized': correct})
    if any(not floor - EPSILON <= row['height_mm'] <= base + EPSILON for row in b[1:]):
        failures.append('LAYER_HEIGHT_OUT_OF_BOUNDS')
    common = common_sections(a, b)
    changed = [r for r in common if any(c['direction'] != 'same' for c in r['changes'])]
    old_narrow, new_narrow = narrow_paths(baseline), narrow_paths(candidate)
    return {'scope_verified': not failures, 'failures': sorted(set(failures)),
            'baseline_layers': len(a), 'regional_layers': len(b), 'outside_layers_compared': min(len(ao), len(bo)),
            'ranges': observations, 'same_z_sections_compared': len(common), 'same_z_changes': changed,
            'nominal_width_height_available': bool(old_narrow) and bool(new_narrow),
            'new_width_below_height_roles': ([list(key) for key, value in new_narrow.items()
                if value > 0 and old_narrow.get(key, 0) == 0] if old_narrow and new_narrow else None),
            'physical_quality_verified': False, 'selected_production_artifact': None}
