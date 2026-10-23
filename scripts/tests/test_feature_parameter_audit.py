import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_feature_parameters import CATALOG, audit, config_from_facts, normalized, parameter_execution_context, path_observations, residual_diagnostics


class FeatureParameterAuditTest(unittest.TestCase):
    def setUp(self):
        self.snapshot = {'process': {}, 'filament': {}}
        self.effective = {}
        for params in CATALOG.values():
            for param in params.split():
                scope, key = param.split('.', 1) if '.' in param else ('process', param)
                self.snapshot[scope][key] = ['1'] if scope == 'filament' else '1'
                self.effective[key] = '1.0'
        self.decision = {'decisions': [{'item': item, 'evidence': 'UNKNOWN', 'action': 'TEMPLATE_RETAINED',
            'reason': 'test', 'evidenceKeys': ['printGeometry'], 'changes': []} for item in CATALOG]}
        self.features = {'printGeometry': {'thinWallRisk': True}}

    def run_audit(self):
        return audit(self.snapshot, self.decision, self.features, self.effective, {})

    def test_all_16_include_unchanged_values_without_claiming_execution_or_quality(self):
        before = copy.deepcopy((self.snapshot, self.decision, self.features, self.effective))
        result = self.run_audit()
        self.assertEqual(16, len(result['items']))
        self.assertEqual([], result['configuration_mismatches'])
        self.assertEqual([], result['unverified_parameters'])
        self.assertFalse(result['physical_quality_verified'])
        self.assertIsNone(result['selected_production_artifact'])
        self.assertTrue(all(not row['path_causality_verified'] and row['parameters'] and row['physical_validation_required']
                            for row in result['items']))
        self.assertEqual(before, (self.snapshot, self.decision, self.features, self.effective))

    def test_missing_is_not_zero_and_percent_is_not_absolute(self):
        self.snapshot['process']['small_perimeter_speed'] = '50%'
        self.effective['small_perimeter_speed'] = '50'
        del self.effective['bridge_flow']
        del self.snapshot['process']['brim_width']
        result = self.run_audit()
        self.assertIn('SMALL_PERIMETER.small_perimeter_speed', result['configuration_mismatches'])
        self.assertIn('BRIDGE.bridge_flow', result['unverified_parameters'])
        self.assertIn('BED_ADHESION.brim_width', result['unverified_parameters'])

    def test_numeric_normalization_never_collapses_multi_material_or_invalid(self):
        self.assertEqual(normalized(['12.0']), normalized('12'))
        self.assertEqual(normalized(True), normalized('1'))
        self.assertEqual('nil', normalized('nil'))
        for value in (None, ['12', '12'], float('nan'), float('inf'), {}):
            self.assertIsNone(normalized(value))

    def test_matching_final_file_cannot_hide_dropped_decision_override(self):
        self.decision['processOverrides'] = {'small_perimeter_speed': '20'}
        result = self.run_audit()
        self.assertEqual([], result['configuration_mismatches'])
        self.assertEqual([{'key': 'process.small_perimeter_speed', 'proposed': '20', 'frozen': '1'}],
                         result['decision_freeze_mismatches'])
        self.snapshot['process']['small_perimeter_speed'] = '20.0'
        self.effective['small_perimeter_speed'] = '20'
        self.assertEqual([], self.run_audit()['decision_freeze_mismatches'])
        self.decision['filamentOverrides'] = ['invalid']
        with self.assertRaises(ValueError): self.run_audit()

    def test_duplicate_or_missing_feature_cannot_look_complete(self):
        self.decision['decisions'][0] = self.decision['decisions'][1]
        with self.assertRaises(ValueError): self.run_audit()
        self.decision['decisions'].pop()
        with self.assertRaises(ValueError): self.run_audit()

    def test_regional_plan_requires_same_measured_model_and_pose(self):
        binding = {'modelSha256': 'a' * 64, 'modelScale': 1, 'rotateX': 0, 'rotateY': 0, 'rotateZ': 0}
        self.snapshot['regionalLayerHeightPlan'] = binding.copy()
        with self.assertRaises(ValueError): self.run_audit()
        self.features['modelBinding'] = binding.copy()
        self.assertTrue(self.run_audit()['detection_plan_binding_verified'])
        self.features['modelBinding']['rotateY'] = 90
        with self.assertRaises(ValueError): self.run_audit()

    def test_effective_config_duplicates_rejected(self):
        self.assertEqual({'a': '1'}, config_from_facts({'configuration': ['; CONFIG_BLOCK_START', '; a = 1']}))
        self.assertEqual({'a': '1'}, config_from_facts({'configuration': ['; a = 1.000', '; a = 1']}))
        with self.assertRaises(ValueError):
            config_from_facts({'configuration': ['; a = 1', '; a = 2']})

    def test_nil_retraction_is_verified_against_frozen_and_exported_machine_value(self):
        self.snapshot['machine'] = {'retraction_length': ['4'], 'retraction_speed': ['40']}
        for key, value in (('retraction_length', '4'), ('retraction_speed', '40')):
            self.snapshot['filament']['filament_' + key] = ['nil']
            self.effective.pop('filament_' + key)
            self.effective[key] = value
        before = copy.deepcopy((self.snapshot, self.effective))
        result = self.run_audit()
        self.assertEqual([], result['unverified_parameters'])
        params = {p['key']: p for r in result['items'] for p in r['parameters']}
        length = params['filament.filament_retraction_length']
        self.assertEqual('MATCH', length['delivery'])
        self.assertEqual('MACHINE_INHERITED', length['resolution']['source'])
        self.assertEqual('4', length['resolution']['resolved_exported_value'])
        self.assertEqual(['nil'], length['frozen'])
        self.assertEqual(before, (self.snapshot, self.effective))

    def test_inherited_retraction_cannot_hide_missing_or_changed_machine_value(self):
        key = 'filament_retraction_length'
        self.snapshot['filament'][key] = ['nil']
        self.snapshot['machine'] = {'retraction_length': ['4']}
        self.effective.pop(key)
        for actual in (None, '5', '4,4', 'NaN'):
            with self.subTest(actual=actual):
                self.effective['retraction_length'] = actual
                result = self.run_audit()
                expected = 'configuration_mismatches' if actual == '5' else 'unverified_parameters'
                self.assertIn('TRAVEL_SURFACE.filament.' + key, result[expected])

    def test_nil_retraction_cannot_hide_unexpected_filament_override(self):
        self.snapshot['filament']['filament_retraction_length'] = ['nil']
        self.snapshot['machine'] = {'retraction_length': ['4']}
        self.effective['retraction_length'] = '4'
        self.effective['filament_retraction_length'] = '4'
        self.assertIn('TRAVEL_SURFACE.filament.filament_retraction_length', self.run_audit()['configuration_mismatches'])

    def test_unknown_or_multimaterial_inheritance_is_not_resolved_by_first_machine(self):
        key = 'filament_retraction_length'
        self.effective.pop(key)
        self.effective['retraction_length'] = '4'
        for filament, machine in ((None, ['4']), (['nil'], ['4', '4']), (['nil', 'nil'], ['4']),
                                  (['nil'], ['-1']), (['nil'], [True])):
            with self.subTest(filament=filament, machine=machine):
                self.snapshot['filament'][key] = filament
                self.snapshot['machine'] = {'retraction_length': machine}
                self.assertIn('TRAVEL_SURFACE.filament.' + key, self.run_audit()['unverified_parameters'])

    def test_explicit_zero_machine_retraction_is_valid_not_missing(self):
        self.snapshot['filament']['filament_retraction_length'] = ['nil']
        self.snapshot['machine'] = {'retraction_length': ['0']}
        self.effective.pop('filament_retraction_length')
        self.effective['retraction_length'] = '0'
        self.assertNotIn('TRAVEL_SURFACE.filament.filament_retraction_length', self.run_audit()['unverified_parameters'])

    def test_absent_or_partial_path_evidence_never_means_no_defects(self):
        result = path_observations({})
        self.assertFalse(result['geometry_available'])
        self.assertIsNone(result['width_below_height_roles'])
        self.assertIsNone(result['material_observations'])
        row = {'layer_index': 0, 'height_mm': .2, 'slice_z_mm': .1, 'print_z_mm': .2, 'complete': True,
               'excluded_path_count': 0, 'invalid_path_count': 0, 'completely_unprinted_island_area_mm2': .1,
               'hole_intrusion_area_mm2': .2, 'closed_void_fully_covered_count': 1, 'solid_fill_uncovered_area_mm2': .3}
        report = {'objects': {'source_object_count': 1, 'instance_count': 1, 'groups': [
            {'model_layer_count': 1, 'planned_layer_coverage': {'scope': 'per_instance_model_paths_before_gcode', 'layers': [row]}}]}}
        result = path_observations(report)
        self.assertTrue(result['geometry_available'])
        self.assertEqual(.2, result['geometry_hotspots']['hole_intrusion_area_mm2'][0]['print_z_mm'])
        del row['hole_intrusion_area_mm2']
        self.assertFalse(path_observations(report)['geometry_available'])

    def test_uncollected_roles_are_not_presented_as_zero_paths(self):
        report = {'gcode': {'quality_paths': {'scope': 'final_gcode_positive_extrusion_moves',
                  'rows': [{'role': 'bottom_surface', 'length_mm': 3}]}}}
        result = audit(self.snapshot, self.decision, self.features, self.effective, report)
        by_item = {row['item']: row for row in result['items']}
        self.assertEqual(['sparse_infill'], by_item['INFILL_COST']['roles_not_collected_by_report'])
        self.assertEqual(['brim', 'skirt'], by_item['BED_ADHESION']['roles_not_collected_by_report'])
        self.assertEqual([{'role': 'bottom_surface', 'length_mm': 3}], by_item['SHELL_SURFACE']['related_role_observations'])


