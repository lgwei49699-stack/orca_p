import argparse
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import evaluate_layer_height_candidate as tool


PLAN = {"base": .2, "minimum": .16, "maximum": .2, "quality": .5}


def layer(index, z, height=.2, **changes):
    value = {"layer_index": index, "slice_z_mm": z, "height_mm": height, "complete": True,
             "print_z_mm": z + height / 2, "planned_path_area_mm2": 10.,
             "invalid_path_count": 0, "excluded_path_count": 0,
             "section_area_mm2": 10., "hole_area_mm2": 2., "section_island_count": 1,
             "closed_void_region_count": 1, "completely_unprinted_island_area_mm2": 0.,
             "completely_unprinted_island_count": 0, "hole_intrusion_area_mm2": 0.,
             "closed_void_fully_covered_count": 0}
    value.update(changes)
    return value


def path(role="outer_wall", length=0.):
    return {"extruder_id": 0, "role": role, "complete": True, "width_height_observation": {
        "scope": "same_move_nominal_width_height_non_bridge", "complete": True,
        "applicable": role not in ("bridge", "internal_bridge", "overhang_wall"),
        "below_height_path_length_mm": length}}


def report(layers=None, paths=None):
    layers = layers if layers is not None else [layer(0, .12, .24), layer(1, .34), layer(2, .54)]
    return {"objects": {"source_object_count": 1, "instance_count": 1, "groups": [{
        "model_layer_count": len(layers), "planned_layer_coverage": {"scope": tool.COMMON_SCOPE, "layers": layers}}]},
        "gcode": {"quality_paths": {"scope": "final_gcode_positive_extrusion_moves",
                                   "rows": paths if paths is not None else [path()]},
                  "normal_estimate": {"total_time_s": 12}}}


