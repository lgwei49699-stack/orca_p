#!/usr/bin/env python3
"""Read-only offline audit of all 16 feature decisions and one existing slice.

No slicing, parameter search, network, template writes or print submission. The
output distinguishes configuration delivery from path observations and physical
validation. It never selects an artifact or certifies print readiness.
"""

import argparse
from decimal import Decimal, InvalidOperation
import math
from pathlib import Path

from evaluate_layer_height_candidate import layer_groups, load_artifacts, narrow_paths
from test_cli_slice_analysis import digest, read_json, save_json, validate_residual_peak, validate_solid_fill_peak


# Review coverage, not a second production rule engine. Keep all retained keys.
CATALOG = {
    'FINE_DETAIL': 'wall_generator min_feature_size min_bead_width initial_layer_min_bead_width min_length_factor outer_wall_line_width inner_wall_line_width precise_outer_wall',
    'THIN_CONNECTION': 'minimum_sparse_infill_area',
    'RELIEF': 'layer_height initial_layer_print_height',
    'CURVED_SURFACE': 'layer_height support_style',
    'OVERHANG': 'enable_support support_type support_style enable_overhang_speed overhang_1_4_speed overhang_2_4_speed overhang_3_4_speed overhang_4_4_speed filament.enable_overhang_bridge_fan filament.overhang_fan_speed filament.overhang_fan_threshold',
    'BRIDGE': 'bridge_speed bridge_flow bridge_density thick_bridges internal_bridge_speed internal_bridge_flow internal_bridge_density thick_internal_bridges filament.enable_overhang_bridge_fan filament.overhang_fan_speed filament.internal_bridge_fan_speed',
    'FLOATING_START': 'enable_support support_type support_on_build_plate_only',
    'BED_ADHESION': 'brim_type brim_width brim_object_gap initial_layer_speed raft_layers',
    'TALL_SLENDER': 'outer_wall_speed outer_wall_acceleration small_perimeter_speed',
    'SMALL_PERIMETER': 'small_perimeter_speed small_perimeter_threshold',
    'SMALL_LAYER': 'filament.slow_down_for_layer_cooling filament.slow_down_layer_time filament.slow_down_min_speed filament.fan_min_speed filament.fan_max_speed filament.dont_slow_down_outer_wall filament.close_fan_the_first_x_layers filament.full_fan_speed_layer filament.fan_cooling_layer_time',
    'FLOW_LIMIT': 'filament.filament_max_volumetric_speed',
    'SHELL_SURFACE': 'wall_loops top_shell_layers bottom_shell_layers top_shell_thickness bottom_shell_thickness top_surface_pattern gap_fill_target gap_infill_speed filter_out_gap_fill',
    'INFILL_COST': 'sparse_infill_density sparse_infill_pattern',
    'HOLE_CLEARANCE': 'xy_hole_compensation xy_contour_compensation elefant_foot_compensation',
    'TRAVEL_SURFACE': 'reduce_crossing_wall seam_position filament.filament_retraction_length filament.filament_retraction_speed',
}
PATHS = {
    'FINE_DETAIL': ['outer_wall', 'inner_wall', 'gap_infill'],
    'THIN_CONNECTION': ['internal_solid_infill'],
    'OVERHANG': ['outer_wall', 'inner_wall', 'overhang_wall', 'support', 'support_interface'],
    'BRIDGE': ['bridge', 'internal_bridge', 'overhang_wall'],
    'FLOATING_START': ['support', 'support_interface'],
    'BED_ADHESION': ['brim', 'skirt', 'support'],
    'TALL_SLENDER': ['outer_wall'],
    'SMALL_PERIMETER': ['outer_wall', 'inner_wall'],
    'SHELL_SURFACE': ['top_surface', 'bottom_surface', 'internal_solid_infill', 'gap_infill'],
    'INFILL_COST': ['sparse_infill'],
}
# Current SliceAnalysis::quality_role contract; uncollected roles are not zero.
COLLECTED_ROLES = {'inner_wall', 'outer_wall', 'overhang_wall', 'bridge', 'internal_bridge',
                   'internal_solid_infill', 'top_surface', 'bottom_surface', 'gap_infill',
                   'support', 'support_interface', 'support_transition'}
