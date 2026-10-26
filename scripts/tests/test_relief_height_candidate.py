import copy
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import evaluate_relief_height_candidate as tool
from evaluate_layer_height_candidate import COMMON_SCOPE


def features(bands=None):
    return {'modelSizeZMm': 5., 'printGeometry': {
        'scope': 'horizontal_sections_and_planar_steps', 'planarHeightBandScope': 'single_model_bed_relative_z',
        'planarHeightScanComplete': True, 'planarHeightBandsComplete': True,
        'planarHeightBands': bands if bands is not None else [{'bottomZMm': 2., 'topZMm': 2.34}]}}


PROCESS = {'layer_height': '.2', 'initial_layer_print_height': '.24', 'raft_layers': '0', 'spiral_mode': '0',
           'outer_wall_line_width': '.42'}
MACHINE = {'min_layer_height': ['.08'], 'max_layer_height': ['.32']}


def report(heights):
    layers, z = [], 0.
    for index, height in enumerate(heights):
        layers.append({'layer_index': index, 'bottom_z_mm': z, 'print_z_mm': z + height,
                       'invalid_path_count': 0, 'excluded_path_count': 0,
                       'slice_z_mm': z + height / 2, 'height_mm': height, 'complete': True,
                       'section_area_mm2': 10., 'planned_path_area_mm2': 10., 'hole_area_mm2': 0., 'section_island_count': 1,
                       'closed_void_region_count': 0, 'completely_unprinted_island_area_mm2': 0.,
                       'completely_unprinted_island_count': 0, 'hole_intrusion_area_mm2': 0.,
                       'closed_void_fully_covered_count': 0})
        z += height
    return {'objects': {'source_object_count': 1, 'instance_count': 1, 'groups': [{
        'model_layer_count': len(layers), 'planned_layer_coverage': {'scope': COMMON_SCOPE,
                                                                  'layers': layers}}]},
        'gcode': {'quality_paths': {'scope': 'final_gcode_positive_extrusion_moves', 'rows': [{
            'extruder_id': 0, 'role': 'outer_wall', 'complete': True, 'width_height_observation': {
                'scope': 'same_move_nominal_width_height_non_bridge', 'complete': True,
                'applicable': True, 'below_height_path_length_mm': 0.}}]}}}


class PlannerTest(unittest.TestCase):
    def test_reachable_target_is_local_and_inputs_unchanged(self):
        data, process, machine = features(), copy.deepcopy(PROCESS), copy.deepcopy(MACHINE)
        before = copy.deepcopy((data, process, machine))
        plan = tool.make_plan(data, process, machine)
        self.assertEqual(plan['base_mm'], .2)
        self.assertEqual(plan['first_mm'], .24)
        self.assertAlmostEqual(float(plan['height_ranges'][0]['range_params']['layer_height']), .17)
        self.assertEqual((plan['height_ranges'][0]['min_z'], plan['height_ranges'][0]['max_z']), (2., 2.34))
        self.assertEqual(before, (data, process, machine))

    def test_unreachable_satisfied_and_first_layer_bands_do_not_generate_ranges(self):
        for lo, hi, reason in ((2, 2.05, 'TARGET_BELOW_USABLE_FLOOR'), (2, 2.5, 'BASE_NOMINAL_TARGET_MET'),
                              (.1, .44, 'FIRST_LAYER_INTERSECTION')):
            with self.subTest(reason=reason):
                plan = tool.make_plan(features([{'bottomZMm': lo, 'topZMm': hi}]), PROCESS, MACHINE)
                self.assertEqual(plan['height_ranges'], [])
                self.assertEqual(plan['skipped'][0]['reason'], reason)
        self.assertEqual(tool.make_plan(features([]), PROCESS, MACHINE)['height_ranges'], [])

    def test_overlapping_targets_partition_by_tighter_height(self):
        bands = [{'bottomZMm': 2., 'topZMm': 2.38}, {'bottomZMm': 2.2, 'topZMm': 2.54}]
        ranges = tool.make_plan(features(bands), PROCESS, MACHINE)['height_ranges']
        self.assertEqual([(r['min_z'], r['max_z']) for r in ranges], [(2., 2.2), (2.2, 2.54)])
        self.assertAlmostEqual(float(ranges[0]['range_params']['layer_height']), .19)
        self.assertAlmostEqual(float(ranges[1]['range_params']['layer_height']), .17)
        self.assertEqual(ranges, tool.make_plan(features(list(reversed(bands))), PROCESS, MACHINE)['height_ranges'])

    def test_bad_identity_partial_coordinates_or_bounds_rejected(self):
        for key, value in (('planarHeightBandScope', None), ('planarHeightBandsComplete', False),
                           ('planarHeightScanComplete', False)):
            data = features(); data['printGeometry'][key] = value
            with self.assertRaises(ValueError): tool.make_plan(data, PROCESS, MACHINE)
        for lo, hi in ((-1, 2), (2, 1), (2, 6), (float('nan'), 3), (True, 3)):
            with self.assertRaises(ValueError):
                tool.make_plan(features([{'bottomZMm': lo, 'topZMm': hi}]), PROCESS, MACHINE)
        duplicate = features()['printGeometry']['planarHeightBands'] * 2
        with self.assertRaises(ValueError): tool.make_plan(features(duplicate), PROCESS, MACHINE)
        for key, value in (('min_layer_height', [0]), ('max_layer_height', [.1]), ('max_layer_height', ['75%'])):
            with self.assertRaises(ValueError): tool.make_plan(features(), PROCESS, {**MACHINE, key: value})
        for key in ('raft_layers', 'spiral_mode'):
            with self.assertRaises(ValueError): tool.make_plan(features(), {**PROCESS, key: '1'}, MACHINE)

    def test_machine_floor_prevents_infeasible_partial_refinement(self):
        plan = tool.make_plan(features(), PROCESS, {**MACHINE, 'min_layer_height': ['.18']})
        self.assertEqual(plan['height_ranges'], [])
        self.assertAlmostEqual(plan['skipped'][0]['required_height_mm'], .17)