class AssessmentTest(unittest.TestCase):
    def test_final_gcode_extent_is_checked_independently_of_planned_paths(self):
        extents = {'baseline': {'complete': True, 'last_path_print_z_mm': .64},
                   'candidate': {'complete': True, 'last_path_print_z_mm': .44}}
        value = tool.assess(report(), report(), PLAN, source_height_mm=.64, final_extents=extents)
        self.assertIn('FINAL_GCODE_MODEL_TOP_ERROR_INCREASED', [r['code'] for r in value['reasons']])
        self.assertIn('FINAL_GCODE_AND_PLANNED_EXTENT_DIFFER', value['cautions'])
        self.assertEqual(value['selected_variant'], 'fixed_b')

    def test_nominal_top_alignment_cannot_hide_a_lower_last_path(self):
        a = report([layer(0, .12, .24), layer(1, .34), layer(2, .54),
                    layer(3, .74, planned_path_area_mm2=0)])
        b = report([layer(0, .12, .24), layer(1, .32, .16), layer(2, .49, .18),
                    layer(3, .69, .2, planned_path_area_mm2=0)])
        value = tool.assess(a, b, PLAN, source_height_mm=.79)
        self.assertEqual(value['status'], 'FALLBACK_FIXED_B')
        self.assertIn('MODEL_PATH_TOP_ERROR_INCREASED', [r['code'] for r in value['reasons']])
        self.assertEqual(value['terminal_paths']['candidate']['terminal_source_sections_without_paths'], [3])
        self.assertAlmostEqual(value['terminal_paths']['candidate']['nominal_plan_top_mm'], .79)

    def test_top_error_checks_both_undershoot_and_overshoot_without_rewarding_empty_layers(self):
        a = report([layer(0, .12, .24), layer(1, .34)])
        b = report([layer(0, .12, .24), layer(1, .50)])
        value = tool.assess(a, b, PLAN, source_height_mm=.44)
        self.assertTrue(value['terminal_paths']['path_top_error_increased'])
        b['objects']['groups'][0]['planned_layer_coverage']['layers'][1]['planned_path_area_mm2'] = 0
        value = tool.assess(a, b, PLAN, source_height_mm=.44)
        self.assertTrue(value['terminal_paths']['path_top_error_increased'])

    def test_unknown_extent_never_becomes_zero_or_a_success(self):
        for key, invalid in (('complete', False), ('print_z_mm', None), ('planned_path_area_mm2', None),
                             ('invalid_path_count', 1), ('excluded_path_count', 1)):
            b = report()
            b['objects']['groups'][0]['planned_layer_coverage']['layers'][-1][key] = invalid
            value = tool.assess(report(), b, PLAN, source_height_mm=.64)
            self.assertIsNone(value['terminal_paths']['path_top_error_increased'])
            self.assertIn('MODEL_PATH_EXTENT_UNAVAILABLE', value['cautions'])
            self.assertEqual(value['status'], 'REVIEW_REQUIRED')
        self.assertFalse(tool.terminal_paths(report(), None)['complete'])

    def test_clean_report_is_not_a_quality_pass(self):
        value = tool.assess(report(), report(), PLAN)
        self.assertEqual(value["status"], "REVIEW_REQUIRED")
        self.assertIsNone(value["selected_variant"])
        self.assertFalse(value["physical_quality_verified"])
        self.assertIn("LOCAL_SURFACE_FIDELITY_AND_PHYSICAL_QUALITY_NOT_VERIFIED", value["cautions"])

    def test_exact_same_commands_keep_fixed_b(self):
        value = tool.assess(report(), report(), PLAN, identical_commands=True)
        self.assertEqual(value["status"], "NO_EXECUTABLE_CHANGE")
        self.assertEqual(value["selected_variant"], "fixed_b")

    def test_first_layer_is_not_constrained_by_ordinary_bounds(self):
        b = report([layer(0, .12, .24), layer(1, .32, .16), layer(2, .48, .16)])
        value = tool.assess(report(), b, PLAN)
        self.assertEqual(value["model_layer_plan"]["ordinary_out_of_bounds_count"], 0)
        self.assertEqual(value["model_layer_plan"]["ordinary_refined_count"], 2)
        self.assertEqual(value["section_comparison"]["comparable_sections"], 1)
        self.assertIn("PARTIAL_SOURCE_SECTION_COMPARISON", value["cautions"])

    def test_changed_first_or_out_of_bound_layer_prevents_candidate_selection(self):
        for heights, code in (((.2, .2), "FIRST_MODEL_LAYER_CHANGED"), ((.24, .08), "LAYER_PLAN_VIOLATION")):
            with self.subTest(code=code):
                b = report([layer(0, .12, heights[0]), layer(1, .32, heights[1])])
                value = tool.assess(report(), b, PLAN)
                self.assertEqual(value["status"], "FALLBACK_FIXED_B")
                self.assertIn(code, [reason["code"] for reason in value["reasons"]])

    def test_compare_by_source_z_not_layer_number(self):
        a = report([layer(0, .12, .24), layer(1, .34), layer(2, .54)])
        b = report([layer(0, .12, .24), layer(1, .3, .16), layer(2, .46, .16),
                    layer(3, .54, .16, hole_intrusion_area_mm2=.2)])
        value = tool.assess(a, b, PLAN)
        obs = value["common_sections"][-1]
        self.assertEqual((obs["baseline_layer_index"], obs["candidate_layer_index"]), (2, 3))
        self.assertEqual(value["status"], "REVIEW_REQUIRED")
        self.assertIn("SOURCE_SECTION_AREA_INCREASE_REQUIRES_LOCAL_REVIEW", value["cautions"])

    def test_different_source_section_or_incomplete_is_not_comparable(self):
        for changes in ({"slice_z_mm": .35}, {"section_area_mm2": 11.}, {"hole_area_mm2": 3.},
                        {"complete": False}, {"hole_intrusion_area_mm2": None}):
            with self.subTest(changes=changes):
                b = report([layer(0, .12, .24), layer(1, .34, hole_intrusion_area_mm2=.2)])
                b["objects"]["groups"][0]["planned_layer_coverage"]["layers"][1].update(changes)
                value = tool.assess(report(), b, PLAN)
                self.assertEqual(value["section_comparison"]["comparable_sections"], 1)
                self.assertEqual(value["status"], "REVIEW_REQUIRED")

    def test_lost_complete_section_features_cause_fallback_not_area_score(self):
        for key, increase in (("completely_unprinted_island_count", 1), ("closed_void_fully_covered_count", 1)):
            with self.subTest(key=key):
                b = report([layer(0, .12, .24), layer(1, .34, **{key: increase})])
                result = tool.assess(report(), b, PLAN)
                self.assertEqual(result["selected_variant"], "fixed_b")
                self.assertFalse(result["physical_quality_verified"])
        for key in ("completely_unprinted_island_area_mm2", "hole_intrusion_area_mm2"):
            result = tool.assess(report(), report([layer(0, .12, .24, **{key: .01})]), PLAN)
            self.assertEqual(result["status"], "REVIEW_REQUIRED")
            self.assertIn("SOURCE_SECTION_AREA_INCREASE_REQUIRES_LOCAL_REVIEW", result["cautions"])

    def test_area_improvement_cannot_certify_curve_quality(self):
        a = report([layer(0, .12, .24, hole_intrusion_area_mm2=1.)])
        b = report([layer(0, .12, .24)])
        result = tool.assess(a, b, PLAN)
        changes = result["common_sections"][0]["changes"]
        self.assertEqual(next(row for row in changes if row["metric"] == "hole_intrusion_area_mm2")["direction"], "decrease")
        self.assertEqual(result["status"], "REVIEW_REQUIRED")

    def test_only_ulp_scale_area_noise_is_ignored(self):
        a = report([layer(0, .12, .24, hole_intrusion_area_mm2=.5)])
        b = report([layer(0, .12, .24, hole_intrusion_area_mm2=.5 + 1e-15)])
        self.assertEqual(tool.assess(a, b, PLAN)["reasons"], [])
        b["objects"]["groups"][0]["planned_layer_coverage"]["layers"][0]["hole_intrusion_area_mm2"] = .500001
        self.assertIn("SOURCE_SECTION_AREA_INCREASE_REQUIRES_LOCAL_REVIEW", tool.assess(a, b, PLAN)["cautions"])

    def test_new_narrow_path_is_a_warning_with_conservative_fallback(self):
        result = tool.assess(report(), report(paths=[path(length=2.)]), PLAN)
        self.assertEqual(result["reasons"][0]["code"], "NEW_NOMINAL_WIDTH_BELOW_HEIGHT")
        self.assertEqual(result["selected_variant"], "fixed_b")

    def test_do_not_score_narrow_length_totals_from_different_layer_plans(self):
        result = tool.assess(report(paths=[path(length=1.)]), report(paths=[path(length=5.)]), PLAN)
        self.assertEqual(result["status"], "REVIEW_REQUIRED")
        result = tool.assess(report(), report(paths=[path(), path("bridge", None)]), PLAN)
        self.assertEqual(result["reasons"], [])

    def test_missing_narrow_evidence_is_not_zero(self):
        b = report()
        b["gcode"].pop("quality_paths")
        result = tool.assess(report(), b, PLAN)
        self.assertIn("NOMINAL_WIDTH_HEIGHT_EVIDENCE_UNAVAILABLE", result["cautions"])
        self.assertIsNone(result["selected_variant"])

    def test_reject_ambiguous_model_or_nonfixed_control(self):
        for invalid in (report([layer(0, .12, .24), layer(1, .32, .16)]),
                        report([layer(0, .12), layer(0, .34)]), report([])):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                tool.assess(invalid, report(), PLAN)
        a = report()
        a["objects"]["instance_count"] = 2
        with self.assertRaises(ValueError):
            tool.assess(a, report(), PLAN)


class GcodeExtentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'test.gcode'

    def observe(self, source):
        self.path.write_text(source)
        return tool.gcode_model_extent(self.path)

    def test_excludes_support_travel_and_pure_e_unretract(self):
        value = self.observe('G90\nM83\n;TYPE:Outer wall\nG1 Z.2\nG1 X2 E1\n'
                             'G1 Z10\nG1 E1\n;TYPE:Support\nG1 X3 E1\n')
        self.assertTrue(value['complete'])
        self.assertEqual(value['last_path_print_z_mm'], .2)
        self.assertEqual(value['model_extrusion_z_count'], 1)

    def test_relative_absolute_e_reset_and_full_circle_arc(self):
        value = self.observe('G90\nM82\n;TYPE:Outer wall\nG1 Z.2\nG1 X2 E1\nG92 E0\n'
                             'G91\nG1 Z.2\nG2 I1 J0 E.3\nM83\nG1 Z.2\nG1 X2 E.1\n')
        self.assertTrue(value['complete'])
        self.assertAlmostEqual(value['last_path_print_z_mm'], .6)
        self.assertEqual(value['model_extrusion_move_count'], 3)

    def test_unsupported_modes_and_unknown_roles_remain_unknown(self):
        for prefix in ('G20', 'G54', 'G92 Z10', 'T1', 'T01', 'M200 D1.75', 'G10', 'M221 S0', 'M221 S-1'):
            self.assertFalse(self.observe(prefix + '\nM83\n;TYPE:Outer wall\nG1 X2 E1\n')['complete'])
        self.assertFalse(self.observe('M83\n;TYPE:Alien\nG1 X2 E1\n')['complete'])
        self.assertFalse(self.observe('M83\n;TYPE:Outer wall\nG1 E1\n')['complete'])


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.args = argparse.Namespace(output_dir=self.root / "trial", minimum=.16, maximum=.2, quality=.5,
                                       timeout=30, optimize_model_detail_paths=False)
        for key in ("binary", "model", "machine", "process", "filament"):
            path = self.root / key
            path.write_text('{"layer_height":"0.2"}' if key == "process" else key)
            setattr(self.args, key, path)

    def artifacts(self, directory):
        return report(), {"configuration": ["fixed config"], "commands": ["G1 X1"]}, {"gcode": str(directory / "output/plate_1.gcode")}

    def test_pair_preserves_all_other_options_and_writes_manifest(self):
        commands = []
        self.args.optimize_model_detail_paths = True
        with patch.object(tool, "execute", side_effect=lambda command, *_: commands.append(command) or 0), \
                patch.object(tool, "load_artifacts", side_effect=self.artifacts):
            result = tool.trial(self.args)
        self.assertTrue(result["completed"])
        self.assertEqual(result["assessment"]["selected_variant"], "fixed_b")
        self.assertNotIn("--variable-layer-height-mode", commands[0])
        self.assertIn("--variable-layer-height-mode", commands[1])
        for command in commands:
            self.assertEqual(command[-1], str(self.args.model))
            self.assertIn("--optimize-model-detail-paths", command)
            self.assertNotIn("--auto-repair-model", command)
        self.assertTrue((self.args.output_dir / "trial.json").is_file())

    def test_configuration_mismatch_cannot_produce_a_selection(self):
        def artifacts(directory):
            result = self.artifacts(directory)
            result[1]["configuration"] = [directory.name]
            return result
        with patch.object(tool, "execute", return_value=0), patch.object(tool, "load_artifacts", side_effect=artifacts):
            result = tool.trial(self.args)
        self.assertFalse(result["completed"])
        self.assertIsNone(result["selected_artifact"])
        self.assertIn("configs differ", result["error"])

    def test_input_mutation_and_failed_process_never_choose_stale_output(self):
        def execute(*args):
            self.args.machine.write_text("changed")
            return 0
        with patch.object(tool, "execute", side_effect=execute), patch.object(tool, "load_artifacts", side_effect=self.artifacts):
            result = tool.trial(self.args)
        self.assertFalse(result["completed"])
        self.assertIsNone(result["selected_artifact"])
        self.assertIn("identity changed", result["error"])
        self.args.output_dir = self.root / "failed-trial"
        with patch.object(tool, "execute", side_effect=[0, 2]), patch.object(tool, "load_artifacts", side_effect=self.artifacts):
            result = tool.trial(self.args)
        self.assertFalse(result["completed"])
        self.assertIsNone(result["selected_artifact"])

    def test_invalid_or_unresolved_plan_does_not_execute(self):
        for low, high, quality in ((.08, .2, .5), (.16, .24, .5), (.16, .2, 2)):
            self.args.minimum, self.args.maximum, self.args.quality = low, high, quality
            with patch.object(tool, "execute") as execute, self.assertRaises(ValueError):
                tool.trial(self.args)
            execute.assert_not_called()
        self.assertFalse(self.args.output_dir.exists())

    def test_surface_proxy_requires_known_unscaled_source(self):
        valid = ["; filament_shrink = 100%", "; filament_shrinkage_compensation_z = 100%"]
        valid += ["; wipe_tower_x = 15.000", "; wipe_tower_x = 15"]  # Native export duplicates unrelated keys.
        with patch.object(tool, "compare_surface", return_value={"complete": True}) as compare:
            self.assertTrue(tool.surface_evidence(self.args.model, report(), report(), valid)["complete"])
            compare.assert_called_once()
            for lines in ([], valid[:1], valid + valid, [valid[0], "; filament_shrinkage_compensation_z = 99%"]):
                self.assertFalse(tool.surface_evidence(self.args.model, report(), report(), lines)["complete"])
            compare.assert_called_once()


if __name__ == "__main__":
    unittest.main()