PHYSICAL = {
    'FINE_DETAIL': '细节实际成形、加宽后的尺寸与脆弱性',
    'THIN_CONNECTION': '层间结合、承载强度；实体填充不等于强度合格',
    'RELIEF': '浮雕/薄片实际可见性、末端成形',
    'CURVED_SURFACE': '阶梯观感、光泽和表面粗糙度',
    'OVERHANG': '卷边、下垂、支撑接触面质量和拆除损伤',
    'BRIDGE': '桥丝张紧、下垂和锚定；慢速/低流量不保证更好',
    'FLOATING_START': '实际承托稳定性及脱落风险',
    'BED_ADHESION': '床面清洁、调平、附着力、翘曲和脱模',
    'TALL_SLENDER': '机架/模型动态响应和振纹',
    'SMALL_PERIMETER': '小孔/小柱实际尺寸、积热和表面质量',
    'SMALL_LAYER': '局部温度、风道效能、层间结合',
    'FLOW_LIMIT': '给定材料/温度/喷嘴的实际熔融能力及挤出稳定性',
    'SHELL_SURFACE': '封顶完整度、表面纹理和细线沉积',
    'INFILL_COST': '实际刚度与承载；无用途时不自动减料',
    'HOLE_CLEARANCE': '目标公差、收缩、孔径和装配间隙',
    'TRAVEL_SURFACE': '拉丝、接缝、擦碰和湿料影响',
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def normalized(value):
    if isinstance(value, list):
        return normalized(value[0]) if len(value) == 1 else None
    if isinstance(value, bool):
        return '1' if value else '0'
    if value is None or isinstance(value, dict):
        return None
    text = str(value).strip()
    if text in ('true', 'false'):
        return '1' if text == 'true' else '0'
    percent = text.endswith('%')
    try:
        number = Decimal(text[:-1] if percent else text)
        if not number.is_finite():
            return None
        return str(number.normalize()) + ('%' if percent else '')
    except InvalidOperation:
        return text


def config_from_facts(facts):
    values = {}
    for line in facts['configuration']:
        if line.startswith('; ') and ' = ' in line:
            key, value = line[2:].split(' = ', 1)
            if key in values and (normalized(value) is None or normalized(values[key]) != normalized(value)):
                raise ValueError('Duplicate effective parameter: ' + key)
            values[key] = value.strip()
    return values


def inherited_retraction(snapshot, effective, key):
    """Resolve an explicit single-material nil, not an absent/unknown override.

    PrintApply overlays non-nil filament values onto machine retraction keys.
    GCode::append_full_config exports the raw full config and omits nil options;
    therefore absence of the filament key is expected, but the machine key must
    still match the frozen machine. This is configuration evidence, not motion.
    """
    machine_key = key.removeprefix('filament_')

    def amount(value):
        if isinstance(value, list):
            value = value[0] if len(value) == 1 else None
        if type(value) not in (str, int, float):
            return None
        try:
            number = Decimal(str(value).strip())
            return number if number.is_finite() and number >= 0 else None
        except InvalidOperation:
            return None

    machine = snapshot.get('machine')
    frozen = machine.get(machine_key) if isinstance(machine, dict) else None
    exported = effective.get(machine_key)
    a, b = amount(frozen), amount(exported)
    resolution = {'source': 'MACHINE_INHERITED', 'machine_key': machine_key,
                  'frozen_machine_value': frozen, 'exported_machine_value': exported,
                  'resolved_exported_value': format(b, 'f') if b is not None else None,
                  'actual_motion_verified': False}
    if key in effective and normalized(effective[key]) != 'nil':
        return ('MISMATCH' if amount(effective[key]) is not None else 'UNVERIFIED'), resolution
    return ('UNVERIFIED' if a is None or b is None else 'MATCH' if a == b else 'MISMATCH'), resolution


def parameter_execution_context(effective):
    """Resolve configured semantics only; these are not predicted motion or fan commands."""
    def numeric(key):
        value = normalized(effective.get(key))
        try:
            result = float(value)
            return result if math.isfinite(result) else None
        except (TypeError, ValueError):
            return None

    def flag(key):
        value = normalized(effective.get(key))
        return None if value not in ('0', '1') else value == '1'

    bridge = numeric('bridge_speed')
    bridge = bridge if bridge is not None and bridge > 0 else None
    internal_raw = normalized(effective.get('internal_bridge_speed'))
    internal = numeric('internal_bridge_speed')
    if internal_raw is not None and internal_raw.endswith('%'):
        try:
            internal = float(internal_raw[:-1]) * bridge / 100 if bridge is not None else None
        except (ValueError, OverflowError):
            internal = None
    internal = internal if internal is not None and math.isfinite(internal) and internal > 0 else None
    master = flag('enable_overhang_bridge_fan')
    internal_fan = numeric('internal_bridge_fan_speed')
    external_fan = numeric('overhang_fan_speed')
    external_fan = external_fan if external_fan is not None and 0 <= external_fan <= 100 else None
    fan_source = ('INHERIT_OVERHANG' if internal_fan == -1 else 'INDEPENDENT'
                  if internal_fan is not None and 0 <= internal_fan <= 100 else 'UNKNOWN')
    # A disabled master does not command a fan speed of 0: ordinary cooling still applies.
    internal_target = (external_fan if fan_source == 'INHERIT_OVERHANG' else internal_fan
                       if fan_source == 'INDEPENDENT' else None) if master is True else None
    return {
        'scope': 'effective_configuration_semantics_not_actual_path_execution',
        'bridge': {'requested_external_speed_mm_s': bridge, 'requested_internal_speed_mm_s': internal,
                   'internal_percentage_basis': 'bridge_speed',
                   'limitations': ['内外桥接分别取值；首层、流量上限及冷却可继续限速。',
                                   '速度范围命中不证明两端锚定、张紧或下垂已合格。']},
        'feature_fan': {'master_enabled': master, 'internal_setting_source': fan_source,
                        'eligible_external_target_percent': external_fan if master is True else None,
                        'eligible_internal_target_percent': internal_target,
                        'actual_command_percent': None,
                        'limitations': ['主开关关闭时专用档不生效，不等于普通风扇关闭。',
                                        '启用仍受实际角色/悬垂阈值、首层禁风和渐增规则控制，须查最终M106。',
                                        '内部档-1继承悬垂档；独立0是有效请求值，不是缺数据。']},
        'cooling': {'enabled': flag('slow_down_for_layer_cooling'),
                    'configured_time_s': numeric('slow_down_layer_time'),
                    'minimum_speed_mm_s': numeric('slow_down_min_speed'),
                    'outer_wall_exempt': flag('dont_slow_down_outer_wall'),
                    'native_cooling_group_count': None, 'physical_cooling_verified': False,
                    'limitations': ['报告按最终G-code层序统计，原生可合并前置独立支撑层与随后模型层；不能将低于阈值数当冷却失效数。',
                                    '目标时间不是强制等待；最低速度、外墙豁免及不可调路径可能限制降速。',
                                    '冷却内部按路径/速度计时，与最终含加减速估时不同；未提供组边界时不推算达标组数。',
                                    '最低速度0表示不额外设下限，不是停止运动，也不表示适合该机料。']},
        'automatic_parameter_changes': {},
    }


def residual_diagnostics(report):
    """Attribute validated peaks, not physical defects or an automatic repair recipe."""
    specs = {
        'unprinted_peak': ('completely_unprinted_island_area_mm2', 'unprinted_area_mm2',
                           ('retained_region_area_mm2', 'absent_region_area_mm2')),
        'hole_intrusion_peak': ('hole_intrusion_area_mm2', 'intrusion_area_mm2',
                               ('wall_area_mm2', 'gap_fill_area_mm2', 'bridge_area_mm2',
                                'other_fill_area_mm2', 'mixed_area_mm2')),
        'solid_fill_peak': ('solid_fill_uncovered_area_mm2', 'uncovered_area_mm2',
                           ('surface_area_mm2', 'internal_solid_area_mm2', 'bridge_area_mm2', 'mixed_area_mm2')),
    }
    checks = {
        'unprinted_peak': {
            'retained_region_area_mm2': '仍在有效截面：追踪墙生成、短线过滤和填充阶段；不能直接归咎于输入模型被删除。',
            'absent_region_area_mm2': '未保留在有效区域：先检查截面简化、补偿和区域构造，再检查生成路径。',
        },
        'hole_intrusion_peak': {
            'wall_area_mm2': '检查墙中心线、宽度扫掠和轮廓偏移；gap_fill参数不是这部分的直接来源。',
            'gap_fill_area_mm2': '定位实际补缝路径与孔边界，核对gap_fill_target和filter_out_gap_fill。',
            'bridge_area_mm2': '该分组包括桥接/悬垂路径：核对名义宽度扫掠与承托，不等于中心线穿孔，不从面积反推bridge_flow。',
            'other_fill_area_mm2': '定位非桥接填充路径与孔边界，再确认其填充角色和重叠设置。',
            'mixed_area_mm2': '多个角色重叠，不能全部算到单一参数名下。',
        },
        'solid_fill_peak': {
            'surface_area_mm2': '检查表面实体目标及其实际覆盖；不等于全部都是顶面缺陷。',
            'internal_solid_area_mm2': '检查内部实体填充目标及间距；不等于可见封顶破损。',
            'bridge_area_mm2': '先区分内部/外部桥接，再查厚桥模式、桥丝间距和承托；不能当作普通封顶漏填或直接增加流量。',
            'mixed_area_mm2': '目标类型重叠部分单列，不能重复累计为多种缺陷。',
        },
    }
    result = {'scope': 'single_instance_peak_layer_attribution_only', 'root_cause_verified': False,
              'automatic_parameter_changes': {}, 'peaks': {}}
    try:
        layers = layer_groups(report)
        coverage = report['objects']['groups'][0]['planned_layer_coverage']
    except (ValueError, TypeError, KeyError):
        result['peaks'] = {key: {'status': 'UNAVAILABLE', 'reason': 'MODEL_LAYER_IDENTITY_UNAVAILABLE'} for key in specs}
        return result
    for key, (layer_metric, total_key, parts) in specs.items():
        peak = coverage.get(key)
        if not isinstance(peak, dict):
            result['peaks'][key] = {'status': 'UNAVAILABLE', 'reason': 'ATTRIBUTION_NOT_REPORTED'}
            continue
        if not all(row.get('complete') is True and row.get('invalid_path_count') == 0
                   and row.get('excluded_path_count') == 0 for row in layers):
            result['peaks'][key] = {'status': 'INCOMPLETE', 'reason': 'PARTIAL_MODEL_PATH_COVERAGE'}
            continue
        try:
            if not all(finite(row.get(layer_metric)) and row[layer_metric] >= 0
                       and finite(row.get('print_z_mm')) for row in layers):
                raise ValueError('Invalid layer observations')
            if key == 'solid_fill_peak':
                validate_solid_fill_peak(peak, layers)
            else:
                validate_residual_peak(peak, layers, key == 'hole_intrusion_peak')
        except (AssertionError, ValueError, TypeError, KeyError, IndexError) as error:
            result['peaks'][key] = {'status': 'INVALID', 'reason': str(error)}
            continue
        total = peak[total_key]
        components = [{'kind': part, 'area_mm2': peak[part]} for part in parts]
        result['peaks'][key] = {
            'status': 'OBSERVED' if total > 0 else 'NO_RESIDUAL_OBSERVED',
            'source_scope': peak['scope'], 'layer_index': peak['layer_index'],
            'print_z_mm': peak['print_z_mm'],
            'slice_z_mm': layers[peak['layer_index']]['slice_z_mm'] if total > 0 else None,
            'area_mm2': total, 'exclusive_components': components,
            'manual_checks': [{'kind': part, 'check': checks[key][part]} for part in parts if peak[part] > 0],
            'limitations': ['仅该类最大面积层，不能外推其他层或全模型。',
                            '面积是名义路径/目标几何观察，不是沉积仿真或实物失败判定。',
                            '具体参数因果需同源同截面受控复核；不自动降低线宽、短线阈值或增加流量。'],
        }
    return result


def path_observations(report):
    """Preserve unknowns and locations. Positive geometry area is not a failure score."""
    result = {'geometry_available': False, 'nominal_width_height_available': False}
    try:
        layers = layer_groups(report)
    except (ValueError, TypeError, KeyError):
        layers = []
    keys = ['completely_unprinted_island_area_mm2', 'hole_intrusion_area_mm2',
            'closed_void_fully_covered_count', 'solid_fill_uncovered_area_mm2']
    complete = [row for row in layers if row.get('complete') is True
                and row.get('invalid_path_count') == 0 and row.get('excluded_path_count') == 0
                and all(finite(row.get(key)) and row[key] >= 0 for key in keys + ['print_z_mm'])]
    result.update(geometry_available=bool(layers) and len(complete) == len(layers),
                  observed_layer_count=len(layers), complete_layer_count=len(complete))
    result['geometry_hotspots'] = {key: sorted([
        {k: row[k] for k in ('layer_index', 'slice_z_mm', 'print_z_mm', key)}
        for row in complete if finite(row.get(key)) and row[key] > 0],
        key=lambda row: row[key], reverse=True)[:5] for key in keys}
    narrow = narrow_paths(report)
    result['nominal_width_height_available'] = narrow is not None
    result['width_below_height_roles'] = None if narrow is None else [
        {'extruder_id': key[0], 'role': key[1], 'length_mm': length}
        for key, length in narrow.items() if length > 0]
    result['material_observations'] = report.get('gcode', {}).get('extruders')
    result['small_perimeter_selection'] = report.get('gcode', {}).get('small_perimeter_selection')
    result['residual_diagnostics'] = residual_diagnostics(report)
    return result


def audit(snapshot, decision, features, effective, report):
    decisions = decision.get('decisions')
    if not isinstance(decisions, list) or len(decisions) != len(CATALOG):
        raise ValueError('Expected all 16 independent decisions')
    by_item = {row['item']: row for row in decisions}
    if set(by_item) != set(CATALOG):
        raise ValueError('Duplicate, missing or unknown feature decision')
    binding_fields = ['modelSha256', 'modelScale', 'rotateX', 'rotateY', 'rotateZ']
    plan = snapshot.get('regionalLayerHeightPlan')
    binding = features.get('modelBinding', {})
    if plan is not None and (not isinstance(plan, dict) or not isinstance(binding, dict)
            or any(binding.get(key) is None or binding.get(key) != plan.get(key) for key in binding_fields)):
        raise ValueError('Detection binding does not match the frozen regional plan')
    mismatches, unverified, rows, freeze_mismatches = [], [], [], []
    for scope in ('process', 'filament'):
        overrides = decision.get(scope + 'Overrides', {})
        if not isinstance(overrides, dict):
            raise ValueError('Invalid final override map: ' + scope)
        for key, proposed in overrides.items():
            expected = snapshot.get(scope, {}).get(key)
            if normalized(proposed) is None or normalized(proposed) != normalized(expected):
                freeze_mismatches.append({'key': scope + '.' + key, 'proposed': proposed, 'frozen': expected})
    quality = report.get('gcode', {}).get('quality_paths', {})
    valid_roles = quality.get('scope') == 'final_gcode_positive_extrusion_moves'
    for item, parameters in CATALOG.items():
        record = by_item[item]
        values = []
        for parameter in parameters.split():
            scope, key = parameter.split('.', 1) if '.' in parameter else ('process', parameter)
            expected = snapshot.get(scope, {}).get(key)
            actual = effective.get(key)
            a, b = normalized(expected), normalized(actual)
            state = 'NOT_FROZEN' if expected is None else 'UNVERIFIED' if a is None or b is None else 'MATCH' if a == b else 'MISMATCH'
            resolution = None
            if scope == 'filament' and key in ('filament_retraction_length', 'filament_retraction_speed') and a == 'nil':
                state, resolution = inherited_retraction(snapshot, effective, key)
            if state == 'MISMATCH':
                mismatches.append(item + '.' + parameter)
            elif state != 'MATCH':
                unverified.append(item + '.' + parameter)
            changes = [change for change in record.get('changes', []) if change.get('scope') == scope and change.get('key') == key]
            value = {'key': parameter, 'frozen': expected, 'effective': actual, 'delivery': state, 'matching_changes': changes}
            if resolution is not None:
                value['resolution'] = resolution
            values.append(value)
        rows.append({'item': item, 'evidence_state': record.get('evidence'), 'action': record.get('action'),
                     'reason': record.get('reason'), 'measurements': {key: features.get(key) for key in record.get('evidenceKeys', [])},
                     'parameters': values,
                     'related_role_observations': [row for row in quality.get('rows', []) if row.get('role') in PATHS.get(item, [])] if valid_roles else None,
                     'roles_not_collected_by_report': [role for role in PATHS.get(item, []) if role not in COLLECTED_ROLES],
                     'path_causality_verified': False, 'physical_validation_required': PHYSICAL[item]})
    return {'schema_version': 'offline-feature-parameter-audit-v1', 'items': rows,
            'configuration_mismatches': mismatches, 'unverified_parameters': unverified,
            'decision_freeze_mismatches': freeze_mismatches,
            'path_observations': path_observations(report),
            'parameter_execution_context': parameter_execution_context(effective),
            'regional_plan': plan, 'detection_plan_binding_verified': plan is not None,
            'physical_quality_verified': False, 'selected_production_artifact': None,
            'limitations': ['参数写入G-code不等于该区域已命中，更不等于实物改善。',
                           '角色汇总可能混合多个区域，不把所有路径变化归因于一个特征。',
                           '不同层位的面积不能相加比较优劣；正孔侵入/未覆盖面积需定位，不自动增流量。',
                           '低于冷却时间或略超流量上限是观察，不自动修改材料限制。',
                           '局部计划范围执行请另外运行check_regional_layer_scope；此处仅列冻结计划。',
                           '没有自动实印准入；软件问题需修复，已知物理边界需评审后才能安排代表模型实印。']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--request', type=Path, required=True, help='Frozen PDS slice request')
    parser.add_argument('--decision', type=Path, required=True, help='PAM decision JSON')
    parser.add_argument('--features', type=Path, required=True, help='Detection result JSON')
    parser.add_argument('--artifacts', type=Path, required=True, help='Existing run containing output/')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output exists; choose a new evidence file')
    report, facts, identity = load_artifacts(args.artifacts)
    request, decision, detected = map(read_json, (args.request, args.decision, args.features))
    if detected.get('status') != 'success':
        parser.error('Detection did not succeed')
    result = audit(request['configSnapshot'], decision, detected['features'], config_from_facts(facts), report)
    result['artifact_identity'] = identity
    result['input_sha256'] = {str(path.resolve()): digest(path) for path in (args.request, args.decision, args.features)}
    save_json(args.output, result)
    print(f"16 items; {len(result['decision_freeze_mismatches'])} decision/freeze mismatches; "
          f"{len(result['configuration_mismatches'])} delivery mismatches; "
          f"{len(result['unverified_parameters'])} unverified parameters; physical quality NOT verified")
    return 1 if result['configuration_mismatches'] or result['decision_freeze_mismatches'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