class ParameterExecutionContextTest(unittest.TestCase):
    def test_internal_bridge_percentage_uses_external_bridge_not_outer_wall(self):
        effective = {'bridge_speed': '25', 'internal_bridge_speed': '150%', 'outer_wall_speed': '60'}
        before = copy.deepcopy(effective)
        result = parameter_execution_context(effective)
        self.assertEqual(25, result['bridge']['requested_external_speed_mm_s'])
        self.assertEqual(37.5, result['bridge']['requested_internal_speed_mm_s'])
        self.assertEqual({}, result['automatic_parameter_changes'])
        self.assertEqual(before, effective)
        effective['internal_bridge_speed'] = '20'
        self.assertEqual(20, parameter_execution_context(effective)['bridge']['requested_internal_speed_mm_s'])

    def test_disabled_fan_does_not_misreport_dormant_100_percent_or_zero_actual_speed(self):
        result = parameter_execution_context({'enable_overhang_bridge_fan': '0',
            'overhang_fan_speed': '100', 'internal_bridge_fan_speed': '-1'})['feature_fan']
        self.assertFalse(result['master_enabled'])
        self.assertEqual('INHERIT_OVERHANG', result['internal_setting_source'])
        self.assertIsNone(result['eligible_external_target_percent'])
        self.assertIsNone(result['eligible_internal_target_percent'])
        self.assertIsNone(result['actual_command_percent'])

    def test_internal_fan_inherit_independent_zero_and_missing_are_distinct(self):
        effective = {'enable_overhang_bridge_fan': '1', 'overhang_fan_speed': '80', 'internal_bridge_fan_speed': '-1'}
        self.assertEqual(80, parameter_execution_context(effective)['feature_fan']['eligible_internal_target_percent'])
        effective['internal_bridge_fan_speed'] = '0'
        result = parameter_execution_context(effective)['feature_fan']
        self.assertEqual('INDEPENDENT', result['internal_setting_source'])
        self.assertEqual(0, result['eligible_internal_target_percent'])
        for value in (None, '-2', '101', 'NaN', ['30', '50']):
            effective['internal_bridge_fan_speed'] = value
            with self.subTest(value=value):
                result = parameter_execution_context(effective)['feature_fan']
                self.assertEqual('UNKNOWN', result['internal_setting_source'])
                self.assertIsNone(result['eligible_internal_target_percent'])

    def test_no_material_collapse_or_missing_percentage_basis(self):
        for effective in ({}, {'bridge_speed': ['25', '30'], 'internal_bridge_speed': '150%'},
                          {'bridge_speed': '0', 'internal_bridge_speed': '150%'},
                          {'bridge_speed': '25', 'internal_bridge_speed': '1e999%'}):
            with self.subTest(effective=effective):
                result = parameter_execution_context(effective)
                self.assertIsNone(result['bridge']['requested_internal_speed_mm_s'])
                self.assertIsNone(result['feature_fan']['master_enabled'])
                self.assertIsNone(result['cooling']['enabled'])

    def test_cooling_floor_and_exemption_do_not_claim_thermal_pass_or_group_count(self):
        result = parameter_execution_context({'slow_down_for_layer_cooling': '1',
            'slow_down_layer_time': '10', 'slow_down_min_speed': '0', 'dont_slow_down_outer_wall': '1'})['cooling']
        self.assertTrue(result['enabled'])
        self.assertEqual(0, result['minimum_speed_mm_s'])
        self.assertTrue(result['outer_wall_exempt'])
        self.assertIsNone(result['native_cooling_group_count'])
        self.assertFalse(result['physical_cooling_verified'])
        self.assertIn('原生可合并', result['limitations'][0])
        self.assertIn('不是强制等待', result['limitations'][1])

    def test_complete_parameter_associations_include_gating_and_internal_roles(self):
        for group, key in [('BRIDGE', 'filament.internal_bridge_fan_speed'),
                           ('OVERHANG', 'filament.overhang_fan_threshold'),
                           ('SMALL_LAYER', 'filament.dont_slow_down_outer_wall'),
                           ('SMALL_LAYER', 'filament.close_fan_the_first_x_layers'),
                           ('SMALL_LAYER', 'filament.full_fan_speed_layer'),
                           ('SMALL_LAYER', 'filament.fan_cooling_layer_time')]:
            self.assertIn(key, CATALOG[group].split())