class ObservationTest(unittest.TestCase):
    def test_local_samples_cannot_hide_a_missing_terminal_path(self):
        self.candidate['objects']['groups'][0]['planned_layer_coverage']['layers'][-1]['planned_path_area_mm2'] = 0
        result = tool.assess(self.baseline, self.candidate, self.plan)
        self.assertIn('MODEL_PATH_TOP_ERROR_INCREASED', result['failures'])
        self.assertEqual(result['status'], 'REJECTED')

    def setUp(self):
        self.plan = tool.make_plan(features([{'bottomZMm': .6, 'topZMm': .94}]), PROCESS, MACHINE)
        self.plan['model_height_mm'] = 1.2
        self.baseline = report([.24, .2, .2, .2, .2, .2])
        self.candidate = report([.24, .2, .2, .17, .17, .2])

    def test_two_local_samples_not_physical_quality_approval(self):
        result = tool.assess(self.baseline, self.candidate, self.plan)
        self.assertEqual(result['status'], 'LOCAL_SAMPLING_VERIFIED')
        self.assertEqual(result['improved_objectives'], 1)
        self.assertFalse(result['physical_quality_verified'])
        self.assertIsNone(result['selected_production_artifact'])

    def test_global_refinement_and_missing_target_are_rejected(self):
        global_fine = report([.24, .17, .17, .17, .17, .17, .17])
        value = tool.assess(self.baseline, global_fine, self.plan)
        self.assertIn('REFINEMENT_OUTSIDE_TARGET_WINDOWS', value['failures'])
        value = tool.assess(self.baseline, self.baseline, self.plan)
        self.assertIn('LOCAL_SAMPLING_TARGET_NOT_MET', value['failures'])

    def test_first_layer_and_bounds_are_checked(self):
        for heights, code in (([.2] * 5, 'FIRST_LAYER_CHANGED'), ([.24, .2, .08, .08, .2], 'REGIONAL_BOUNDS_VIOLATION')):
            self.assertIn(code, tool.assess(self.baseline, report(heights), self.plan)['failures'])

    def test_baseline_refinement_inside_window_is_not_a_fixed_control(self):
        value = tool.assess(self.candidate, self.candidate, self.plan)
        self.assertIn('FIXED_CONTROL_NOT_FIXED', value['failures'])

    def test_top_error_regression_rejects_even_with_more_local_samples(self):
        plan = {**self.plan, 'model_height_mm': 1.24}
        value = tool.assess(self.baseline, self.candidate, plan)
        self.assertEqual(value['candidate']['objectives'][0]['sample_count'], 2)
        self.assertIn('NOMINAL_TOP_ERROR_INCREASED', value['failures'])
        self.assertIsNone(value['selected_production_artifact'])

    def test_same_section_missing_island_rejects(self):
        layers = self.candidate['objects']['groups'][0]['planned_layer_coverage']['layers']
        layers[0]['completely_unprinted_island_count'] = 1
        value = tool.assess(self.baseline, self.candidate, self.plan)
        self.assertIn('NEW_MISSING_ISLAND_OR_CLOSED_VOID', value['failures'])
        self.assertTrue(value['same_section_observations'])

    def test_sample_planes_without_complete_nonempty_path_evidence_reject(self):
        for field, invalid in (('complete', False), ('planned_path_area_mm2', None),
                               ('planned_path_area_mm2', 0), ('section_area_mm2', float('nan'))):
            candidate = copy.deepcopy(self.candidate)
            candidate['objects']['groups'][0]['planned_layer_coverage']['layers'][3][field] = invalid
            value = tool.assess(self.baseline, candidate, self.plan)
            self.assertIn('TARGET_SECTION_EVIDENCE_UNAVAILABLE', value['failures'])
            self.assertEqual(value['status'], 'REJECTED')

    def test_no_new_target_is_not_called_improvement(self):
        value = tool.assess(self.baseline, self.baseline, {**self.plan, 'objectives': []})
        self.assertEqual(value['status'], 'NO_SAMPLING_GAIN')

    def test_missing_role_evidence_and_new_narrow_moves_reject(self):
        self.candidate['gcode'].pop('quality_paths')
        self.assertIn('NOMINAL_WIDTH_HEIGHT_UNAVAILABLE', tool.assess(self.baseline, self.candidate, self.plan)['failures'])
        self.candidate = report([.24, .2, .2, .17, .17, .2])
        self.candidate['gcode']['quality_paths']['rows'][0]['width_height_observation']['below_height_path_length_mm'] = 1.
        self.assertIn('NEW_NOMINAL_WIDTH_BELOW_HEIGHT', tool.assess(self.baseline, self.candidate, self.plan)['failures'])
        self.candidate['gcode']['quality_paths']['rows'] = []
        self.assertIn('NOMINAL_WIDTH_HEIGHT_UNAVAILABLE', tool.assess(self.baseline, self.candidate, self.plan)['failures'])


