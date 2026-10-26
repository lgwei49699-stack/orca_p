import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_regional_layer_scope import check_scope
from test_relief_height_candidate import report


class RegionalScopeTest(unittest.TestCase):
    def setUp(self):
        self.plan = {'version': 'regional-slope-v1', 'baseMm': .2, 'firstMm': .24, 'minimumMm': .16,
                     'ranges': [{'minimumZMm': .64, 'maximumZMm': 1.44, 'layerHeightMm': .16}]}
        self.a = report([.24] + [.2] * 10)
        self.b = report([.24] + [.2] * 2 + [.16] * 5 + [.2] * 4)

    def test_exact_local_plan_preserves_scope_but_is_not_physical_approval(self):
        result = check_scope(self.a, self.b, self.plan)
        self.assertTrue(result['scope_verified'], result)
        self.assertEqual(7, result['outside_layers_compared'])
        self.assertTrue(result['ranges'][0]['realized'])
        self.assertFalse(result['physical_quality_verified'])
        self.assertIsNone(result['selected_production_artifact'])

    def test_base_height_above_range_does_not_hide_phase_shift(self):
        bad = report([.24] + [.2] * 2 + [.17] * 5 + [.2] * 4)
        result = check_scope(self.a, bad, self.plan)
        self.assertIn('OUTSIDE_LAYER_PHASE_CHANGED', result['failures'])
        self.assertIn('REGIONAL_PLAN_NOT_REALIZED', result['failures'])

    def test_noop_is_not_realized_and_refinement_cannot_spill_outside(self):
        self.assertIn('REGIONAL_PLAN_NOT_REALIZED', check_scope(self.a, self.a, self.plan)['failures'])
        global_fine = report([.24] + [.16] * 12)
        self.assertIn('OUTSIDE_LAYER_PHASE_CHANGED', check_scope(self.a, global_fine, self.plan)['failures'])

    def test_first_layer_and_machine_bounds(self):
        bad = report([.2] + [.2] * 2 + [.16] * 5 + [.2] * 4)
        self.assertIn('FIRST_LAYER_CHANGED', check_scope(self.a, bad, self.plan)['failures'])
        bad = report([.24] + [.2] * 2 + [.08] * 10 + [.2] * 4)
        self.assertIn('LAYER_HEIGHT_OUT_OF_BOUNDS', check_scope(self.a, bad, self.plan)['failures'])

    def test_invalid_plan_is_not_silently_checked(self):
        for key, value in [('minimumZMm', .65), ('maximumZMm', 1.5), ('layerHeightMm', .17), ('layerHeightMm', float('nan'))]:
            plan = copy.deepcopy(self.plan); plan['ranges'][0][key] = value
            with self.assertRaises(ValueError): check_scope(self.a, self.b, plan)

    def test_empty_plan_is_identity_only(self):
        plan = {**self.plan, 'ranges': []}
        self.assertTrue(check_scope(self.a, self.a, plan)['scope_verified'])
        self.assertFalse(check_scope(self.a, self.b, plan)['scope_verified'])

    def test_missing_width_evidence_remains_unknown_not_zero_or_scope_failure(self):
        self.b['gcode'].pop('quality_paths')
        result = check_scope(self.a, self.b, self.plan)
        self.assertTrue(result['scope_verified'])
        self.assertFalse(result['nominal_width_height_available'])
        self.assertIsNone(result['new_width_below_height_roles'])