class ResidualDiagnosticTest(unittest.TestCase):
    def setUp(self):
        self.layers = [{'layer_index': i, 'height_mm': .2, 'slice_z_mm': .1 + .2*i,
                        'print_z_mm': .2 + .2*i, 'complete': True, 'excluded_path_count': 0,
                        'invalid_path_count': 0, 'completely_unprinted_island_area_mm2': .4*i,
                        'hole_intrusion_area_mm2': .5*i, 'solid_fill_uncovered_area_mm2': 45.*i,
                        'closed_void_fully_covered_count': 0} for i in range(2)]
        self.coverage = {'scope': 'per_instance_model_paths_before_gcode', 'layers': self.layers,
            'unprinted_peak': {'scope': 'largest_unprinted_layer_region_slices', 'complete': True,
                'layer_index': 1, 'print_z_mm': .4, 'unprinted_area_mm2': .4,
                'retained_region_area_mm2': .4, 'absent_region_area_mm2': 0},
            'hole_intrusion_peak': {'scope': 'largest_hole_intrusion_layer_exclusive_path_kinds', 'complete': True,
                'layer_index': 1, 'print_z_mm': .4, 'intrusion_area_mm2': .5, 'wall_area_mm2': .48,
                'gap_fill_area_mm2': 0, 'bridge_area_mm2': .02, 'other_fill_area_mm2': 0, 'mixed_area_mm2': 0},
            'solid_fill_peak': {'scope': 'largest_solid_uncovered_layer_exclusive_target_kinds', 'complete': True,
                'layer_index': 1, 'print_z_mm': .4, 'uncovered_area_mm2': 45., 'surface_area_mm2': 2.,
                'internal_solid_area_mm2': 6., 'bridge_area_mm2': 37., 'mixed_area_mm2': 0}}
        self.report = {'objects': {'source_object_count': 1, 'instance_count': 1,
                                  'groups': [{'model_layer_count': 2, 'planned_layer_coverage': self.coverage}]}}

    def test_same_layer_attribution_separates_bridge_surface_and_walls_without_autotuning(self):
        original = copy.deepcopy(self.report)
        result = residual_diagnostics(self.report)
        self.assertEqual({}, result['automatic_parameter_changes'])
        self.assertFalse(result['root_cause_verified'])
        for peak in result['peaks'].values():
            self.assertEqual('OBSERVED', peak['status'])
            self.assertEqual(self.layers[1]['slice_z_mm'], peak['slice_z_mm'])
        hole = result['peaks']['hole_intrusion_peak']
        self.assertEqual(['wall_area_mm2', 'bridge_area_mm2'], [row['kind'] for row in hole['manual_checks']])
        self.assertIn('桥接/悬垂', hole['manual_checks'][1]['check'])
        self.assertIn('不等于中心线穿孔', hole['manual_checks'][1]['check'])
        solid = result['peaks']['solid_fill_peak']
        self.assertEqual(37, next(row['area_mm2'] for row in solid['exclusive_components'] if row['kind'] == 'bridge_area_mm2'))
        unprinted = result['peaks']['unprinted_peak']
        self.assertEqual(['retained_region_area_mm2'], [row['kind'] for row in unprinted['manual_checks']])
        self.assertEqual(original, self.report)
        self.assertEqual(result, path_observations(self.report)['residual_diagnostics'])

    def test_wrong_peak_layer_z_scope_or_partition_is_rejected_not_explained(self):
        changes = [('layer_index', 0), ('print_z_mm', .6), ('scope', 'unknown'),
                   ('bridge_area_mm2', 40.), ('surface_area_mm2', -1.),
                   ('bridge_area_mm2', float('nan')), ('complete', False)]
        for key, value in changes:
            report = copy.deepcopy(self.report)
            report['objects']['groups'][0]['planned_layer_coverage']['solid_fill_peak'][key] = value
            with self.subTest(key=key, value=value):
                peak = residual_diagnostics(report)['peaks']['solid_fill_peak']
                self.assertEqual('INVALID', peak['status'])
                self.assertNotIn('exclusive_components', peak)

    def test_absent_partial_or_multiple_instances_never_mean_no_defects(self):
        del self.coverage['solid_fill_peak']
        self.assertEqual('UNAVAILABLE', residual_diagnostics(self.report)['peaks']['solid_fill_peak']['status'])
        for key, value in [('complete', False), ('invalid_path_count', 1), ('excluded_path_count', 1)]:
            report = copy.deepcopy(self.report)
            report['objects']['groups'][0]['planned_layer_coverage']['layers'][0][key] = value
            with self.subTest(key=key):
                self.assertEqual('INCOMPLETE', residual_diagnostics(report)['peaks']['unprinted_peak']['status'])
        self.report['objects']['instance_count'] = 2
        self.assertTrue(all(row['status'] == 'UNAVAILABLE' for row in residual_diagnostics(self.report)['peaks'].values()))

    def test_no_residual_requires_valid_zero_partition_and_no_location(self):
        for layer in self.layers:
            layer['completely_unprinted_island_area_mm2'] = 0
        peak = self.coverage['unprinted_peak']
        peak.update(layer_index=None, print_z_mm=None, unprinted_area_mm2=0,
                    retained_region_area_mm2=0, absent_region_area_mm2=0)
        result = residual_diagnostics(self.report)['peaks']['unprinted_peak']
        self.assertEqual('NO_RESIDUAL_OBSERVED', result['status'])
        self.assertEqual([], result['manual_checks'])
        self.assertIsNone(result['slice_z_mm'])
        peak['layer_index'] = 0
        self.assertEqual('INVALID', residual_diagnostics(self.report)['peaks']['unprinted_peak']['status'])

    def test_partition_tolerance_does_not_become_a_quality_threshold(self):
        self.coverage['unprinted_peak']['absent_region_area_mm2'] = 1e-9
        result = residual_diagnostics(self.report)['peaks']['unprinted_peak']
        self.assertEqual('OBSERVED', result['status'])
        self.assertEqual(1e-9, result['exclusive_components'][1]['area_mm2'])
        self.assertEqual('absent_region_area_mm2', result['manual_checks'][1]['kind'])


if __name__ == '__main__':
    unittest.main()