class RunnerTest(unittest.TestCase):
    def test_fresh_detection_no_target_skips_both_slices_and_rejects_input_change(self):
        for mutate in (False, True):
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                args = SimpleNamespace(output_dir=root/'trial', timeout=5, optimize_model_detail_paths=False)
                for key in ('binary', 'model', 'machine', 'process', 'filament', 'blender', 'feature_script'):
                    path = root / key
                    path.write_text(key)
                    setattr(args, key, path)
                for name in ('print_feature_geometry.py', 'wall_feature_geometry.py'):
                    (root/name).write_text(name)
                tool.save_json(args.machine, MACHINE); tool.save_json(args.process, PROCESS)
                tool.save_json(args.filament, {'filament_shrink': ['100%'], 'filament_shrinkage_compensation_z': ['100%']})
                commands = []
                def execute(command, directory, timeout):
                    commands.append(command)
                    tool.save_json(directory/'features.json', {'status': 'success', 'features': features([])})
                    if mutate: args.model.write_text('changed')
                    return 0
                with patch.object(tool, 'execute', side_effect=execute): result = tool.trial(args)
                self.assertEqual(len(commands), 1)
                self.assertEqual(result['completed'], not mutate)
                if mutate: self.assertIsNone(result['assessment'])
                else: self.assertEqual(result['assessment']['status'], 'NO_REACHABLE_LOCAL_TARGET')


class PamContractTest(unittest.TestCase):
    def setUp(self):
        self.features = features()
        self.filament = {'filament_shrink': ['100%'], 'filament_shrinkage_compensation_z': ['100%']}
        self.plan = tool.make_plan(self.features, PROCESS, MACHINE)
        self.request = {'features': self.features, 'process': copy.deepcopy(PROCESS), 'filament': self.filament,
                        'minimumLayerHeightMm': .08, 'maximumLayerHeightMm': .32}
        self.response = {'processOverrides': {}, 'filamentOverrides': {},
                         'regionalLayerHeightCandidate': {'version': 'regional-height-v1', 'status': 'READY_FOR_SLICE_VALIDATION',
                             'baseMm': .2, 'firstMm': .24, 'minimumMm': .16, 'maximumMm': .2, 'modelHeightMm': 5.,
                             'objectives': [{'bottomZMm': 2., 'topZMm': 2.34, 'requiredHeightMm': .17, 'status': 'CANDIDATE'}],
                             'ranges': [{'minimumZMm': 2., 'maximumZMm': 2.34, 'layerHeightMm': .17}], 'processOverrides': {}}}

    def verify(self, process=PROCESS):
        return tool.verified_pam_plan(self.request, self.response, self.features, process, MACHINE, self.filament, self.plan)

    def test_actual_pam_ranges_are_consumed_after_binding(self):
        self.assertEqual(self.verify()['height_ranges'], self.plan['height_ranges'])

    def test_stale_features_profiles_and_bounds_reject(self):
        for key, value in (('features', {}), ('process', {}), ('filament', {}), ('minimumLayerHeightMm', .09),
                           ('maximumLayerHeightMm', .4)):
            old = self.request[key]
            self.request[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.verify()
            self.request[key] = old

    def test_tampered_candidate_or_effective_overrides_reject(self):
        candidate = self.response['regionalLayerHeightCandidate']
        for key, value in (('baseMm', .21), ('status', 'APPLIED'), ('ranges', []), ('objectives', []),
                           ('processOverrides', {'flow_ratio': '1.1'})):
            old = candidate[key]
            candidate[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.verify()
            candidate[key] = old
        self.response['processOverrides']['layer_height'] = '.17'
        with self.assertRaises(ValueError): self.verify()

    def test_coupled_shell_and_tree_are_explicit_and_verified(self):
        process = {**PROCESS, 'bottom_shell_layers': '3', 'bottom_shell_thickness': '0',
                   'enable_support': '1', 'support_type': 'tree(auto)', 'support_style': 'organic'}
        self.request['process'] = process
        with self.assertRaises(ValueError): self.verify(process)
        coupled = {'bottom_shell_thickness': '0.6', 'support_style': 'tree_hybrid'}
        self.response['regionalLayerHeightCandidate']['processOverrides'] = coupled
        self.assertEqual(self.verify(process)['candidate_process_overrides'], coupled)


if __name__ == '__main__':
    unittest.main()
