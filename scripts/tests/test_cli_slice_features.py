import importlib.util
import copy
from collections import Counter
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("feature_fixture_tools", SCRIPTS / "test_cli_slice_features.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)
from test_cli_slice_analysis import (validate_gcode_observations, validate_plate_bed_footprint, validate_perimeter_loops,
                                     validate_solid_fill_target_depth, validate_planned_material_depth,
                                     validate_nominal_target_path_connectivity, validate_report,
                                     validate_layer_time_threshold_observation, validate_estimated_motion,
                                     validate_estimated_peak_flow, validate_estimated_flow_cap_distance, validate_metric_values)


class LayerTimeThresholdObservationTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "final_gcode_layer_sequences_per_material", "complete": True, "applicable": True,
                "slow_down_enabled": True, "configured_threshold_s": 8,
                "active_layer_sequence_count": 3, "below_threshold_layer_sequence_count": 1,
                "below_threshold_layer_ratio": 1 / 3}

    def test_material_counts_are_distinct_sequences_not_moves_or_all_plate_layers(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        validate_layer_time_threshold_observation(value, 12)
        self.assertEqual(value, before)
        for below in (0, 3):
            validate_layer_time_threshold_observation({**value, "below_threshold_layer_sequence_count": below,
                                                      "below_threshold_layer_ratio": below / 3}, 3)

    def test_disabled_zero_threshold_and_unused_material_are_not_applicable(self):
        for change in ({"slow_down_enabled": False}, {"configured_threshold_s": 0},
                       {"active_layer_sequence_count": 0}):
            value = {**self.fixture(), **change, "applicable": False,
                     "below_threshold_layer_sequence_count": None, "below_threshold_layer_ratio": None}
            with self.subTest(change=change):
                validate_layer_time_threshold_observation(value, 12)
                for field in ("below_threshold_layer_sequence_count", "below_threshold_layer_ratio"):
                    with self.assertRaises(AssertionError):
                        validate_layer_time_threshold_observation({**value, field: 0}, 12)

    def test_unknown_preserves_readable_settings_but_not_partial_counts(self):
        value = {**self.fixture(), "complete": False, "applicable": None,
                 "active_layer_sequence_count": None, "below_threshold_layer_sequence_count": None,
                 "below_threshold_layer_ratio": None}
        for change in ({}, {"slow_down_enabled": None}, {"configured_threshold_s": None},
                       {"slow_down_enabled": False, "configured_threshold_s": 0}):
            validate_layer_time_threshold_observation({**value, **change}, 12)
        for field, partial in (("applicable", False), ("active_layer_sequence_count", 0),
                               ("below_threshold_layer_sequence_count", 0), ("below_threshold_layer_ratio", 0)):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                validate_layer_time_threshold_observation({**value, field: partial}, 12)

    def test_missing_old_report_fields_are_not_accepted_as_zero(self):
        for field in self.fixture():
            value = self.fixture()
            del value[field]
            with self.subTest(field=field), self.assertRaises(KeyError):
                validate_layer_time_threshold_observation(value, 12)

    def test_invalid_config_scope_types_counts_and_ratio_are_rejected(self):
        changes = ({"scope": "physical_cooling"}, {"complete": 1}, {"applicable": None}, {"applicable": 1},
                   {"applicable": False}, {"slow_down_enabled": 1}, {"slow_down_enabled": None},
                   {"configured_threshold_s": None}, {"configured_threshold_s": -1},
                   {"configured_threshold_s": True}, {"configured_threshold_s": math.inf},
                   {"configured_threshold_s": math.nan}, {"active_layer_sequence_count": 13},
                   {"active_layer_sequence_count": -1}, {"active_layer_sequence_count": True},
                   {"active_layer_sequence_count": 3.0}, {"below_threshold_layer_sequence_count": 4},
                   {"below_threshold_layer_sequence_count": True}, {"below_threshold_layer_sequence_count": -1},
                   {"below_threshold_layer_ratio": None}, {"below_threshold_layer_ratio": 0.25},
                   {"below_threshold_layer_ratio": math.nan}, {"below_threshold_layer_ratio": True})
        for change in changes:
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_layer_time_threshold_observation({**self.fixture(), **change}, 12)

    def test_zero_sequences_can_only_be_unused_and_inapplicable(self):
        value = {**self.fixture(), "applicable": False, "active_layer_sequence_count": 0,
                 "below_threshold_layer_sequence_count": None, "below_threshold_layer_ratio": None}
        validate_layer_time_threshold_observation(value, 0)
        with self.assertRaises(AssertionError):
            validate_layer_time_threshold_observation(self.fixture(), 0)

    def test_materials_have_independent_thresholds_and_active_denominators(self):
        values = [self.fixture(), {**self.fixture(), "configured_threshold_s": 20,
                                  "active_layer_sequence_count": 2, "below_threshold_layer_sequence_count": 2,
                                  "below_threshold_layer_ratio": 1}]
        for value in values:
            validate_layer_time_threshold_observation(value, 4)
        # Materials can share one sequence; their counts need not sum to four.
        self.assertGreater(sum(value["active_layer_sequence_count"] for value in values), 4)


class SolidFillTargetDepthTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "per_instance_continuous_solid_fill_surface_slabs", "complete": True, "z_resolution_mm": 1e-6,
                "top": {"target_area_sum_mm2": 8, "continuous_depth_mm": {"min": 0.2, "max": 0.6}},
                "bottom": {"target_area_sum_mm2": 4, "continuous_depth_mm": {"min": 0.6, "max": 0.6}}}

    def test_top_and_bottom_keep_independent_positive_nominal_ranges(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        validate_solid_fill_target_depth(value, 3)
        self.assertEqual(value, before)
        value["top"]["continuous_depth_mm"] = {"min": 1e-6, "max": 1e-6}
        value["top"]["target_area_sum_mm2"] = 1e-12
        validate_solid_fill_target_depth(value, 3)

    def test_no_target_on_one_or_both_sides_is_not_unknown(self):
        value = self.fixture()
        for side in ("top", "bottom"):
            value[side] = {"target_area_sum_mm2": 0, "continuous_depth_mm": {"min": None, "max": None}}
            validate_solid_fill_target_depth(value, 3)

    def test_incomplete_or_no_retained_layers_has_no_partial_values(self):
        value = self.fixture()
        value["complete"] = False
        for side in ("top", "bottom"):
            value[side] = {"target_area_sum_mm2": None, "continuous_depth_mm": {"min": None, "max": None}}
        for layers in (0, 3):
            validate_solid_fill_target_depth(value, layers)
        for side in ("top", "bottom"):
            for field in ("area", "min", "max"):
                bad = copy.deepcopy(value)
                if field == "area":
                    bad[side]["target_area_sum_mm2"] = 0
                else:
                    bad[side]["continuous_depth_mm"][field] = 0.2
                with self.subTest(side=side, field=field), self.assertRaises(AssertionError):
                    validate_solid_fill_target_depth(bad, 3)
        with self.assertRaisesRegex(AssertionError, "No retained model layers"):
            validate_solid_fill_target_depth(self.fixture(), 0)

    def test_scope_completeness_and_resolution_are_not_guessed(self):
        for change in ({"scope": "physical_material_thickness"}, {"complete": 1}, {"z_resolution_mm": 0},
                       {"z_resolution_mm": -1}, {"z_resolution_mm": True}, {"z_resolution_mm": "0.000001"},
                       {"z_resolution_mm": math.inf}, {"z_resolution_mm": math.nan}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_solid_fill_target_depth({**self.fixture(), **change}, 3)

    def test_area_and_range_consistency_rejects_boolean_nonfinite_partial_and_inverted_values(self):
        changes = ({"target_area_sum_mm2": -1}, {"target_area_sum_mm2": None}, {"target_area_sum_mm2": True},
                   {"target_area_sum_mm2": math.inf}, {"target_area_sum_mm2": 0},
                   {"continuous_depth_mm": {"min": None, "max": None}},
                   {"continuous_depth_mm": {"min": 0, "max": 0.2}},
                   {"continuous_depth_mm": {"min": 0.4, "max": 0.2}},
                   {"continuous_depth_mm": {"min": True, "max": 1}},
                   {"continuous_depth_mm": {"min": 0.2, "max": math.nan}},
                   {"continuous_depth_mm": {"min": 0.2, "max": None}})
        for side in ("top", "bottom"):
            for change in changes:
                value = self.fixture()
                value[side].update(change)
                with self.subTest(side=side, change=change), self.assertRaises(AssertionError):
                    validate_solid_fill_target_depth(value, 3)

    def test_missing_contract_fields_are_not_old_data_success(self):
        for path in (("scope",), ("complete",), ("z_resolution_mm",), ("top",), ("bottom",),
                     ("top", "target_area_sum_mm2"), ("bottom", "continuous_depth_mm"),
                     ("top", "continuous_depth_mm", "min"), ("bottom", "continuous_depth_mm", "max")):
            value = self.fixture()
            target = value
            for key in path[:-1]:
                target = target[key]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_solid_fill_target_depth(value, 3)

    def test_full_report_entry_checks_new_group_contract_before_accepting_other_metrics(self):
        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "planned_perimeter_loops": {"scope": "per_instance_closed_model_perimeter_centerlines_before_gcode",
                                             "complete": True, "loop_count": 0,
                                             "loop_length_mm": {"min": None, "max": None},
                                             "turn_angle_degrees": {"min": None, "max": None}},
                 "solid_fill_target_depth": self.fixture()}
        report = {"planned_plate_bed_footprint": {"scope": "planned_path_nominal_bottom_on_bed_before_gcode",
                  "complete": True, "model_area_mm2": 1, "support_and_raft_area_mm2": 0,
                  "brim_area_mm2": 0, "combined_area_mm2": 1},
                  "objects": {"slice_object_group_count": 1, "source_object_count": 1, "instance_count": 1,
                              "groups": [group]}}
        group["solid_fill_target_depth"]["complete"] = False
        with self.assertRaisesRegex(AssertionError, "Incomplete solid-fill depth"):
            validate_metric_values(report, {}, {})
        del group["solid_fill_target_depth"]
        with self.assertRaisesRegex(KeyError, "solid_fill_target_depth"):
            validate_metric_values(report, {}, {})


class PlannedMaterialDepthTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "per_instance_fixed_xy_model_path_slabs_before_gcode", "complete": True, "z_resolution_mm": 1e-6,
                "top": {"target_area_sum_mm2": 8, "uncovered_start_area_sum_mm2": 2,
                        "continuous_depth_mm": {"min": 0, "max": 0.6}},
                "bottom": {"target_area_sum_mm2": 4, "uncovered_start_area_sum_mm2": 0,
                           "continuous_depth_mm": {"min": 0.2, "max": 0.6}}}

    def test_independent_ranges_keep_known_zero_and_positive_values(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        validate_planned_material_depth(value, 3)
        self.assertEqual(value, before)
        value["z_resolution_mm"] = 1e-5
        value["bottom"]["continuous_depth_mm"] = {"min": 1e-5, "max": 1e-5}
        validate_planned_material_depth(value, 3)

    def test_any_positive_uncovered_area_contributes_zero_minimum(self):
        for uncovered in (1e-12, 2):
            value = self.fixture()
            value["top"]["uncovered_start_area_sum_mm2"] = uncovered
            validate_planned_material_depth(value, 3)
            value["top"]["continuous_depth_mm"]["min"] = 1e-6
            with self.subTest(uncovered=uncovered), self.assertRaisesRegex(AssertionError, "known zero minimum"):
                validate_planned_material_depth(value, 3)

    def test_entirely_uncovered_targets_have_zero_not_unknown_depth(self):
        value = self.fixture()
        for side in ("top", "bottom"):
            value[side]["uncovered_start_area_sum_mm2"] = value[side]["target_area_sum_mm2"]
            value[side]["continuous_depth_mm"] = {"min": 0, "max": 0}
        validate_planned_material_depth(value, 3)
        for side in ("top", "bottom"):
            invalid = copy.deepcopy(value)
            invalid[side]["continuous_depth_mm"]["max"] = 0.2
            with self.subTest(side=side), self.assertRaisesRegex(AssertionError, "Entirely uncovered"):
                validate_planned_material_depth(invalid, 3)

    def test_absent_targets_have_zero_uncovered_area_and_no_range(self):
        value = self.fixture()
        for side in ("top", "bottom"):
            value[side] = {"target_area_sum_mm2": 0, "uncovered_start_area_sum_mm2": 0,
                           "continuous_depth_mm": {"min": None, "max": None}}
            validate_planned_material_depth(value, 3)
            invalid = copy.deepcopy(value)
            invalid[side]["continuous_depth_mm"] = {"min": 0, "max": 0}
            with self.subTest(side=side), self.assertRaisesRegex(AssertionError, "No planned-material targets"):
                validate_planned_material_depth(invalid, 3)

    def test_unknown_and_no_layers_have_no_partial_values(self):
        value = self.fixture()
        value["complete"] = False
        for side in ("top", "bottom"):
            value[side] = {"target_area_sum_mm2": None, "uncovered_start_area_sum_mm2": None,
                           "continuous_depth_mm": {"min": None, "max": None}}
        for layers in (0, 3):
            validate_planned_material_depth(value, layers)
        for side in ("top", "bottom"):
            for field in ("target_area_sum_mm2", "uncovered_start_area_sum_mm2", "min", "max"):
                invalid = copy.deepcopy(value)
                target = invalid[side] if field.endswith("mm2") else invalid[side]["continuous_depth_mm"]
                target[field] = 0
                with self.subTest(side=side, field=field), self.assertRaisesRegex(AssertionError, "Incomplete planned-material"):
                    validate_planned_material_depth(invalid, 3)
        with self.assertRaisesRegex(AssertionError, "No retained model layers"):
            validate_planned_material_depth(self.fixture(), 0)

    def test_scope_types_resolution_areas_and_ranges_are_validated(self):
        for change in ({"scope": "physical_fused_material"}, {"complete": 1}, {"z_resolution_mm": 0},
                       {"z_resolution_mm": -1}, {"z_resolution_mm": True}, {"z_resolution_mm": "0.000001"},
                       {"z_resolution_mm": math.inf}, {"z_resolution_mm": math.nan}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_planned_material_depth({**self.fixture(), **change}, 3)
        changes = ({"target_area_sum_mm2": -1}, {"target_area_sum_mm2": None}, {"target_area_sum_mm2": True},
                   {"target_area_sum_mm2": math.inf}, {"uncovered_start_area_sum_mm2": -1},
                   {"uncovered_start_area_sum_mm2": 9}, {"uncovered_start_area_sum_mm2": True},
                   {"uncovered_start_area_sum_mm2": math.nan}, {"uncovered_start_area_sum_mm2": None},
                   {"continuous_depth_mm": {"min": None, "max": None}},
                   {"continuous_depth_mm": {"min": -1, "max": 0.6}},
                   {"continuous_depth_mm": {"min": 1, "max": 0.6}},
                   {"continuous_depth_mm": {"min": True, "max": 1}},
                   {"continuous_depth_mm": {"min": 0, "max": math.inf}},
                   {"continuous_depth_mm": {"min": 0, "max": None}})
        for side in ("top", "bottom"):
            for change in changes:
                value = self.fixture()
                value[side].update(change)
                with self.subTest(side=side, change=change), self.assertRaises(AssertionError):
                    validate_planned_material_depth(value, 3)

    def test_missing_fields_do_not_become_known_zero(self):
        for path in (("scope",), ("complete",), ("z_resolution_mm",), ("top",), ("bottom",),
                     ("top", "target_area_sum_mm2"), ("bottom", "uncovered_start_area_sum_mm2"),
                     ("top", "continuous_depth_mm"), ("top", "continuous_depth_mm", "min"),
                     ("bottom", "continuous_depth_mm", "max")):
            value = self.fixture()
            target = value
            for key in path[:-1]:
                target = target[key]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_planned_material_depth(value, 3)

    def test_full_report_entry_requires_new_group_without_changing_old_depth(self):
        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "planned_perimeter_loops": {"scope": "per_instance_closed_model_perimeter_centerlines_before_gcode",
                                             "complete": True, "loop_count": 0,
                                             "loop_length_mm": {"min": None, "max": None},
                                             "turn_angle_degrees": {"min": None, "max": None}},
                 "solid_fill_target_depth": SolidFillTargetDepthTest().fixture(),
                 "planned_material_depth": self.fixture()}
        report = {"planned_plate_bed_footprint": {"scope": "planned_path_nominal_bottom_on_bed_before_gcode",
                  "complete": True, "model_area_mm2": 1, "support_and_raft_area_mm2": 0,
                  "brim_area_mm2": 0, "combined_area_mm2": 1},
                  "objects": {"slice_object_group_count": 1, "source_object_count": 1, "instance_count": 1,
                              "groups": [group]}}
        group["planned_material_depth"]["complete"] = False
        with self.assertRaisesRegex(AssertionError, "Incomplete planned-material depth"):
            validate_metric_values(report, {}, {})
        del group["planned_material_depth"]
        with self.assertRaisesRegex(KeyError, "planned_material_depth"):
            validate_metric_values(report, {}, {})


class NominalTargetPathConnectivityTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "per_instance_compensated_region_section_and_model_path_slabs",
                "coordinate_frame": "print_object_local_xy_and_print_z", "z_resolution_mm": 1e-6, "complete": True,
                "target_components": [
                    {"component_index": 0, "bounds_mm": {"min": [-4, -2, 0], "max": [-2, 2, 1]},
                     "target_volume_mm3": 8, "uncovered_volume_mm3": 2, "clipped_path_component_count": 2,
                     "missing_slab_regions": [{"bounds_mm": {"min": [-4, -2, 0.25], "max": [-2, 2, 0.5]},
                                               "volume_mm3": 2}]},
                    {"component_index": 1, "bounds_mm": {"min": [0, -2, 0], "max": [2, 2, 1]},
                     "target_volume_mm3": 8, "uncovered_volume_mm3": 0, "clipped_path_component_count": 1,
                     "missing_slab_regions": []}],
                "cross_target_joins": [{"path_component_index": 5, "target_component_indices": [1, 0]}]}

    def test_valid_local_coordinates_and_sparse_path_indices_are_not_reordered(self):
        value = self.fixture()
        value["cross_target_joins"].append({"path_component_index": 9, "target_component_indices": [0, 1]})
        before = copy.deepcopy(value)
        validate_nominal_target_path_connectivity(value, 5)
        self.assertEqual(value, before)

    def test_empty_and_unknown_do_not_share_an_array_representation(self):
        for complete, components, joins, layers in ((True, [], [], 1), (False, None, None, 1), (False, None, None, 0)):
            value = {**self.fixture(), "complete": complete, "target_components": components, "cross_target_joins": joins}
            validate_nominal_target_path_connectivity(value, layers)
        with self.assertRaisesRegex(AssertionError, "No retained model layers"):
            validate_nominal_target_path_connectivity({**self.fixture(), "target_components": [], "cross_target_joins": []}, 0)

    def test_unknown_rejects_partial_or_known_empty_arrays(self):
        value = {**self.fixture(), "complete": False, "target_components": None, "cross_target_joins": None}
        for key in ("target_components", "cross_target_joins"):
            for partial in ([], self.fixture()[key]):
                with self.subTest(key=key, partial=partial), self.assertRaisesRegex(AssertionError, "Incomplete nominal"):
                    validate_nominal_target_path_connectivity({**value, key: partial}, 2)

    def test_scope_frame_types_and_resolution_are_not_inferred(self):
        for change in ({"scope": "physical_solid_connectivity"}, {"coordinate_frame": "world_xy"}, {"complete": 1},
                       {"z_resolution_mm": 0}, {"z_resolution_mm": -1}, {"z_resolution_mm": True},
                       {"z_resolution_mm": 1e-5}, {"z_resolution_mm": 1e-7},
                       {"z_resolution_mm": math.nextafter(1e-6, math.inf)},
                       {"z_resolution_mm": None}, {"z_resolution_mm": "0.000001"},
                       {"z_resolution_mm": math.inf}, {"z_resolution_mm": math.nan},
                       {"target_components": None}, {"target_components": {}}, {"cross_target_joins": None},
                       {"cross_target_joins": {}}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity({**self.fixture(), **change}, 2)

    def test_bounds_require_three_finite_signed_coordinates_and_positive_extents(self):
        invalid_bounds = ({"min": [0, 0], "max": [1, 1]}, {"min": [0, 0, 0, 0], "max": [1, 1, 1]},
                          {"min": "000", "max": [1, 1, 1]}, {"min": [False, 0, 0], "max": [1, 1, 1]},
                          {"min": [math.nan, 0, 0], "max": [1, 1, 1]},
                          {"min": [0, 0, 0], "max": [1, math.inf, 1]},
                          {"min": [0, 0, 0], "max": [0, 1, 1]},
                          {"min": [0, 0, 2], "max": [1, 1, 1]})
        for missing in (False, True):
            for bounds in invalid_bounds:
                value = self.fixture()
                target = value["target_components"][0]
                if missing:
                    target = target["missing_slab_regions"][0]
                target["bounds_mm"] = bounds
                with self.subTest(missing=missing, bounds=bounds), self.assertRaises(AssertionError):
                    validate_nominal_target_path_connectivity(value, 2)

    def test_missing_region_must_stay_inside_its_target_in_all_three_axes(self):
        for coordinate in range(3):
            for side, delta in (("min", -1), ("max", 1)):
                value = self.fixture()
                component = value["target_components"][0]
                bounds = copy.deepcopy(component["bounds_mm"])
                bounds[side][coordinate] += delta
                component["missing_slab_regions"][0]["bounds_mm"] = bounds
                with self.subTest(coordinate=coordinate, side=side), self.assertRaisesRegex(AssertionError, "bounds exceed"):
                    validate_nominal_target_path_connectivity(value, 2)

    def test_positive_target_volume_nonnegative_uncovered_and_integer_component_counts(self):
        changes = ({"target_volume_mm3": 0}, {"target_volume_mm3": -1}, {"target_volume_mm3": math.inf},
                   {"target_volume_mm3": None}, {"target_volume_mm3": True}, {"uncovered_volume_mm3": -1},
                   {"uncovered_volume_mm3": 9}, {"uncovered_volume_mm3": math.nan}, {"uncovered_volume_mm3": True},
                   {"uncovered_volume_mm3": None}, {"clipped_path_component_count": -1},
                   {"clipped_path_component_count": True}, {"clipped_path_component_count": 1.5})
        for change in changes:
            value = self.fixture()
            value["target_components"][0].update(change)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)

    def test_fully_missing_target_and_zero_uncovered_are_known_values(self):
        value = self.fixture()
        target = value["target_components"][0]
        target.update(uncovered_volume_mm3=8, clipped_path_component_count=0,
                      missing_slab_regions=[{"bounds_mm": copy.deepcopy(target["bounds_mm"]), "volume_mm3": 8}])
        value["cross_target_joins"] = []
        validate_nominal_target_path_connectivity(value, 2)
        target.update(uncovered_volume_mm3=0, clipped_path_component_count=1, missing_slab_regions=[])
        validate_nominal_target_path_connectivity(value, 2)

    def test_missing_regions_need_positive_volumes_and_corresponding_uncovered_value(self):
        for volume in (0, -1, None, True, math.inf, math.nan):
            value = self.fixture()
            value["target_components"][0]["missing_slab_regions"][0]["volume_mm3"] = volume
            with self.subTest(volume=volume), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)
        for change in ({"missing_slab_regions": []}, {"missing_slab_regions": None},
                       {"missing_slab_regions": {}}, {"uncovered_volume_mm3": 0}):
            value = self.fixture()
            value["target_components"][0].update(change)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)

    def test_region_accumulation_roundoff_and_bounding_volume_do_not_define_filled_volume(self):
        value = self.fixture()
        target = value["target_components"][0]
        target["uncovered_volume_mm3"] = 0.3
        first = target["missing_slab_regions"][0]
        first["volume_mm3"] = 0.1
        second = copy.deepcopy(first)
        second["bounds_mm"] = {"min": [-4, -2, 0.5], "max": [-2, 2, 0.75]}
        second["volume_mm3"] = 0.2
        target["missing_slab_regions"].append(second)
        self.assertNotEqual(sum(region["volume_mm3"] for region in target["missing_slab_regions"]), 0.3)
        validate_nominal_target_path_connectivity(value, 2)

    def test_target_indices_are_sequential_locally_not_stable_external_ids(self):
        for index in (-1, True, 0.0, 1, 7):
            value = self.fixture()
            value["target_components"][0]["component_index"] = index
            with self.subTest(index=index), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)
        value = self.fixture()
        value["target_components"][1]["component_index"] = 0
        with self.assertRaisesRegex(AssertionError, "Nonsequential"):
            validate_nominal_target_path_connectivity(value, 2)

    def test_cross_target_join_references_are_distinct_valid_indices(self):
        for references in ([], [0], [0, 0], [0, 2], [-1, 0], [True, 0], [0.0, 1], None, {}):
            value = self.fixture()
            value["cross_target_joins"][0]["target_component_indices"] = references
            with self.subTest(references=references), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)
        for index in (-1, True, 1.5, None):
            value = self.fixture()
            value["cross_target_joins"][0]["path_component_index"] = index
            with self.subTest(index=index), self.assertRaises(AssertionError):
                validate_nominal_target_path_connectivity(value, 2)
        value = self.fixture()
        value["cross_target_joins"].append(copy.deepcopy(value["cross_target_joins"][0]))
        with self.assertRaisesRegex(AssertionError, "Duplicate joined path"):
            validate_nominal_target_path_connectivity(value, 2)
        value = self.fixture()
        value["target_components"][1]["clipped_path_component_count"] = 0
        with self.assertRaisesRegex(AssertionError, "joined target must contain"):
            validate_nominal_target_path_connectivity(value, 2)

    def test_present_but_incomplete_contract_cannot_be_treated_as_historical_absence(self):
        paths = [(key,) for key in self.fixture()]
        paths += [("target_components", 0, key) for key in self.fixture()["target_components"][0]]
        paths += [("cross_target_joins", 0, key) for key in self.fixture()["cross_target_joins"][0]]
        paths += [("target_components", 0, "bounds_mm", "min"),
                  ("target_components", 0, "missing_slab_regions", 0, "volume_mm3"),
                  ("target_components", 0, "missing_slab_regions", 0, "bounds_mm", "max")]
        for path in paths:
            value = self.fixture()
            target = value
            for key in path[:-1]:
                target = target[key]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_nominal_target_path_connectivity(value, 2)

    def test_metric_entry_checks_present_block_and_allows_historical_absence(self):
        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "support_layer_count": 0, "model_layer_height_mm": {"count": 3, "invalid_count": 0, "min": 0.2, "max": 0.2},
                 "planned_model_bed_footprint_area_mm2_per_instance": 8,
                 "planned_perimeter_loops": PerimeterLoopsTest().fixture(),
                 "solid_fill_target_depth": SolidFillTargetDepthTest().fixture(),
                 "planned_material_depth": PlannedMaterialDepthTest().fixture(),
                 "nominal_target_path_connectivity": self.fixture()}
        report = {"planned_plate_bed_footprint": PlateBedFootprintTest().fixture(),
                  "objects": {"slice_object_group_count": 1, "source_object_count": 1, "instance_count": 1, "groups": [group]}}
        group["nominal_target_path_connectivity"]["complete"] = False
        with self.assertRaisesRegex(AssertionError, "Incomplete nominal"):
            validate_metric_values(report, {}, {})
        group["nominal_target_path_connectivity"] = self.fixture()
        # Both valid current and absent historical blocks reach the next metric
        # section; this minimal fixture deliberately has no unrelated G-code data.
        with self.assertRaisesRegex(KeyError, "gcode"):
            validate_metric_values(report, {}, {})
        del group["nominal_target_path_connectivity"]
        with self.assertRaisesRegex(KeyError, "gcode"):
            validate_metric_values(report, {}, {})

    def test_fresh_cli_report_requires_block_before_dispatching_metric_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            path = output / "model.gcode"
            path.write_text("G1 X1\n", encoding="utf-8")
            report = {"schema_version": 1, "scope": "sliced_print_metrics", "plate_index": 1,
                      "gcode_file": "model.gcode", "objects": {"groups": [{}]}}
            plate, facts = {"id": 1}, {"path": path.resolve()}
            with patch("test_cli_slice_analysis.validate_metric_values") as metric_validator:
                with self.assertRaisesRegex(AssertionError, "Current CLI output lacks nominal_target_path_connectivity"):
                    validate_report(report, output, plate, facts)
                metric_validator.assert_not_called()
                report["objects"]["groups"][0]["nominal_target_path_connectivity"] = self.fixture()
                validate_report(report, output, plate, facts)
                metric_validator.assert_called_once_with(report, plate, facts)


class PerimeterLoopsTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "per_instance_closed_model_perimeter_centerlines_before_gcode", "complete": True,
                "loop_count": 2, "loop_length_mm": {"min": 4, "max": 14}, "turn_angle_degrees": {"min": 0, "max": 90}}

    def test_complete_minimum_and_directional_turn(self):
        validate_perimeter_loops(self.fixture())

    def test_no_applicable_loops_and_unknown_are_distinct(self):
        for complete, loops in ((True, 0), (False, None)):
            validate_perimeter_loops({**self.fixture(), "complete": complete, "loop_count": loops,
                                      "loop_length_mm": {"min": None, "max": None},
                                      "turn_angle_degrees": {"min": None, "max": None}})

    def test_invalid_loop_ranges_and_partial_totals_are_rejected(self):
        for change in ({"loop_count": -1}, {"loop_count": True}, {"loop_count": 0}, {"complete": False},
                       {"loop_length_mm": {"min": 0, "max": 4}}, {"loop_length_mm": {"min": 3, "max": 2}},
                       {"turn_angle_degrees": {"min": 0, "max": 181}}, {"turn_angle_degrees": {"min": None, "max": 1}}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_perimeter_loops({**self.fixture(), **change})


class PlateBedFootprintTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "planned_path_nominal_bottom_on_bed_before_gcode", "complete": True,
                "model_area_mm2": 24, "support_and_raft_area_mm2": 12, "brim_area_mm2": 8, "combined_area_mm2": 40}

    def test_union_keeps_overlapping_categories_separate(self):
        validate_plate_bed_footprint(self.fixture())

    def test_no_printed_paths_is_known_zero(self):
        value = self.fixture()
        value.update(model_area_mm2=0, support_and_raft_area_mm2=0, brim_area_mm2=0, combined_area_mm2=0)
        validate_plate_bed_footprint(value)

    def test_unknown_has_no_partial_area(self):
        value = self.fixture()
        value.update(complete=False, model_area_mm2=None, support_and_raft_area_mm2=None,
                     brim_area_mm2=None, combined_area_mm2=None)
        validate_plate_bed_footprint(value)

    def test_contradictions_are_rejected(self):
        for change in ({"combined_area_mm2": 20}, {"combined_area_mm2": 45}, {"model_area_mm2": -1},
                       {"support_and_raft_area_mm2": math.inf}, {"brim_area_mm2": True}, {"complete": False},
                       {"complete": 1}, {"scope": "model_only"}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_plate_bed_footprint({**self.fixture(), **change})


class PlateBedRegionsTest(unittest.TestCase):
    categories = ("model", "support_and_raft", "brim", "combined")

    def fixture(self):
        value = PlateBedFootprintTest().fixture()
        for category in self.categories:
            value[category + "_regions"] = [{"area_mm2": value[category + "_area_mm2"],
                                             "bounds_mm": {"min": [-10, -5], "max": [10, 5]}}]
        value["model_regions"] = [{"area_mm2": 10, "bounds_mm": {"min": [-10, -5], "max": [-5, 0]}},
                                  {"area_mm2": 14, "bounds_mm": {"min": [0, 0], "max": [5, 5]}}]
        return value

    def test_negative_world_coordinates_holes_and_region_reordering_are_valid(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        # Area is material area with holes removed, not the rectangle's area.
        validate_plate_bed_footprint(value)
        self.assertEqual(value, before)
        value["model_regions"].reverse()
        validate_plate_bed_footprint(value)

    def test_old_reports_without_any_region_array_remain_compatible(self):
        value = PlateBedFootprintTest().fixture()
        validate_plate_bed_footprint(value)
        value.update(complete=False, model_area_mm2=None, support_and_raft_area_mm2=None,
                     brim_area_mm2=None, combined_area_mm2=None)
        validate_plate_bed_footprint(value)

    def test_complete_empty_categories_are_empty_arrays_not_unknown(self):
        value = self.fixture()
        for category in self.categories:
            value[category + "_area_mm2"] = 0
            value[category + "_regions"] = []
        validate_plate_bed_footprint(value)
        for category in self.categories:
            with self.subTest(category=category), self.assertRaises(AssertionError):
                validate_plate_bed_footprint({**value, category + "_regions": None})
        value = self.fixture()
        with self.assertRaises(AssertionError):
            validate_plate_bed_footprint({**value, "model_regions": []})

    def test_incomplete_regions_are_all_null_not_partial_or_empty_arrays(self):
        value = self.fixture()
        value["complete"] = False
        for category in self.categories:
            value[category + "_area_mm2"] = None
            value[category + "_regions"] = None
        validate_plate_bed_footprint(value)
        for category in self.categories:
            for invalid in ([], self.fixture()[category + "_regions"]):
                with self.subTest(category=category, invalid=invalid), self.assertRaises(AssertionError):
                    validate_plate_bed_footprint({**value, category + "_regions": invalid})

    def test_region_schema_is_complete_when_any_optional_array_is_present(self):
        for category in self.categories:
            value = self.fixture()
            del value[category + "_regions"]
            with self.subTest(category=category), self.assertRaises(KeyError):
                validate_plate_bed_footprint(value)
        for path in (("area_mm2",), ("bounds_mm",), ("bounds_mm", "min"), ("bounds_mm", "max")):
            value = self.fixture()
            region = value["model_regions"][0]
            target = region if len(path) == 1 else region[path[0]]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_plate_bed_footprint(value)

    def test_invalid_region_area_and_bounds_are_rejected_without_a_tolerance(self):
        for area in (0, -1, True, None, math.inf, math.nan):
            value = self.fixture()
            value["model_regions"][0]["area_mm2"] = area
            with self.subTest(area=area), self.assertRaises(AssertionError):
                validate_plate_bed_footprint(value)
        for low, high in (([0], [1]), ([0, 0, 0], [1, 1, 1]), ([0, 0], [0, 1]), ([1, 0], [0, 1]),
                          ([True, 0], [1, 1]), ([math.nan, 0], [1, 1]), ([0, 0], [math.inf, 1]), (None, [1, 1])):
            value = self.fixture()
            value["model_regions"][0]["bounds_mm"] = {"min": low, "max": high}
            with self.subTest(low=low, high=high), self.assertRaises(AssertionError):
                validate_plate_bed_footprint(value)

    def test_region_sum_is_not_required_to_match_a_differently_ordered_total_bitwise(self):
        value = self.fixture()
        value["model_area_mm2"] = math.nextafter(value["model_area_mm2"], math.inf)
        self.assertNotEqual(sum(region["area_mm2"] for region in value["model_regions"]), value["model_area_mm2"])
        validate_plate_bed_footprint(value)

    def test_full_report_entry_validates_present_regions_before_other_metrics(self):
        value = self.fixture()
        value["model_regions"][0]["area_mm2"] = 0
        with self.assertRaisesRegex(AssertionError, "regions must have positive area"):
            validate_metric_values({"planned_plate_bed_footprint": value}, {}, {})


class FixtureBedFootprintTest(unittest.TestCase):
    def check(self, name="raised-stroke", model=24, support=0, brim=0, complete=True, settings=None):
        bed = {"scope": "planned_path_nominal_bottom_on_bed_before_gcode", "complete": complete,
               "model_area_mm2": model, "support_and_raft_area_mm2": support, "brim_area_mm2": brim,
               "combined_area_mm2": model + support + brim if complete else None}
        tools.verify_fixture_bed_footprint(name, {"planned_plate_bed_footprint": bed},
                                          settings or {"raft_layers": "0", "enable_support": "0"})

    def test_on_bed_model_with_optional_auto_brim(self):
        self.check()
        self.check(brim=8)

    def test_normal_and_tree_support_have_positive_bed_area(self):
        for name in ("bridge", "cantilever", "new-island", "new-island-tree"):
            with self.subTest(name=name):
                self.check(name=name, support=12, settings={"raft_layers": "0", "enable_support": "1"})

    def test_raft_lifts_model_but_keeps_support_on_bed(self):
        self.check(name="cura-raft", model=0, support=40,
                   settings={"raft_mode": "cura_v1", "raft_layers": "3", "enable_support": "0"})

    def test_missing_known_geometry_is_not_allowed_to_pass_as_unknown(self):
        with self.assertRaises(AssertionError):
            self.check(model=None, support=None, brim=None, complete=False)
        with self.assertRaises(AssertionError):
            self.check(model=0)
        with self.assertRaises(AssertionError):
            self.check(support=12)
        with self.assertRaises(AssertionError):
            self.check(name="new-island-tree", settings={"raft_layers": "0", "enable_support": "1"})

    def test_raft_cannot_relabel_model_paths_as_bed_contact(self):
        settings = {"raft_mode": "cura_v1", "raft_layers": "3", "enable_support": "0"}
        with self.assertRaises(AssertionError):
            self.check(name="cura-raft", model=1, support=40, settings=settings)
        with self.assertRaises(AssertionError):
            self.check(name="cura-raft", model=0, support=0, brim=8, settings=settings)

    def test_changed_fixture_configuration_does_not_reuse_known_bed_claims(self):
        with self.assertRaises(AssertionError):
            self.check(settings={"raft_layers": "1", "enable_support": "0"})
        with self.assertRaises(AssertionError):
            self.check(name="cura-raft", model=0, support=40,
                       settings={"raft_mode": "legacy", "raft_layers": "3"})


class EstimatedFlowCapDistanceTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "normal_estimator_finalized_linear_e_block_phases", "complete": True,
                "participating_path_length_mm": 3, "above_cap_path_length_mm": 1,
                "above_cap_path_length_ratio": 1 / 3}

    def test_partial_phase_distance_uses_its_own_material_denominator(self):
        values = [self.fixture(), {**self.fixture(), "participating_path_length_mm": 5,
                                   "above_cap_path_length_mm": 4, "above_cap_path_length_ratio": 0.8}]
        before = copy.deepcopy(values)
        for value, cap in zip(values, (8, 12)):
            validate_estimated_flow_cap_distance(value, cap)
        self.assertEqual(values, before)

    def test_unused_material_and_zero_flow_have_different_denominators(self):
        validate_estimated_flow_cap_distance({**self.fixture(), "participating_path_length_mm": 0,
                                             "above_cap_path_length_mm": 0, "above_cap_path_length_ratio": None}, 8)
        # Positive-E motion with M221 S0 remains participating distance, with no above-cap part.
        validate_estimated_flow_cap_distance({**self.fixture(), "above_cap_path_length_mm": 0,
                                             "above_cap_path_length_ratio": 0}, 8)
        validate_estimated_flow_cap_distance({**self.fixture(), "above_cap_path_length_mm": 3,
                                             "above_cap_path_length_ratio": 1}, 8)
        with self.assertRaises(AssertionError):
            validate_estimated_flow_cap_distance({**self.fixture(), "participating_path_length_mm": 0,
                                                 "above_cap_path_length_mm": 0, "above_cap_path_length_ratio": 0}, 8)

    def test_unknown_never_exposes_partial_distance_or_zero_for_unavailable_metadata(self):
        value = {**self.fixture(), "complete": False, "participating_path_length_mm": None,
                 "above_cap_path_length_mm": None, "above_cap_path_length_ratio": None}
        for cap in (8, None, 0, -1, math.nan, math.inf):
            validate_estimated_flow_cap_distance(value, cap)
        for field in ("participating_path_length_mm", "above_cap_path_length_mm", "above_cap_path_length_ratio"):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                validate_estimated_flow_cap_distance({**value, field: 0}, 8)

    def test_unavailable_caps_cannot_report_a_complete_comparison_even_if_unused(self):
        empty = {**self.fixture(), "participating_path_length_mm": 0,
                 "above_cap_path_length_mm": 0, "above_cap_path_length_ratio": None}
        for cap in (None, 0, -1, True, math.nan, math.inf):
            for value in (self.fixture(), empty):
                with self.subTest(cap=cap, value=value), self.assertRaises(AssertionError):
                    validate_estimated_flow_cap_distance(value, cap)
        validate_estimated_flow_cap_distance(self.fixture(), 1e-320)

    def test_ratio_and_subset_constraints_are_strict_without_numeric_tolerance(self):
        for change in ({"above_cap_path_length_ratio": math.nextafter(1 / 3, 1)},
                       {"above_cap_path_length_ratio": math.nextafter(1 / 3, 0)},
                       {"above_cap_path_length_mm": math.nextafter(3., math.inf), "above_cap_path_length_ratio": 1},
                       {"above_cap_path_length_mm": 0, "above_cap_path_length_ratio": None}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_estimated_flow_cap_distance({**self.fixture(), **change}, 8)

    def test_invalid_scope_types_and_distances_are_rejected(self):
        for change in ({"scope": "measured_nozzle_flow"}, {"complete": 1}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_estimated_flow_cap_distance({**self.fixture(), **change}, 8)
        for field in ("participating_path_length_mm", "above_cap_path_length_mm", "above_cap_path_length_ratio"):
            for invalid in (None, -1, True, math.nan, math.inf):
                with self.subTest(field=field, value=invalid), self.assertRaises(AssertionError):
                    validate_estimated_flow_cap_distance({**self.fixture(), field: invalid}, 8)

    def test_missing_fields_do_not_become_known_zero(self):
        for field in self.fixture():
            value = self.fixture()
            del value[field]
            with self.subTest(field=field), self.assertRaises(KeyError):
                validate_estimated_flow_cap_distance(value, 8)


class EstimatedPeakFlowTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "normal_estimator_finalized_linear_e_blocks", "complete": True, "block_count": 3,
                "peak_volumetric_rate_mm3_s": {"min": 2, "max": 4}, "max_to_configured_cap_ratio": 0.5}

    def test_known_zero_flow_is_not_unknown_or_no_blocks(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        validate_estimated_peak_flow(value, 8)
        self.assertEqual(value, before)
        value["peak_volumetric_rate_mm3_s"] = {"min": 0, "max": 0}
        value["max_to_configured_cap_ratio"] = 0
        validate_estimated_peak_flow(value, 8)

    def test_unknown_and_zero_blocks_preserve_distinct_states_without_partial_values(self):
        for complete, blocks in ((False, None), (True, 0)):
            value = {**self.fixture(), "complete": complete, "block_count": blocks,
                     "peak_volumetric_rate_mm3_s": {"min": None, "max": None}, "max_to_configured_cap_ratio": None}
            validate_estimated_peak_flow(value, 8)
            for field in ("peak_volumetric_rate_mm3_s", "max_to_configured_cap_ratio"):
                bad = copy.deepcopy(value)
                bad[field] = {"min": 0, "max": 0} if field == "peak_volumetric_rate_mm3_s" else 0
                with self.subTest(complete=complete, field=field), self.assertRaises(AssertionError):
                    validate_estimated_peak_flow(bad, 8)
            if not complete:
                with self.assertRaises(AssertionError):
                    validate_estimated_peak_flow({**value, "block_count": 0}, 8)

    def test_missing_unlimited_or_invalid_cap_does_not_hide_known_raw_flow(self):
        for cap in (None, 0, -1, math.nan, math.inf, True, "8", 1e-320):
            value = {**self.fixture(), "max_to_configured_cap_ratio": None}
            with self.subTest(cap=cap):
                validate_estimated_peak_flow(value, cap)
                with self.assertRaises(AssertionError):
                    validate_estimated_peak_flow({**value, "max_to_configured_cap_ratio": 0}, cap)
        # A tiny cap is not inherently invalid: a finite 0/cap remains known zero.
        validate_estimated_peak_flow({**self.fixture(), "peak_volumetric_rate_mm3_s": {"min": 0, "max": 0},
                                      "max_to_configured_cap_ratio": 0}, 1e-320)

    def test_valid_material_caps_require_their_own_ratio_without_quality_thresholds(self):
        for cap in (2, 8, 16):
            value = {**self.fixture(), "max_to_configured_cap_ratio": 4 / cap}
            validate_estimated_peak_flow(value, cap)
            for invalid_ratio in (None, -1, True, math.nan, math.inf, 4 / cap + 0.1):
                with self.subTest(cap=cap, ratio=invalid_ratio), self.assertRaises(AssertionError):
                    validate_estimated_peak_flow({**value, "max_to_configured_cap_ratio": invalid_ratio}, cap)

    def test_invalid_scope_counts_and_ranges_are_rejected(self):
        for change in ({"scope": "measured_nozzle_flow"}, {"complete": 1}, {"block_count": None},
                       {"block_count": -1}, {"block_count": True}, {"block_count": 3.0}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_estimated_peak_flow({**self.fixture(), **change}, 8)
        for bounds in ({"min": -1, "max": 4}, {"min": 5, "max": 4}, {"min": True, "max": 4},
                       {"min": None, "max": 4}, {"min": 2, "max": math.nan}, {"min": 2, "max": math.inf}):
            with self.subTest(bounds=bounds), self.assertRaises(AssertionError):
                validate_estimated_peak_flow({**self.fixture(), "peak_volumetric_rate_mm3_s": bounds}, 8)

    def test_missing_fields_do_not_become_known_zero(self):
        paths = [(key,) for key in self.fixture()] + [("peak_volumetric_rate_mm3_s", bound) for bound in ("min", "max")]
        for path in paths:
            value = self.fixture()
            target = value if len(path) == 1 else value[path[0]]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_estimated_peak_flow(value, 8)

    def test_full_report_entry_checks_material_flow_without_changing_old_ranges(self):
        def samples(value, sample_count=1):
            return {"count": sample_count, "invalid_count": 0, "min": value, "max": value}

        ranges = {key: samples(value) for key, value in (("width_mm", 0.4), ("height_mm", 0.2),
                  ("commanded_feedrate_mm_s", 10), ("nominal_volumetric_rate_mm3_s", 4), ("segment_length_mm", 10))}
        ranges["path_length_mm"] = 10
        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "support_layer_count": 0, "model_layer_height_mm": samples(0.2, 3),
                 "planned_model_bed_footprint_area_mm2_per_instance": 8,
                 "planned_perimeter_loops": PerimeterLoopsTest().fixture(),
                 "solid_fill_target_depth": SolidFillTargetDepthTest().fixture(),
                 "planned_material_depth": PlannedMaterialDepthTest().fixture()}
        material = {"extruder_id": 0, "extrusion_ranges": ranges, "configured_filament_max_volumetric_speed_mm3_s": 8,
                    "max_nominal_flow_to_cap_ratio": 0.5, "estimated_peak_flow": self.fixture(),
                    "estimated_flow_cap_distance": EstimatedFlowCapDistanceTest().fixture(),
                    "layer_time_threshold_observation": {**LayerTimeThresholdObservationTest().fixture(),
                        "active_layer_sequence_count": 1, "below_threshold_layer_ratio": 1},
                    "estimated_volume_mm3": {"total": 1, "other": 1, "support": 0, "wipe_tower": 0, "flush": 0}}
        gcode = {"move_counts": {"total": 1, "extrude": 1, "travel": 0, "retract": 0, "unretract": 0, "wipe": 0, "other": 0},
                 "travel": {"segment_length_mm": samples(None, 0), "path_length_mm": 0}, "seam_record_count": 0,
                 "seam_position_bounds_mm": {"complete": True, "min": None, "max": None},
                 "seam_positions_mm": {"scope": "gcode_processor_preview_coordinates_in_move_order", "complete": True, "positions": []},
                 "normal_estimate": {"total_time_s": 1, "prepare_time_s": 0, "layer_sequence_count": 1,
                                     "layer_duration_s": samples(1), "layer_duration_sequence_s": [1]},
                 "extrusion_ranges": ranges, "extruders": [material], "filament_by_role": [],
                 "extrusion_by_role": [{"role_id": 1, "role": "perimeter", "extrusion_ranges": ranges,
                    "estimated_motion": EstimatedMotionTest().fixture(),
                    "configured_flow_comparison": {"complete": True, "above_cap_path_length_mm": 0, "above_cap_path_length_ratio": 0}}]}
        report = {"planned_plate_bed_footprint": {"scope": "planned_path_nominal_bottom_on_bed_before_gcode",
                  "complete": True, "model_area_mm2": 8, "support_and_raft_area_mm2": 0, "brim_area_mm2": 0, "combined_area_mm2": 8},
                  "objects": {"slice_object_group_count": 1, "source_object_count": 1, "instance_count": 1, "groups": [group]},
                  "gcode": gcode, "limitations": ["Software estimates, not measured output."]}
        plate = {"total_time": 1, "total_filament_g": 0.001, "support_filament_g": 0}
        facts = {"layers": 1, "filament_densities": [1], "footer_mass_g": 0.001}
        before = copy.deepcopy(report)
        validate_metric_values(report, plate, facts)
        self.assertEqual(report, before)
        material["estimated_flow_cap_distance"]["above_cap_path_length_ratio"] = math.nextafter(1 / 3, 1)
        with self.assertRaises(AssertionError):
            validate_metric_values(report, plate, facts)
        del material["estimated_flow_cap_distance"]
        with self.assertRaisesRegex(KeyError, "estimated_flow_cap_distance"):
            validate_metric_values(report, plate, facts)
        material["estimated_flow_cap_distance"] = EstimatedFlowCapDistanceTest().fixture()
        material["estimated_peak_flow"]["max_to_configured_cap_ratio"] = None
        with self.assertRaises(AssertionError):
            validate_metric_values(report, plate, facts)
        del material["estimated_peak_flow"]
        with self.assertRaisesRegex(KeyError, "estimated_peak_flow"):
            validate_metric_values(report, plate, facts)


class EstimatedMotionTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "normal_estimator_finalized_extrusion_blocks", "complete": True, "block_count": 3,
                "peak_speed_mm_s": {"min": 2, "max": 80},
                "tangential_acceleration_mm_s2": {"min": -1000, "max": 800}}

    def test_positive_speed_and_signed_acceleration_are_independent_ranges(self):
        for low, high in ((-1000, 800), (-1000, -10), (0, 0), (10, 800)):
            value = self.fixture()
            value["tangential_acceleration_mm_s2"] = {"min": low, "max": high}
            before = copy.deepcopy(value)
            validate_estimated_motion(value)
            self.assertEqual(value, before)

    def test_unfinalized_or_unavailable_blocks_are_unknown_not_zero(self):
        value = {**self.fixture(), "complete": False, "block_count": None,
                 "peak_speed_mm_s": {"min": None, "max": None},
                 "tangential_acceleration_mm_s2": {"min": None, "max": None}}
        validate_estimated_motion(value)
        for field in ("block_count", "peak_speed_mm_s", "tangential_acceleration_mm_s2"):
            bad = copy.deepcopy(value)
            bad[field] = 0 if field == "block_count" else {"min": 0, "max": 0}
            with self.subTest(field=field), self.assertRaises(AssertionError):
                validate_estimated_motion(bad)

    def test_known_zero_blocks_have_no_applicable_range(self):
        value = {**self.fixture(), "block_count": 0,
                 "peak_speed_mm_s": {"min": None, "max": None},
                 "tangential_acceleration_mm_s2": {"min": None, "max": None}}
        validate_estimated_motion(value)
        for field in ("peak_speed_mm_s", "tangential_acceleration_mm_s2"):
            bad = copy.deepcopy(value)
            bad[field]["min"] = 0
            with self.subTest(field=field), self.assertRaises(AssertionError):
                validate_estimated_motion(bad)

    def test_invalid_types_nonfinite_values_and_inverted_ranges_are_rejected(self):
        for change in ({"scope": "measured_machine_motion"}, {"complete": 1}, {"block_count": True},
                       {"block_count": -1}, {"block_count": 3.0}, {"block_count": None},
                       {"peak_speed_mm_s": {"min": 0, "max": 80}},
                       {"peak_speed_mm_s": {"min": -1, "max": 80}}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_estimated_motion({**self.fixture(), **change})
        for field in ("peak_speed_mm_s", "tangential_acceleration_mm_s2"):
            for bounds in ({"min": 2, "max": 1}, {"min": True, "max": 80}, {"min": 1, "max": None},
                           {"min": math.nan, "max": 80}, {"min": 1, "max": math.inf}, {"min": "1", "max": 80}):
                with self.subTest(field=field, bounds=bounds), self.assertRaises(AssertionError):
                    validate_estimated_motion({**self.fixture(), field: bounds})

    def test_missing_fields_are_not_replaced_by_known_zero(self):
        paths = [(key,) for key in self.fixture()]
        paths += [(field, bound) for field in ("peak_speed_mm_s", "tangential_acceleration_mm_s2")
                  for bound in ("min", "max")]
        for path in paths:
            value = self.fixture()
            target = value if len(path) == 1 else value[path[0]]
            del target[path[-1]]
            with self.subTest(path=path), self.assertRaises(KeyError):
                validate_estimated_motion(value)

    def test_role_observation_is_required_even_when_configured_flow_is_unknown(self):
        metrics = GcodeObservationsTest().fixture()
        metrics["extrusion_by_role"][0]["configured_flow_comparison"] = {
            "complete": False, "above_cap_path_length_mm": None, "above_cap_path_length_ratio": None}
        validate_gcode_observations(metrics)
        missing = copy.deepcopy(metrics)
        del missing["extrusion_by_role"][0]["estimated_motion"]
        with self.assertRaisesRegex(KeyError, "estimated_motion"):
            validate_gcode_observations(missing)
        second = copy.deepcopy(metrics["extrusion_by_role"][0])
        second["estimated_motion"]["complete"] = False
        metrics["extrusion_by_role"].append(second)
        with self.assertRaisesRegex(AssertionError, "Incomplete estimated motion"):
            validate_gcode_observations(metrics)

    def test_full_report_validation_checks_the_motion_contract(self):
        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "support_layer_count": 0, "model_layer_height_mm": {"count": 3, "invalid_count": 0, "min": 0.2, "max": 0.2},
                 "planned_model_bed_footprint_area_mm2_per_instance": 8,
                 "planned_perimeter_loops": PerimeterLoopsTest().fixture(),
                 "solid_fill_target_depth": SolidFillTargetDepthTest().fixture(),
                 "planned_material_depth": PlannedMaterialDepthTest().fixture()}
        report = {"planned_plate_bed_footprint": {"scope": "planned_path_nominal_bottom_on_bed_before_gcode",
                  "complete": True, "model_area_mm2": 8, "support_and_raft_area_mm2": 0,
                  "brim_area_mm2": 0, "combined_area_mm2": 8},
                  "objects": {"slice_object_group_count": 1, "source_object_count": 1, "instance_count": 1,
                              "groups": [group]}, "gcode": GcodeObservationsTest().fixture()}
        report["gcode"]["extrusion_by_role"][0]["estimated_motion"]["scope"] = "physical_acceleration"
        with self.assertRaisesRegex(AssertionError, "Unexpected estimated-motion scope"):
            validate_metric_values(report, {}, {})
        del report["gcode"]["extrusion_by_role"][0]["estimated_motion"]
        with self.assertRaisesRegex(KeyError, "estimated_motion"):
            validate_metric_values(report, {}, {})


class GcodeObservationsTest(unittest.TestCase):
    def fixture(self):
        return {
            "move_counts": {"travel": 2},
            "travel": {"segment_length_mm": {"count": 2, "invalid_count": 0, "min": 2, "max": 8}, "path_length_mm": 10},
            "seam_record_count": 2,
            "seam_position_bounds_mm": {"complete": True, "min": [-4, -2, 0.2], "max": [2, 8, 1.2]},
            "seam_positions_mm": {"scope": "gcode_processor_preview_coordinates_in_move_order", "complete": True,
                                  "positions": [[-4, 8, 0.2], [2, -2, 1.2]]},
            "extrusion_by_role": [{"extrusion_ranges": {"path_length_mm": 20},
                                   "estimated_motion": EstimatedMotionTest().fixture(),
                                   "configured_flow_comparison": {"complete": True, "above_cap_path_length_mm": 2,
                                                                  "above_cap_path_length_ratio": 0.1}}],
        }

    def test_complete_signed_bounds_and_length_weighted_ratio(self):
        validate_gcode_observations(self.fixture())

    def test_incomplete_fields_remain_null(self):
        data = self.fixture()
        data["travel"] = {"segment_length_mm": {"count": 1, "invalid_count": 1, "min": 2, "max": 2}, "path_length_mm": None}
        data["seam_position_bounds_mm"] = {"complete": False, "min": None, "max": None}
        data["seam_positions_mm"].update(complete=False, positions=None)
        data["extrusion_by_role"][0]["configured_flow_comparison"] = {
            "complete": False, "above_cap_path_length_mm": None, "above_cap_path_length_ratio": None}
        validate_gcode_observations(data)

    def test_no_travel_no_seams_and_zero_role_length(self):
        data = self.fixture()
        data["move_counts"]["travel"] = 0
        data["travel"] = {"segment_length_mm": {"count": 0, "invalid_count": 0, "min": None, "max": None}, "path_length_mm": 0}
        data["seam_record_count"] = 0
        data["seam_position_bounds_mm"] = {"complete": True, "min": None, "max": None}
        data["seam_positions_mm"]["positions"] = []
        data["extrusion_by_role"][0]["extrusion_ranges"]["path_length_mm"] = 0
        data["extrusion_by_role"][0]["configured_flow_comparison"] = {
            "complete": True, "above_cap_path_length_mm": 0, "above_cap_path_length_ratio": None}
        validate_gcode_observations(data)

    def test_contradictions_are_rejected(self):
        for kind in ("travel_count", "travel_sum", "travel_unknown", "seam_order", "seam_finite", "seam_count",
                     "flow_denominator", "flow_unknown", "flow_zero", "flow_boolean"):
            with self.subTest(kind=kind):
                data = self.fixture()
                flow = data["extrusion_by_role"][0]["configured_flow_comparison"]
                if kind == "travel_count":
                    data["move_counts"]["travel"] = 3
                elif kind == "travel_sum":
                    data["travel"]["path_length_mm"] = 100
                elif kind == "travel_unknown":
                    data["travel"]["segment_length_mm"].update(count=1, invalid_count=1)
                elif kind == "seam_order":
                    data["seam_position_bounds_mm"]["min"][0] = 20
                elif kind == "seam_finite":
                    data["seam_position_bounds_mm"]["max"][0] = math.inf
                elif kind == "seam_count":
                    data["seam_record_count"] = 0
                elif kind == "flow_denominator":
                    flow["above_cap_path_length_ratio"] = 0.5
                elif kind == "flow_unknown":
                    flow["complete"] = False
                elif kind == "flow_zero":
                    data["extrusion_by_role"][0]["extrusion_ranges"]["path_length_mm"] = 0
                elif kind == "flow_boolean":
                    flow["complete"] = 1
                with self.assertRaises(AssertionError):
                    validate_gcode_observations(data)

    def test_seam_points_keep_order_and_duplicates_but_reject_invalid_or_partial_lists(self):
        data = self.fixture()
        data["seam_positions_mm"]["positions"].append([-4, 8, 0.2])
        data["seam_record_count"] = 3
        validate_gcode_observations(data)
        for change in ({"positions": []}, {"positions": [[0, 0, 0]]}, {"complete": False},
                       {"positions": [[-4, 8, 0.2], [2, -2, math.nan]]}, {"scope": "model_local"}):
            bad = self.fixture()
            bad["seam_positions_mm"].update(change)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_gcode_observations(bad)


class ParallelClearanceTest(unittest.TestCase):
    def layer(self, material=None, void=None, complete=True):
        return {"complete": True, "parallel_boundary_clearance": {
            "scope": "same_section_island_parallel_voronoi_sites", "complete": complete,
            "min_material_width_mm": material, "min_void_width_mm": void}}

    def test_exact_open_slot_and_neck_widths(self):
        for name, material, void in (("u-slot-wide", 3, 2), ("u-slot-narrow", 3, 0.36),
                                     ("c-slot", 3, 0.5), ("narrow-neck", 0.36, None)):
            with self.subTest(name=name):
                tools.verify_parallel_clearance([self.layer(material, void)], name)

    def test_no_applicable_pairs_and_unknowns_are_distinct(self):
        tools.verify_parallel_clearance([self.layer(), self.layer(complete=False)])

    def test_invalid_or_contradictory_measurements_rejected(self):
        for layer in (self.layer(0), self.layer(-1), self.layer(math.inf), self.layer(1, complete=False),
                      self.layer(1, void=True), {**self.layer(1), "complete": False}):
            with self.subTest(layer=layer), self.assertRaises(AssertionError):
                tools.verify_parallel_clearance([layer])

    def test_nonmatching_known_fixture_width_rejected(self):
        with self.assertRaises(AssertionError):
            tools.verify_parallel_clearance([self.layer(3, 1)], "u-slot-wide")


class CellMeshTest(unittest.TestCase):
    def test_cube_and_shared_faces(self):
        cube = tools.cell_mesh([0, 1], [0, 1], [0, 1], {(0, 0, 0)})
        self.assertEqual(len(cube), 12)
        two_cells = tools.cell_mesh([0, 1, 2], [0, 1], [0, 1], {(0, 0, 0), (1, 0, 0)})
        self.assertEqual(len(two_cells), 20)
        self.assertFalse(any(all(p[0] == 1 for p in triangle) for triangle in two_cells))

    def test_deterministic_order(self):
        cells = [(0, 0, 0), (1, 0, 0)]
        self.assertEqual(tools.cell_mesh([0, 1, 2], [0, 1], [0, 1], set(cells)),
                         tools.cell_mesh([0, 1, 2], [0, 1], [0, 1], set(reversed(cells))))

    def test_edge_only_contact_is_not_a_valid_manifold_fixture(self):
        with self.assertRaises(AssertionError):
            tools.cell_mesh([0, 1, 2], [0, 1, 2], [0, 1], {(0, 0, 0), (1, 1, 0)})

    def test_exported_stroke_requires_motion_and_extrusion_above_base(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.gcode"
            path.write_text("G90\nM83\n;TYPE:Outer wall\nG1 X1 Z1.2 E1\nG1 X2 Z1.4 E1\n"
                            "G1 Z1.6\nG1 E1\nG1 X3\nG1 X4 E-1\nM82\nG92 E0\nG1 X5 E1\n"
                            ";TYPE:Custom\nG1 X6 Z2.0 E2\n", encoding="utf-8")
            self.assertEqual(tools.raised_stroke_gcode_layers(path), [1.4, 1.6])

    def test_exported_stroke_does_not_guess_arc_coverage(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.gcode"
            path.write_text("M83\n;TYPE:Outer wall\nG1 Z1.4\nG2 X1 E1\n", encoding="utf-8")
            with self.assertRaises(AssertionError):
                tools.raised_stroke_gcode_layers(path)

    def test_support_fixtures_are_closed_with_expected_volume_and_bed_reference(self):
        with tempfile.TemporaryDirectory() as folder:
            fixtures = tools.fixtures(Path(folder))
            self.assertEqual(len(fixtures), 17)
            self.assertEqual(fixtures["new-island"].read_bytes(), fixtures["new-island-tree"].read_bytes())
            for name, volume in {"bridge": 307.2, "cantilever": 182.4, "new-island": 153.6, "new-island-tree": 153.6}.items():
                with self.subTest(name=name):
                    vertices = [tuple(map(float, line.split()[1:])) for line in fixtures[name].read_text().splitlines()
                                if line.startswith("vertex ")]
                    signed_volume = 0
                    for i in range(0, len(vertices), 3):
                        a, b, c = vertices[i:i + 3]
                        signed_volume += (a[0] * (b[1] * c[2] - b[2] * c[1])
                                          + a[1] * (b[2] * c[0] - b[0] * c[2])
                                          + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
                    self.assertAlmostEqual(signed_volume, volume)
                    self.assertEqual(min(p[2] for p in vertices), 0)
                    self.assertEqual(max(p[2] for p in vertices), 5.2)

    def test_wedge_is_closed_outward_and_has_the_expected_source_plane(self):
        triangles = tools.wedge_mesh()
        self.assertEqual(len(triangles), 8)
        edges = Counter((a, b) for tri in triangles for a, b in zip(tri, tri[1:] + tri[:1]))
        self.assertTrue(all(number == 1 and edges[(b, a)] == 1 for (a, b), number in edges.items()))
        volume = 0
        slopes, horizontal = [], 0
        for a, b, c in triangles:
            volume += (a[0] * (b[1] * c[2] - b[2] * c[1])
                       + a[1] * (b[2] * c[0] - b[0] * c[2])
                       + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
            self.assertTrue(all(0 <= z <= x <= 4 and 0 <= y <= 6 for x, y, z in (a, b, c)))
            if a[2] == b[2] == c[2]:
                horizontal += 1
                self.assertEqual(a[2], 0)
            if all(x == z for x, y, z in (a, b, c)):
                u, v = (tuple(p[d] - a[d] for d in range(3)) for p in (b, c))
                n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                slopes.append(abs(n[2]) / math.sqrt(sum(value * value for value in n)))
        self.assertEqual(volume, 48)
        self.assertEqual(horizontal, 2)
        self.assertEqual(len(slopes), 2)
        self.assertTrue(all(math.isclose(value, 1 / math.sqrt(2)) for value in slopes))
        with tempfile.TemporaryDirectory() as folder:
            first, second = Path(folder) / "first.stl", Path(folder) / "second.stl"
            tools.write_stl(first, triangles)
            tools.write_stl(second, tools.wedge_mesh())
            self.assertEqual(first.read_bytes(), second.read_bytes())


    def test_narrow_neck_is_a_closed_connected_grid_with_expected_volume(self):
        triangles = tools.cell_mesh([0, 4, 8, 12], [0, 1.82, 2.18, 4], [0, 2.4],
                                    {(i, j, 0) for i in (0, 2) for j in range(3)} | {(1, 1, 0)})
        volume = 0
        for a, b, c in triangles:
            volume += (a[0] * (b[1] * c[2] - b[2] * c[1])
                       + a[1] * (b[2] * c[0] - b[0] * c[2])
                       + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
        self.assertAlmostEqual(volume, (32 + 4 * 0.36) * 2.4)
        # Internal interfaces joining the neck to each large end are removed.
        self.assertFalse(any(all(p[0] in (4, 8) and 1.82 <= p[1] <= 2.18 for p in tri)
                             and len({p[0] for p in tri}) == 1 for tri in triangles))

    def test_uncompensated_section_stls_have_analytical_volume_and_fixed_height(self):
        with tempfile.TemporaryDirectory() as folder:
            fixtures = tools.fixtures(Path(folder))
            for name, area in dict(tools.OPEN_SLOT_AREAS, **{"nested-island": 88.}).items():
                with self.subTest(name=name):
                    vertices = [tuple(map(float, line.split()[1:])) for line in fixtures[name].read_text().splitlines()
                                if line.startswith("vertex ")]
                    volume = 0
                    for index in range(0, len(vertices), 3):
                        a, b, c = vertices[index:index + 3]
                        volume += (a[0] * (b[1] * c[2] - b[2] * c[1])
                                   + a[1] * (b[2] * c[0] - b[0] * c[2])
                                   + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
                    self.assertAlmostEqual(volume, area * 2.4)
                    self.assertEqual(min(p[2] for p in vertices), 0)
                    self.assertEqual(max(p[2] for p in vertices), 2.4)

    def test_3mf_preserves_one_assembly_and_three_separate_source_meshes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = tools.fixtures(Path(folder))["source-volume-sampling"]
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(set(archive.namelist()), {"[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model"})
                model = ET.fromstring(archive.read("3D/3dmodel.model"))
            ns = {"m": tools.MODEL_NS}
            objects = model.findall("m:resources/m:object", ns)
            self.assertEqual(len(objects), 4)
            self.assertEqual(model.get("unit"), "millimeter")
            self.assertEqual([item.get("objectid") for item in model.findall("m:build/m:item", ns)], ["4"])
            self.assertEqual([item.get("objectid") for item in objects[3].findall("m:components/m:component", ns)],
                             ["1", "2", "3"])
            expected = ((0, 4, 20), (1.21, 1.26, 0), (1.28, 1.32, 1))
            for obj, (low, high, planes) in zip(objects, expected):
                vertices = [tuple(float(vertex.get(axis)) for axis in "xyz")
                            for vertex in obj.findall("m:mesh/m:vertices/m:vertex", ns)]
                self.assertEqual(len(vertices), 8)
                self.assertEqual(min(v[2] for v in vertices), low)
                self.assertEqual(max(v[2] for v in vertices), high)
                self.assertEqual(sum(low < 0.1 + index * 0.2 < high for index in range(20)), planes)
                triangles = [tuple(int(face.get("v" + str(i))) for i in (1, 2, 3))
                             for face in obj.findall("m:mesh/m:triangles/m:triangle", ns)]
                self.assertEqual(len(triangles), 12)
                self.assertTrue(all(0 <= vertex < len(vertices) for face in triangles for vertex in face))

    def test_3mf_archive_identity_is_deterministic(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second = Path(folder) / "first.3mf", Path(folder) / "second.3mf"
            volumes = [("cube", tools.cell_mesh([0, 1], [0, 1], [0, 1], {(0, 0, 0)}))]
            tools.write_volume_3mf(first, volumes)
            tools.write_volume_3mf(second, volumes)
            self.assertEqual(first.read_bytes(), second.read_bytes())


def add_external_bridge_projection(group, runs=0, maximum=None, two_sided=0, two_sided_maximum=None):
    layers = []
    model_layers = group["cross_layer_geometry"]["layers"]
    for index, model in enumerate(model_layers):
        layers.append({"layer_index": index, "previous_layer_index": index - 1 if index else None,
                       "print_z_mm": model["print_z_mm"],
                       "previous_print_z_mm": model_layers[index - 1]["print_z_mm"] if index else None,
                       "complete": bool(index), "uncovered_run_count": runs if index else None,
                       "max_uncovered_run_mm": maximum if index else None,
                       "two_sided_run_count": two_sided if index else None,
                       "max_two_sided_run_mm": two_sided_maximum if index else None})
    group["external_bridge_projection"] = {
        "scope": "external_bridge_centerlines_over_previous_model_footprint_before_gcode", "layers": layers}


def add_support_projection(group, support_z=None):
    layers = []
    for index, model in enumerate(group["cross_layer_geometry"]["layers"]):
        target = model["section_area_mm2"] if index == 0 else model["outside_previous_model_area_mm2"]
        levels = []
        if target and support_z is not None and support_z <= model["print_z_mm"]:
            levels = [{"support_print_z_mm": support_z, "model_layer_bottom_gap_mm": model["bottom_z_mm"] - support_z,
                       "nearest_projection_area_mm2": target / 2,
                       "intervening_model_projection": {"complete": support_z < model["print_z_mm"],
                                                        "area_mm2": 0 if support_z < model["print_z_mm"] else None}}]
        layers.append({"layer_index": index, "complete": True, "model_bottom_z_mm": model["bottom_z_mm"],
                       "model_print_z_mm": model["print_z_mm"], "target_area_mm2": target,
                       "unmatched_target_area_mm2": target / 2 if levels else target, "support_levels": levels})
    group["instance_count"] = 1
    group["planned_support_projection"] = {"scope": "per_instance_plate_support_projection_before_gcode",
                                           "instances": [{"instance_index": 0, "layers": layers}]}


def support_report(name="bridge", support=False):
    overlap, outside, count, area = {
        "bridge": (48, 48, 0, 0), "cantilever": (24, 48, 0, 0), "new-island": (24, 24, 1, 24),
    }[name]
    layers = [
        {"layer_index": 0, "slice_z_mm": 3.9, "print_z_mm": 4., "bottom_z_mm": 3.8, "height_mm": .2,
         "complete": False, "section_area_mm2": overlap,
         **{key: None for key in ("previous_layer_index", "previous_slice_z_mm", "previous_print_z_mm",
                                  "previous_model_gap_mm", "previous_model_overlap_area_mm2",
                                  "outside_previous_model_area_mm2", "no_previous_overlap_island_count",
                                  "no_previous_overlap_island_area_mm2")}},
        {"layer_index": 1, "slice_z_mm": 4.1, "print_z_mm": 4.2, "bottom_z_mm": 4., "height_mm": .2,
         "complete": True, "section_area_mm2": overlap + outside, "previous_layer_index": 0,
         "previous_slice_z_mm": 3.9, "previous_print_z_mm": 4., "previous_model_gap_mm": 0.,
         "previous_model_overlap_area_mm2": overlap, "outside_previous_model_area_mm2": outside,
         "no_previous_overlap_island_count": count, "no_previous_overlap_island_area_mm2": area},
    ]
    bridge = name == "bridge" and not support
    planned_roles = [{"role": role, "path_count": 1 if role == "bridge_infill" and bridge else 0,
                      "path_length_mm": 8 if role == "bridge_infill" and bridge else 0,
                      "layer_footprint_area_sum_mm2": 4 if role == "bridge_infill" and bridge else 0,
                      "invalid_path_count": 0, "excluded_path_count": 0} for role in sorted(tools.BRIDGE_ROLES)]
    roles = ([{"role": "bridge_infill", "extrusion_ranges": {"path_length_mm": 8}}] if bridge else [])
    if support:
        roles.append({"role": "support_material_interface", "extrusion_ranges": {"path_length_mm": 10}})
    report = {"objects": {"groups": [{"model_layer_count": 2, "support_layer_count": 1 if support else 0,
                                    "cross_layer_geometry": {"scope": "per_instance_adjacent_model_sections", "layers": layers},
                                    "planned_bridge_paths": {"scope": "per_instance_model_paths_before_gcode",
                                                             "complete": True, "roles": planned_roles}}]},
            "gcode": {"extrusion_by_role": roles,
                      "extruders": [{"estimated_volume_mm3": {"support": 2 if support else None}}]}}
    add_support_projection(report["objects"]["groups"][0], 3.8 if support else None)
    add_external_bridge_projection(report["objects"]["groups"][0], 1, 8, 1 if name == "bridge" else 0,
                                   8 if name == "bridge" else None)
    return report


class CrossLayerChecksTest(unittest.TestCase):
    def test_first_layer_unknown_and_normal_area_accounting(self):
        group = support_report()["objects"]["groups"][0]
        self.assertEqual(len(tools.verify_cross_layer_metrics(group)), 2)

    def test_first_layer_zero_is_not_a_known_previous_overlap(self):
        group = support_report()["objects"]["groups"][0]
        group["cross_layer_geometry"]["layers"][0]["previous_model_overlap_area_mm2"] = 0
        with self.assertRaises(AssertionError):
            tools.verify_cross_layer_metrics(group)

    def test_missing_layer_or_wrong_area_is_rejected(self):
        for change in ("count", "area"):
            with self.subTest(change=change):
                group = support_report()["objects"]["groups"][0]
                if change == "count":
                    group["model_layer_count"] = 3
                else:
                    group["cross_layer_geometry"]["layers"][1]["outside_previous_model_area_mm2"] = 0
                with self.assertRaises(AssertionError):
                    tools.verify_cross_layer_metrics(group)

    def test_invalid_or_unknown_planned_bridge_data_is_rejected(self):
        for change in ("complete", "invalid", "unknown"):
            with self.subTest(change=change):
                group = support_report()["objects"]["groups"][0]
                planned = group["planned_bridge_paths"]
                if change == "complete":
                    planned["complete"] = False
                elif change == "invalid":
                    planned["roles"][0]["invalid_path_count"] = 1
                else:
                    planned["roles"][0]["path_length_mm"] = None
                with self.assertRaises(AssertionError):
                    tools.verify_cross_layer_metrics(group)

    def test_bridge_cantilever_and_whole_new_island_are_distinct(self):
        for name in tools.SUPPORT_FIXTURES:
            with self.subTest(name=name):
                report = support_report(name)
                tools.verify_cross_layer_metrics(report["objects"]["groups"][0])
                tools.verify_support_fixture(name, {"classic": report}, False)

    def test_partial_overlap_does_not_count_as_a_whole_new_island(self):
        report = support_report("cantilever")
        report["objects"]["groups"][0]["cross_layer_geometry"]["layers"][1]["no_previous_overlap_island_count"] = 1
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("cantilever", {"classic": report}, False)

    def test_support_paths_and_volume_required_but_bridge_may_disappear(self):
        report = support_report(support=True)
        tools.verify_support_fixture("bridge", {"classic": report}, True)
        for missing in ("path", "volume"):
            with self.subTest(missing=missing):
                changed = copy.deepcopy(report)
                if missing == "path":
                    changed["gcode"]["extrusion_by_role"] = []
                else:
                    changed["gcode"]["extruders"][0]["estimated_volume_mm3"]["support"] = None
                with self.assertRaises(AssertionError):
                    tools.verify_support_fixture("bridge", {"classic": changed}, True)

    def test_bridge_must_reach_final_gcode(self):
        report = support_report()
        report["gcode"]["extrusion_by_role"] = []
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("bridge", {"classic": report}, False)

    def test_support_does_not_clear_model_only_projection(self):
        off = {generator: support_report("new-island") for generator in ("classic", "arachne")}
        on = {generator: support_report("new-island", True) for generator in ("classic", "arachne")}
        tools.verify_support_geometry_unchanged(off, on)
        on["classic"]["objects"]["groups"][0]["cross_layer_geometry"]["layers"][1]["outside_previous_model_area_mm2"] = 0
        with self.assertRaises(AssertionError):
            tools.verify_support_geometry_unchanged(off, on)


class SupportProjectionChecksTest(unittest.TestCase):
    def test_first_layer_and_no_support_have_known_targets_not_unknown_previous_model(self):
        group = support_report()["objects"]["groups"][0]
        layers = tools.verify_support_projection(group)[0]["layers"]
        self.assertFalse(group["cross_layer_geometry"]["layers"][0]["complete"])
        self.assertTrue(layers[0]["complete"])
        self.assertTrue(all(layer["target_area_mm2"] > 0 and layer["support_levels"] == []
                            and layer["target_area_mm2"] == layer["unmatched_target_area_mm2"] for layer in layers))
        group["cross_layer_geometry"]["layers"][1]["outside_previous_model_area_mm2"] = 0
        add_support_projection(group)
        self.assertEqual(tools.verify_support_projection(group)[0]["layers"][1]["target_area_mm2"], 0)

    def test_same_print_plane_support_keeps_negative_nominal_gap(self):
        group = support_report(support=True)["objects"]["groups"][0]
        level = group["planned_support_projection"]["instances"][0]["layers"][1]["support_levels"][0]
        level.update(support_print_z_mm=4.2, model_layer_bottom_gap_mm=-0.2,
                     intervening_model_projection={"complete": False, "area_mm2": None})
        tools.verify_support_projection(group)
        level.update(support_print_z_mm=4.3, model_layer_bottom_gap_mm=-0.3)
        with self.assertRaises(AssertionError):
            tools.verify_support_projection(group)

    def test_partitioned_levels_conserve_area_and_need_not_have_one_gap(self):
        report = support_report(support=True)
        group = report["objects"]["groups"][0]
        layer = group["planned_support_projection"]["instances"][0]["layers"][1]
        layer["support_levels"][0]["nearest_projection_area_mm2"] -= 2
        layer["support_levels"].append({"support_print_z_mm": 3.6, "model_layer_bottom_gap_mm": 0.4,
                                        "nearest_projection_area_mm2": 2,
                                        "intervening_model_projection": {"complete": True, "area_mm2": 0}})
        tools.verify_support_projection(group)
        tools.verify_support_fixture("bridge", {"classic": report}, True)
        layer["support_levels"][1]["nearest_projection_area_mm2"] = 3
        with self.assertRaises(AssertionError):
            tools.verify_support_projection(group)

    def test_incomplete_keeps_all_three_derived_fields_unknown(self):
        group = support_report()["objects"]["groups"][0]
        layer = group["planned_support_projection"]["instances"][0]["layers"][0]
        layer.update(complete=False, target_area_mm2=None, unmatched_target_area_mm2=None, support_levels=None)
        tools.verify_support_projection(group)
        for key, invalid in (("target_area_mm2", 0), ("unmatched_target_area_mm2", 0), ("support_levels", [])):
            with self.subTest(key=key):
                layer[key] = invalid
                with self.assertRaises(AssertionError):
                    tools.verify_support_projection(group)
                layer[key] = None
        layer["model_bottom_z_mm"] = None
        group["cross_layer_geometry"]["layers"][0]["bottom_z_mm"] = None
        tools.verify_support_projection(group)

    def test_instance_and_model_layer_identity_are_required(self):
        group = support_report()["objects"]["groups"][0]
        instances = group["planned_support_projection"]["instances"]
        instances.append(copy.deepcopy(instances[0]))
        instances[1]["instance_index"] = 1
        group["instance_count"] = 2
        self.assertEqual(len(tools.verify_support_projection(group)), 2)
        for change in ("instance_count", "instance_index", "layer_count", "layer_index", "model_z"):
            with self.subTest(change=change):
                changed = copy.deepcopy(group)
                second = changed["planned_support_projection"]["instances"][1]
                if change == "instance_count":
                    changed["instance_count"] = 1
                elif change == "instance_index":
                    second["instance_index"] = 0
                elif change == "layer_count":
                    second["layers"].pop()
                elif change == "layer_index":
                    second["layers"][0]["layer_index"] = True
                else:
                    second["layers"][0]["model_print_z_mm"] = 20
                with self.assertRaises(AssertionError):
                    tools.verify_support_projection(changed)

    def test_invalid_levels_zero_area_wrong_gap_and_duplicate_z_are_rejected(self):
        for change in ("gap", "nan", "negative", "zero", "duplicate", "ascending", "target", "missing"):
            with self.subTest(change=change):
                group = support_report(support=True)["objects"]["groups"][0]
                layer = group["planned_support_projection"]["instances"][0]["layers"][1]
                level = layer["support_levels"][0]
                if change == "gap":
                    level["model_layer_bottom_gap_mm"] = 0
                elif change == "nan":
                    level["model_layer_bottom_gap_mm"] = float("nan")
                elif change == "negative":
                    level["nearest_projection_area_mm2"] = -1
                elif change == "zero":
                    level["nearest_projection_area_mm2"] = 0
                    layer["unmatched_target_area_mm2"] = layer["target_area_mm2"]
                elif change in ("duplicate", "ascending"):
                    level["nearest_projection_area_mm2"] /= 2
                    other = dict(level)
                    if change == "ascending":
                        other.update(support_print_z_mm=4., model_layer_bottom_gap_mm=0.)
                    layer["support_levels"].append(other)
                elif change == "target":
                    layer["target_area_mm2"] *= 2
                    layer["unmatched_target_area_mm2"] += layer["target_area_mm2"] / 2
                else:
                    layer["support_levels"] = None
                with self.assertRaises(AssertionError):
                    tools.verify_support_projection(group)

    def test_transition_requires_actual_projected_support_not_just_role_volume(self):
        report = support_report(support=True)
        layer = report["objects"]["groups"][0]["planned_support_projection"]["instances"][0]["layers"][1]
        layer.update(support_levels=[], unmatched_target_area_mm2=layer["target_area_mm2"])
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("bridge", {"classic": report}, True)

    def test_tree_settings_are_explicit_without_changing_existing_normal_support(self):
        original = {"layer_height": "0.2", "wall_loops": "2", "enable_support": "0", "raft_mode": "legacy"}
        tree = tools.fixture_process_settings("new-island-tree", "arachne", original)
        self.assertEqual(original["enable_support"], "0")
        self.assertEqual(tree, dict(original, wall_generator="arachne", enable_support="1", support_type="tree(auto)",
                                    support_style="tree_slim", support_on_build_plate_only="1", bridge_no_support="0", raft_layers="0"))
        normal = tools.fixture_process_settings("new-island", "arachne", original, True)
        self.assertEqual(normal["support_type"], "normal(auto)")
        self.assertEqual(normal["support_style"], "default")


class InterveningModelProjectionChecksTest(unittest.TestCase):
    def setUp(self):
        self.group = support_report(support=True)["objects"]["groups"][0]
        self.layer = self.group["planned_support_projection"]["instances"][0]["layers"][1]
        self.level = self.layer["support_levels"][0]

    def test_zero_partial_and_full_overlap_have_no_quality_threshold(self):
        for area in (0, 0.000001, 8, self.level["nearest_projection_area_mm2"]):
            with self.subTest(area=area):
                self.level["intervening_model_projection"] = {"complete": True, "area_mm2": area}
                tools.verify_support_projection(self.group)

    def test_unknown_intervening_paths_do_not_invalidate_old_support_allocation(self):
        original = {key: value for key, value in self.level.items() if key != "intervening_model_projection"}
        self.level["intervening_model_projection"] = {"complete": False, "area_mm2": None}
        tools.verify_support_projection(self.group)
        self.assertTrue(self.layer["complete"])
        self.assertEqual(original, {key: value for key, value in self.level.items() if key != "intervening_model_projection"})
        self.assertEqual(self.layer["unmatched_target_area_mm2"], self.layer["target_area_mm2"] / 2)

    def test_invalid_completeness_unknown_zero_and_invalid_areas_are_rejected(self):
        invalid = [None, [], {"complete": 1, "area_mm2": 0}, {"complete": None, "area_mm2": None},
                   {"complete": False, "area_mm2": 0}, {"complete": False, "area_mm2": 1}]
        invalid.extend({"complete": True, "area_mm2": area}
                       for area in (None, True, -1, float("nan"), float("inf"),
                                    self.level["nearest_projection_area_mm2"] + 1))
        for observation in invalid:
            with self.subTest(observation=observation):
                self.level["intervening_model_projection"] = observation
                with self.assertRaises(AssertionError):
                    tools.verify_support_projection(self.group)

    def test_same_z_is_unknown_but_strict_positive_print_plane_interval_is_valid(self):
        self.level.update(support_print_z_mm=4.2, model_layer_bottom_gap_mm=-0.2,
                          intervening_model_projection={"complete": False, "area_mm2": None})
        tools.verify_support_projection(self.group)
        self.level["intervening_model_projection"] = {"complete": True, "area_mm2": 0}
        with self.assertRaises(AssertionError):
            tools.verify_support_projection(self.group)
        # This remains a positive interval despite a negative nominal bottom gap.
        self.level.update(support_print_z_mm=4.2 - 1e-9, model_layer_bottom_gap_mm=4. - (4.2 - 1e-9))
        tools.verify_support_projection(self.group)

    def test_area_is_bounded_by_its_support_level_not_the_entire_target(self):
        self.level["nearest_projection_area_mm2"] -= 2
        self.layer["support_levels"].append({"support_print_z_mm": 3.6, "model_layer_bottom_gap_mm": 0.4,
                                            "nearest_projection_area_mm2": 2,
                                            "intervening_model_projection": {"complete": True, "area_mm2": 2}})
        tools.verify_support_projection(self.group)
        self.layer["support_levels"][1]["intervening_model_projection"]["area_mm2"] = 3
        with self.assertRaises(AssertionError):
            tools.verify_support_projection(self.group)

    def test_instance_observations_are_independent(self):
        instances = self.group["planned_support_projection"]["instances"]
        instances.append(copy.deepcopy(instances[0]))
        instances[1]["instance_index"] = 1
        self.group["instance_count"] = 2
        other = instances[1]["layers"][1]["support_levels"][0]["intervening_model_projection"]
        other["area_mm2"] = 8
        tools.verify_support_projection(self.group)
        self.assertEqual(self.level["intervening_model_projection"]["area_mm2"], 0)
        other["area_mm2"] = -1
        with self.assertRaises(AssertionError):
            tools.verify_support_projection(self.group)

    def test_old_missing_data_is_not_replaced_by_a_clear_projection(self):
        for key in ("intervening_model_projection", "complete", "area_mm2"):
            with self.subTest(key=key):
                group = copy.deepcopy(self.group)
                level = group["planned_support_projection"]["instances"][0]["layers"][1]["support_levels"][0]
                del (level if key == "intervening_model_projection" else level["intervening_model_projection"])[key]
                with self.assertRaises(KeyError):
                    tools.verify_support_projection(group)


class JointLowerPathSlabCoverageChecksTest(unittest.TestCase):
    derived = ("uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm")

    def fixture(self):
        layers = [{"layer_index": index, "print_z_mm": top, "complete": True,
                   "bottom_planes": [{"bottom_z_mm": top - .2, "complete": True,
                                      "target_area_mm2": 16, "uncovered_area_mm2": 0,
                                      "external_bridge": {"complete": True, "uncovered_run_count": 0,
                                                          "max_uncovered_run_mm": None, "two_sided_run_count": 0,
                                                          "max_two_sided_run_mm": None}}]}
                  for index, top in enumerate((.2, .4))]
        return {"instance_count": 1, "model_layer_count": 2,
                "cross_layer_geometry": {"layers": [{"layer_index": index, "print_z_mm": top}
                                                     for index, top in enumerate((.2, .4))]},
                "joint_lower_path_slab_coverage": {
                    "scope": "per_instance_lower_source_print_z_nominal_path_slabs_before_gcode",
                    "z_resolution_mm": .000001, "instances": [{"instance_index": 0, "layers": layers}]}}

    def layers(self, group):
        return group["joint_lower_path_slab_coverage"]["instances"][0]["layers"]

    def test_first_layer_signed_bottom_and_multiple_instances_are_observations_not_support_verdicts(self):
        group = self.fixture()
        plane = self.layers(group)[0]["bottom_planes"][0]
        plane.update(bottom_z_mm=-.1, uncovered_area_mm2=4)
        plane["external_bridge"].update(uncovered_run_count=3, max_uncovered_run_mm=1000,
                                        two_sided_run_count=2, max_two_sided_run_mm=900)
        instances = group["joint_lower_path_slab_coverage"]["instances"]
        second = copy.deepcopy(instances[0])
        second["instance_index"] = 1
        instances.append(second)
        group["instance_count"] = 2
        before = copy.deepcopy(group)
        self.assertEqual(len(tools.verify_joint_lower_path_slab_coverage(group)), 2)
        self.assertEqual(group, before)

    def test_no_paths_and_empty_targets_preserve_complete_zero_without_bridge_maxima(self):
        group = self.fixture()
        first, later = self.layers(group)
        first["bottom_planes"] = []
        later["bottom_planes"][0].update(target_area_mm2=0, uncovered_area_mm2=0)
        tools.verify_joint_lower_path_slab_coverage(group)
        first["complete"] = False
        with self.assertRaises(AssertionError):
            tools.verify_joint_lower_path_slab_coverage(group)
        group["instance_count"] = 0
        group["joint_lower_path_slab_coverage"]["instances"] = []
        tools.verify_joint_lower_path_slab_coverage(group)

    def test_unknown_target_heights_preserve_null_planes_and_invalid_z_is_not_zero(self):
        group = self.fixture()
        layer = self.layers(group)[0]
        layer.update(complete=False, bottom_planes=None)
        tools.verify_joint_lower_path_slab_coverage(group)
        layer["print_z_mm"] = group["cross_layer_geometry"]["layers"][0]["print_z_mm"] = None
        tools.verify_joint_lower_path_slab_coverage(group)
        for change in ({"complete": True}, {"bottom_planes": []}, {"print_z_mm": 0}):
            with self.subTest(change=change):
                changed = copy.deepcopy(group)
                self.layers(changed)[0].update(change)
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(changed)

    def test_one_unknown_plane_does_not_hide_other_planes_and_bridge_unknown_is_independent(self):
        group = self.fixture()
        layer = self.layers(group)[0]
        known = layer["bottom_planes"][0]
        known["external_bridge"] = {"complete": False, **dict.fromkeys(self.derived)}
        tools.verify_joint_lower_path_slab_coverage(group)  # Known areas keep the layer complete.
        unknown = copy.deepcopy(known)
        unknown.update(bottom_z_mm=.1, complete=False, target_area_mm2=None, uncovered_area_mm2=None)
        layer["bottom_planes"].append(unknown)
        layer["complete"] = False
        tools.verify_joint_lower_path_slab_coverage(group)
        for key in self.derived:
            with self.subTest(key=key):
                unknown["external_bridge"][key] = 0
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(group)
                unknown["external_bridge"][key] = None
        for change in ({"target_area_mm2": 0}, {"uncovered_area_mm2": 0}, {"complete": True}):
            with self.subTest(change=change):
                changed = copy.deepcopy(group)
                self.layers(changed)[0]["bottom_planes"][1].update(change)
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(changed)

    def test_area_bounds_are_strict_and_finite_without_a_quality_tolerance(self):
        for key, value in (("target_area_mm2", -1), ("target_area_mm2", True), ("target_area_mm2", None),
                           ("target_area_mm2", math.nan), ("target_area_mm2", math.inf),
                           ("uncovered_area_mm2", -1), ("uncovered_area_mm2", True),
                           ("uncovered_area_mm2", math.inf), ("uncovered_area_mm2", None),
                           ("uncovered_area_mm2", math.nextafter(16., math.inf))):
            with self.subTest(key=key, value=value):
                group = self.fixture()
                self.layers(group)[0]["bottom_planes"][0][key] = value
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(group)

    def test_instances_layers_print_planes_and_quantized_bottom_identity_are_checked(self):
        for change in ("instance_count", "instance_index", "model_count", "missing_layer", "layer_index", "reference_index",
                       "print_z", "nonfinite_z", "bottom_above", "bottom_nan", "bottom_bool", "duplicate_quantized_bottom",
                       "descending_bottom", "layer_bool", "plane_bool", "scope", "resolution"):
            with self.subTest(change=change):
                group = self.fixture()
                observation = group["joint_lower_path_slab_coverage"]
                layer = self.layers(group)[0]
                plane = layer["bottom_planes"][0]
                if change == "instance_count": group["instance_count"] = 2
                elif change == "instance_index": observation["instances"][0]["instance_index"] = True
                elif change == "model_count": group["model_layer_count"] = 3
                elif change == "missing_layer": self.layers(group).pop()
                elif change == "layer_index": layer["layer_index"] = 1
                elif change == "reference_index": group["cross_layer_geometry"]["layers"][0]["layer_index"] = 1
                elif change == "print_z": layer["print_z_mm"] = .21
                elif change == "nonfinite_z": layer["print_z_mm"] = math.nan
                elif change == "bottom_above": plane["bottom_z_mm"] = math.nextafter(.2, math.inf)
                elif change == "bottom_nan": plane["bottom_z_mm"] = math.nan
                elif change == "bottom_bool": plane["bottom_z_mm"] = False
                elif change in ("duplicate_quantized_bottom", "descending_bottom"):
                    extra = copy.deepcopy(plane)
                    extra["bottom_z_mm"] = .0000001 if change == "duplicate_quantized_bottom" else -.1
                    layer["bottom_planes"].append(extra)
                elif change == "layer_bool": layer["complete"] = 1
                elif change == "plane_bool": plane["complete"] = 1
                elif change == "scope": observation["scope"] = "physical_support"
                else: observation["z_resolution_mm"] = .01
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(group)

    def test_bridge_count_maximum_pairs_and_two_sided_subset_are_strict(self):
        for runs, maximum, two, two_maximum in ((0, 0, 0, None), (1, None, 0, None), (1, 0, 0, None),
                                              (1, math.nan, 0, None), (1, math.inf, 0, None),
                                              (True, 1, 0, None), (-1, 1, 0, None), (.5, 1, 0, None),
                                              (1, 8, 2, 8), (1, 8, 1, math.nextafter(8., math.inf)),
                                              (1, 8, 0, 0), (1, 8, 1, None), (1, 8, 1, -1)):
            with self.subTest(runs=runs, maximum=maximum, two=two, two_maximum=two_maximum):
                group = self.fixture()
                bridge = self.layers(group)[0]["bottom_planes"][0]["external_bridge"]
                bridge.update(uncovered_run_count=runs, max_uncovered_run_mm=maximum,
                              two_sided_run_count=two, max_two_sided_run_mm=two_maximum)
                with self.assertRaises(AssertionError):
                    tools.verify_joint_lower_path_slab_coverage(group)

    def test_missing_observation_or_plane_fields_are_not_inferred_from_legacy_reports(self):
        for path in (("joint_lower_path_slab_coverage",),
                     ("joint_lower_path_slab_coverage", "z_resolution_mm"),
                     ("joint_lower_path_slab_coverage", "instances", 0, "layers", 0, "bottom_planes"),
                     *(("joint_lower_path_slab_coverage", "instances", 0, "layers", 0, "bottom_planes", 0, key)
                       for key in ("bottom_z_mm", "complete", "target_area_mm2", "uncovered_area_mm2", "external_bridge"))):
            with self.subTest(path=path):
                group = self.fixture()
                target = group
                for key in path[:-1]:
                    target = target[key]
                del target[path[-1]]
                with self.assertRaises(KeyError):
                    tools.verify_joint_lower_path_slab_coverage(group)

    def test_real_feature_validation_entry_checks_new_block_before_accepting_old_observations(self):
        group = self.fixture()
        self.layers(group)[0]["bottom_planes"][0]["uncovered_area_mm2"] = 17
        with self.assertRaisesRegex(AssertionError, "Joint slab uncovered area exceeds"):
            tools.verify_feature_metrics("bridge", {"classic": {"objects": {"groups": [group]}}})
        del group["joint_lower_path_slab_coverage"]
        with self.assertRaisesRegex(KeyError, "joint_lower_path_slab_coverage"):
            tools.verify_feature_metrics("bridge", {"classic": {"objects": {"groups": [group]}}})


class ExternalBridgeProjectionChecksTest(unittest.TestCase):
    def test_first_layer_is_not_applicable_and_valid_zero_has_no_maximum(self):
        group = support_report()["objects"]["groups"][0]
        add_external_bridge_projection(group)
        first, later = tools.verify_external_bridge_projection(group)
        self.assertFalse(first["complete"])
        self.assertIsNone(first["previous_layer_index"])
        self.assertIsNone(first["uncovered_run_count"])
        self.assertTrue(later["complete"])
        self.assertEqual(later["uncovered_run_count"], 0)
        self.assertIsNone(later["max_uncovered_run_mm"])

    def test_positive_runs_and_two_sided_subset_have_no_quality_threshold(self):
        group = support_report()["objects"]["groups"][0]
        for runs, longest, two_sided, two_longest in ((1, .000001, 0, None), (3, 1000., 2, 999.), (1, 8., 1, 8.)):
            with self.subTest(runs=runs, longest=longest):
                add_external_bridge_projection(group, runs, longest, two_sided, two_longest)
                tools.verify_external_bridge_projection(group)

    def test_unavailable_or_closed_path_observations_keep_all_derived_fields_null(self):
        group = support_report()["objects"]["groups"][0]
        layer = group["external_bridge_projection"]["layers"][1]
        derived = ("uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm")
        layer.update(complete=False, **dict.fromkeys(derived))
        tools.verify_external_bridge_projection(group)
        for key in derived:
            with self.subTest(key=key):
                layer[key] = 0
                with self.assertRaises(AssertionError):
                    tools.verify_external_bridge_projection(group)
                layer[key] = None
        layer["print_z_mm"] = None
        group["cross_layer_geometry"]["layers"][1]["print_z_mm"] = None
        tools.verify_external_bridge_projection(group)

    def test_first_layer_cannot_claim_complete_zero_even_if_raft_support_exists(self):
        group = raft_report()["objects"]["groups"][0]
        self.assertGreater(group["support_layer_count"], 0)
        tools.verify_external_bridge_projection(group)
        group["external_bridge_projection"]["layers"][0].update(
            complete=True, uncovered_run_count=0, max_uncovered_run_mm=None, two_sided_run_count=0, max_two_sided_run_mm=None)
        with self.assertRaises(AssertionError):
            tools.verify_external_bridge_projection(group)

    def test_invalid_count_maximum_pairs_are_rejected(self):
        for runs, longest, two, two_longest in ((0, 0, 0, None), (1, None, 0, None), (1, 0, 0, None),
                                              (1, -1, 0, None), (1, float("nan"), 0, None),
                                              (1, float("inf"), 0, None), (1, True, 0, None),
                                              (None, None, 0, None), (True, 1, 0, None), (.5, 1, 0, None),
                                              (-1, 1, 0, None), (1, 8, 2, 8), (1, 8, 1, 9),
                                              (1, 8, 0, 0), (1, 8, 1, None), (1, 8, 1, 0)):
            with self.subTest(runs=runs, longest=longest, two=two, two_longest=two_longest):
                group = support_report()["objects"]["groups"][0]
                add_external_bridge_projection(group, runs, longest, two, two_longest)
                with self.assertRaises(AssertionError):
                    tools.verify_external_bridge_projection(group)

    def test_model_layer_identity_and_increasing_z_are_required(self):
        for change in ("count", "index", "previous_index", "first_previous", "z", "previous_z", "nonfinite", "same_z"):
            with self.subTest(change=change):
                group = support_report()["objects"]["groups"][0]
                first, layer = group["external_bridge_projection"]["layers"]
                if change == "count":
                    group["model_layer_count"] = 3
                elif change == "index":
                    layer["layer_index"] = True
                elif change == "previous_index":
                    layer["previous_layer_index"] = 1
                elif change == "first_previous":
                    first["previous_layer_index"] = 0
                elif change == "z":
                    layer["print_z_mm"] = 9
                elif change == "previous_z":
                    layer["previous_print_z_mm"] = 9
                elif change == "nonfinite":
                    layer["print_z_mm"] = float("nan")
                else:
                    layer["print_z_mm"] = 4.
                    group["cross_layer_geometry"]["layers"][1]["print_z_mm"] = 4.
                with self.assertRaises(AssertionError):
                    tools.verify_external_bridge_projection(group)

    def test_old_missing_data_is_not_silently_replaced_by_zero(self):
        group = support_report()["objects"]["groups"][0]
        for field in ("uncovered_run_count", "max_uncovered_run_mm", "two_sided_run_count", "max_two_sided_run_mm"):
            with self.subTest(field=field):
                changed = copy.deepcopy(group)
                del changed["external_bridge_projection"]["layers"][1][field]
                with self.assertRaises(KeyError):
                    tools.verify_external_bridge_projection(changed)
        del group["external_bridge_projection"]
        with self.assertRaises(KeyError):
            tools.verify_external_bridge_projection(group)

    def test_support_does_not_convert_new_island_into_two_sided_model_projection(self):
        report = support_report("new-island", True)
        tools.verify_support_fixture("new-island", {"classic": report}, True)
        report["objects"]["groups"][0]["external_bridge_projection"]["layers"][1].update(
            two_sided_run_count=1, max_two_sided_run_mm=8)
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("new-island", {"classic": report}, True)


def raft_report():
    report = support_report(support=True)
    group = report["objects"]["groups"][0]
    first, later = group["cross_layer_geometry"]["layers"]
    layers = []
    for index in range(20):
        layer = copy.deepcopy(first if index == 0 else later)
        top = 1.2 + index * 0.2 - (0.1 if index else 0)
        layer.update(layer_index=index, slice_z_mm=0.1 + index * 0.2, print_z_mm=top,
                     bottom_z_mm=top - 0.2, section_area_mm2=96)
        if index:
            previous = layers[-1]
            layer.update(previous_layer_index=index - 1, previous_slice_z_mm=previous["slice_z_mm"],
                         previous_print_z_mm=previous["print_z_mm"],
                         previous_model_gap_mm=layer["bottom_z_mm"] - previous["print_z_mm"],
                         previous_model_overlap_area_mm2=96, outside_previous_model_area_mm2=0,
                         no_previous_overlap_island_count=0, no_previous_overlap_island_area_mm2=0)
        layers.append(layer)
    group.update(model_layer_count=20, support_layer_count=3)
    group["cross_layer_geometry"]["layers"] = layers
    add_support_projection(group, 0.8)
    add_external_bridge_projection(group)
    group["source_surface_layer_alignment"] = surface_alignment_group(non_horizontal=160, horizontal=4)[
        "source_surface_layer_alignment"]
    return report


def volume_sampling_report():
    volumes = []
    for index, (low, high, planes) in enumerate(((0, 4, 20), (1.21, 1.26, 0), (1.28, 1.32, 1))):
        volumes.append({"volume_index": index, "z_min_mm": low, "z_max_mm": high, "complete": True,
                        "interior_plane_count": planes, "boundary_plane_count": 0,
                        "source_section_layer_count": planes, "final_section_overlap_layer_count": planes,
                        "planned_path_overlap_layer_count": planes})
    return {"objects": {"groups": [{"model_layer_count": 20,
                                    "source_volume_sampling": {"scope": "per_instance_source_model_part_on_retained_layers",
                                                               "volumes": volumes}}]}}


def surface_alignment_group(non_horizontal=1, normal_offset=0, horizontal=1, boundary_offset=0, complete=True):
    volume = {"volume_index": 0, "complete": complete,
              "non_horizontal_facet_band_count": non_horizontal if complete else None,
              "max_vertical_projection_normal_offset_mm": normal_offset if complete else None,
              "horizontal_facet_count": horizontal if complete else None,
              "max_horizontal_boundary_offset_mm": boundary_offset if complete else None}
    return {"model_layer_count": 20, "source_surface_layer_alignment": {
        "scope": "source_model_part_facets_and_retained_source_layer_bands", "volumes": [volume]}}


class SourceSurfaceAlignmentTest(unittest.TestCase):
    def test_vertical_aligned_and_positive_observations_are_valid(self):
        for offset, boundary in ((0, 0), (1e-12, 0.08), (0.2 / (2 * math.sqrt(2)), 0.1)):
            with self.subTest(offset=offset, boundary=boundary):
                group = surface_alignment_group(normal_offset=offset, boundary_offset=boundary)
                tools.verify_source_surface_layer_alignment(group)

    def test_no_applicable_faces_are_null_not_zero(self):
        for non_horizontal, horizontal in ((0, 1), (1, 0), (0, 0)):
            with self.subTest(non_horizontal=non_horizontal, horizontal=horizontal):
                group = surface_alignment_group(non_horizontal, None if non_horizontal == 0 else 0,
                                                horizontal, None if horizontal == 0 else 0)
                tools.verify_source_surface_layer_alignment(group)
                volume = group["source_surface_layer_alignment"]["volumes"][0]
                for key in ("max_vertical_projection_normal_offset_mm", "max_horizontal_boundary_offset_mm"):
                    if volume[key] is None:
                        changed = copy.deepcopy(group)
                        changed["source_surface_layer_alignment"]["volumes"][0][key] = 0
                        with self.assertRaises(AssertionError):
                            tools.verify_source_surface_layer_alignment(changed)

    def test_incomplete_and_no_layers_preserve_unknown(self):
        group = surface_alignment_group(complete=False)
        tools.verify_source_surface_layer_alignment(group)
        group["model_layer_count"] = 0
        tools.verify_source_surface_layer_alignment(group)
        for key in ("non_horizontal_facet_band_count", "max_vertical_projection_normal_offset_mm",
                    "horizontal_facet_count", "max_horizontal_boundary_offset_mm"):
            changed = copy.deepcopy(group)
            changed["source_surface_layer_alignment"]["volumes"][0][key] = 0
            with self.subTest(key=key), self.assertRaises(AssertionError):
                tools.verify_source_surface_layer_alignment(changed)
        group = surface_alignment_group()
        group["model_layer_count"] = 0
        with self.assertRaises(AssertionError):
            tools.verify_source_surface_layer_alignment(group)

    def test_invalid_counts_offsets_and_completeness_are_rejected(self):
        for key, values in {
            "complete": (None, 0, 1, "true"),
            "non_horizontal_facet_band_count": (None, True, -1, 0.5, "1"),
            "horizontal_facet_count": (None, True, -1, 0.5, "1"),
            "max_vertical_projection_normal_offset_mm": (None, True, -1, float("nan"), float("inf"), "0.1"),
            "max_horizontal_boundary_offset_mm": (None, True, -1, float("nan"), float("inf"), "0.1"),
        }.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    group = surface_alignment_group()
                    group["source_surface_layer_alignment"]["volumes"][0][key] = value
                    with self.assertRaises(AssertionError):
                        tools.verify_source_surface_layer_alignment(group)

    def test_source_volume_identity_order_and_independent_unknown(self):
        group = surface_alignment_group()
        second = surface_alignment_group(complete=False)["source_surface_layer_alignment"]["volumes"][0]
        second["volume_index"] = 2  # Excluded negative/modifier volumes need not leave contiguous IDs.
        group["source_surface_layer_alignment"]["volumes"].append(second)
        tools.verify_source_surface_layer_alignment(group)
        for indexes in ((0, 0), (2, 0), (0, True), (0, -1), (0, 0.5)):
            with self.subTest(indexes=indexes):
                changed = copy.deepcopy(group)
                for volume, index in zip(changed["source_surface_layer_alignment"]["volumes"], indexes):
                    volume["volume_index"] = index
                with self.assertRaises(AssertionError):
                    tools.verify_source_surface_layer_alignment(changed)

    def test_old_missing_and_wrong_scope_are_not_accepted_as_known_zero(self):
        group = surface_alignment_group()
        with self.assertRaises(KeyError):
            tools.verify_source_surface_layer_alignment({"model_layer_count": 20})
        for key in ("volume_index", "complete", "non_horizontal_facet_band_count",
                    "max_vertical_projection_normal_offset_mm", "horizontal_facet_count", "max_horizontal_boundary_offset_mm"):
            changed = copy.deepcopy(group)
            del changed["source_surface_layer_alignment"]["volumes"][0][key]
            with self.subTest(key=key), self.assertRaises(KeyError):
                tools.verify_source_surface_layer_alignment(changed)
        group["source_surface_layer_alignment"]["scope"] = "actual_print_surface_error"
        with self.assertRaises(AssertionError):
            tools.verify_source_surface_layer_alignment(group)

    def test_wedge_asserts_source_truth_not_wall_generator_preference(self):
        group = surface_alignment_group(non_horizontal=120, normal_offset=0.2 / (2 * math.sqrt(2)), horizontal=2)
        layers = [{"slice_z_mm": 0.1 + index * 0.2, "section_area_mm2": 6 * (4 - 0.1 - index * 0.2),
                   "section_island_count": 1, "closed_void_region_count": 0} for index in range(20)]
        group["planned_layer_coverage"] = {"layers": layers}
        tools.verify_wedge_alignment(group)
        # Float slicing planes may move the maximum slightly above the ideal value.
        group["source_surface_layer_alignment"]["volumes"][0]["max_vertical_projection_normal_offset_mm"] += 1e-7
        tools.verify_wedge_alignment(group)
        for change in ("old_contour_shift", "missing_layer", "wrong_area", "wrong_plane", "wrong_source", "wrong_bottom"):
            with self.subTest(change=change):
                changed = copy.deepcopy(group)
                volume = changed["source_surface_layer_alignment"]["volumes"][0]
                if change == "old_contour_shift":
                    volume["max_vertical_projection_normal_offset_mm"] = 0.2
                elif change == "missing_layer":
                    changed["planned_layer_coverage"]["layers"].pop()
                elif change == "wrong_area":
                    changed["planned_layer_coverage"]["layers"][5]["section_area_mm2"] += 1
                elif change == "wrong_plane":
                    changed["planned_layer_coverage"]["layers"][5]["slice_z_mm"] += 0.01
                elif change == "wrong_source":
                    volume["horizontal_facet_count"] = 4
                else:
                    volume["max_horizontal_boundary_offset_mm"] = 0.1
                with self.assertRaises(AssertionError):
                    tools.verify_wedge_alignment(changed)

    def test_wedge_config_is_isolated_and_explicit(self):
        original = {"layer_height": "0.3", "initial_layer_print_height": "0.3", "wall_loops": "2",
                    "adaptive_layer_height": "1", "enable_support": "1", "raft_layers": "3",
                    "xy_contour_compensation": "0.1", "elefant_foot_compensation": "0.15"}
        before = dict(original)
        classic = tools.fixture_process_settings("slope-wedge", "classic", original)
        arachne = tools.fixture_process_settings("slope-wedge", "arachne", original)
        self.assertEqual(original, before)
        self.assertEqual({key for key in classic if classic[key] != arachne[key]}, {"wall_generator"})
        self.assertEqual(classic["layer_height"], "0.2")
        self.assertEqual(classic["initial_layer_print_height"], "0.2")
        self.assertEqual(classic["adaptive_layer_height"], "0")
        self.assertEqual(classic["enable_support"], "0")
        self.assertEqual(classic["raft_layers"], "0")
        self.assertEqual(classic["slicing_mode"], "regular")
        self.assertEqual(classic["xy_contour_compensation"], "0")
        self.assertEqual(classic["elefant_foot_compensation"], "0")
        self.assertEqual(classic["wall_loops"], "2")


def topology_counts(islands=1, voids=0, fragmented=0, spanning=0, filled=0, divided=0, complete=True):
    return {"complete": complete, "section_island_count": islands, "closed_void_region_count": voids,
            **dict(zip(tools.TOPOLOGY_DERIVED_COUNTS, (fragmented, spanning, filled, divided)))}


class TopologyCountsTest(unittest.TestCase):
    def test_complete_counts_and_sparse_fragmentation_are_allowed(self):
        tools.verify_topology_counts(topology_counts())
        tools.verify_topology_counts(topology_counts(fragmented=1))
        tools.verify_topology_counts(topology_counts(islands=2, fragmented=2, spanning=3))
        tools.verify_topology_counts(topology_counts(voids=3, filled=1, divided=2))

    def test_incomplete_coverage_preserves_all_six_counts_as_unknown(self):
        layer = topology_counts(islands=None, voids=None, complete=False)
        layer.update(dict.fromkeys(tools.TOPOLOGY_DERIVED_COUNTS))
        tools.verify_topology_counts(layer)
        for key in ("section_island_count", "closed_void_region_count", *tools.TOPOLOGY_DERIVED_COUNTS):
            with self.subTest(key=key):
                changed = dict(layer, **{key: 0})
                with self.assertRaises(AssertionError):
                    tools.verify_topology_counts(changed)

    def test_counts_cannot_exceed_source_targets(self):
        for layer in (topology_counts(fragmented=2), topology_counts(spanning=1), topology_counts(voids=1, filled=2),
                      topology_counts(voids=1, divided=2), topology_counts(voids=1, filled=1, divided=1)):
            with self.subTest(layer=layer):
                with self.assertRaises(AssertionError):
                    tools.verify_topology_counts(layer)

    def test_missing_noninteger_negative_or_unknown_complete_counts_are_rejected(self):
        for key in ("section_island_count", "closed_void_region_count", *tools.TOPOLOGY_DERIVED_COUNTS):
            for value in (None, True, 0.5, -1):
                with self.subTest(key=key, value=value):
                    layer = topology_counts()
                    layer[key] = value
                    with self.assertRaises(AssertionError):
                        tools.verify_topology_counts(layer)
            layer = topology_counts()
            del layer[key]
            with self.assertRaises(KeyError):
                tools.verify_topology_counts(layer)


class EnclosedExteriorTest(unittest.TestCase):
    def test_zero_positive_and_tiny_positive_areas_are_observations_not_failures(self):
        for regions, area in ((0, 0), (1, 16.), (2, 0.000000001)):
            tools.verify_enclosed_exterior({"complete": True, "planned_enclosed_exterior_region_count": regions,
                                            "planned_enclosed_exterior_area_mm2": area})

    def test_count_and_area_must_agree_without_replacing_unknown_with_zero(self):
        for regions, area, complete in ((0, 1, True), (1, 0, True), (None, 0, False), (0, None, False),
                                        (None, None, True), (True, 1, True), (0.5, 1, True), (-1, 1, True),
                                        (1, -1, True), (1, float("nan"), True), (1, float("inf"), True)):
            with self.subTest(regions=regions, area=area, complete=complete):
                with self.assertRaises(AssertionError):
                    tools.verify_enclosed_exterior({"complete": complete, "planned_enclosed_exterior_region_count": regions,
                                                    "planned_enclosed_exterior_area_mm2": area})
        tools.verify_enclosed_exterior({"complete": False, "planned_enclosed_exterior_region_count": None,
                                       "planned_enclosed_exterior_area_mm2": None})

    def test_open_slot_source_geometry_does_not_predict_planned_enclosure(self):
        for name, area in tools.OPEN_SLOT_AREAS.items():
            layers = [{"slice_z_mm": 0.1 + index * 0.2, "section_area_mm2": area, "section_island_count": 1,
                       "closed_void_region_count": 0, "hole_area_mm2": 0, "complete": True,
                       "planned_enclosed_exterior_region_count": index % 2,
                       "planned_enclosed_exterior_area_mm2": 0 if index % 2 == 0 else 0.001} for index in range(12)]
            tools.verify_open_slot_layers(name, layers)
            for layer in layers:
                tools.verify_enclosed_exterior(layer)
            for changed in (dict(layers[0], section_area_mm2=area - 1), dict(layers[0], closed_void_region_count=1),
                            dict(layers[0], slice_z_mm=0.2)):
                with self.assertRaises(AssertionError):
                    tools.verify_open_slot_layers(name, [changed, *layers[1:]])

    def test_slot_settings_keep_closing_radius_below_openings_and_no_size_compensation(self):
        original = {"elefant_foot_compensation": "0.15", "slice_closing_radius": "0.5", "layer_height": "0.3"}
        before = dict(original)
        for name in (*tools.OPEN_SLOT_AREAS, "nested-island"):
            settings = tools.fixture_process_settings(name, "classic", original)
            self.assertLess(2 * float(settings["slice_closing_radius"]), 0.36)
            self.assertEqual(settings["slicing_mode"], "regular")
            self.assertEqual(settings["layer_height"], "0.2")
            self.assertEqual(settings["initial_layer_print_height"], "0.2")
            for key in ("elefant_foot_compensation", "xy_contour_compensation", "xy_hole_compensation"):
                self.assertEqual(settings[key], "0")
        self.assertEqual(original, before)


class IslandGapTest(unittest.TestCase):
    def test_distinct_islands_allow_zero_and_positive_exact_distances(self):
        for islands, gap in ((2, 0), (2, 0.5), (3, 1.), (2, 0.000001)):
            tools.verify_island_gap({"complete": True, "section_island_count": islands, "section_min_island_gap_mm": gap})

    def test_unavailable_or_inapplicable_gap_stays_null(self):
        for complete, islands in ((False, None), (True, 0), (True, 1)):
            layer = {"complete": complete, "section_island_count": islands, "section_min_island_gap_mm": None}
            tools.verify_island_gap(layer)
            for value in (0, 0.5):
                with self.subTest(complete=complete, islands=islands, value=value):
                    with self.assertRaises(AssertionError):
                        tools.verify_island_gap(dict(layer, section_min_island_gap_mm=value))

    def test_missing_nonfinite_negative_or_nonnumeric_gap_is_rejected(self):
        for value in (None, -1, True, "0.5", float("inf"), float("nan")):
            with self.subTest(value=value):
                with self.assertRaises(AssertionError):
                    tools.verify_island_gap({"complete": True, "section_island_count": 2, "section_min_island_gap_mm": value})
        with self.assertRaises(KeyError):
            tools.verify_island_gap({"complete": True, "section_island_count": 2})

    def test_single_island_slot_and_neck_widths_are_not_inter_island_gaps(self):
        for name, width in (("u-slot-wide", 2.), ("u-slot-narrow", 0.36), ("c-slot", 0.5), ("narrow-neck", 0.36)):
            with self.subTest(name=name):
                layer = {"complete": True, "section_island_count": 1, "section_min_island_gap_mm": None}
                tools.verify_island_gap(layer)
                with self.assertRaises(AssertionError):
                    tools.verify_island_gap(dict(layer, section_min_island_gap_mm=width))

    def test_parallel_and_nested_islands_have_analytical_section_distances(self):
        for name, area, hole_area, voids, gap in (("separated-blocks", 64., 0., 0, 0.5),
                                                 ("nested-island", 88., 12., 1, 1.)):
            layers = [{"slice_z_mm": 0.1 + index * 0.2, "section_area_mm2": area, "hole_area_mm2": hole_area,
                       "section_island_count": 2, "closed_void_region_count": voids, "section_min_island_gap_mm": gap}
                      for index in range(12)]
            tools.verify_island_fixture_layers(name, layers)
            for changed in (dict(layers[0], section_min_island_gap_mm=gap + 0.1),
                            dict(layers[0], section_area_mm2=area - 1), dict(layers[0], hole_area_mm2=hole_area + 1),
                            dict(layers[0], section_island_count=1), dict(layers[0], slice_z_mm=0.2)):
                with self.subTest(name=name, changed=changed):
                    with self.assertRaises(AssertionError):
                        tools.verify_island_fixture_layers(name, [changed, *layers[1:]])

    def test_existing_separated_fixture_keeps_its_original_process(self):
        original = {"elefant_foot_compensation": "0.15", "slice_closing_radius": "0.049", "layer_height": "0.2",
                    "xy_contour_compensation": "0", "xy_hole_compensation": "0", "wall_loops": "2"}
        for generator in ("classic", "arachne"):
            self.assertEqual(tools.fixture_process_settings("separated-blocks", generator, original),
                             dict(original, wall_generator=generator))


class SourceSamplingAndRaftTest(unittest.TestCase):
    def test_raft_negative_gap_is_valid_and_complete(self):
        report = raft_report()
        tools.verify_cura_raft(report)
        layer = report["objects"]["groups"][0]["cross_layer_geometry"]["layers"][1]
        self.assertLess(layer["previous_model_gap_mm"], 0)
        self.assertTrue(layer["complete"])

    def test_raft_source_ends_remain_aligned_not_shifted_to_print_z(self):
        tools.verify_cura_raft(raft_report())
        for change in ("print_z_offset", "unknown", "wrong_faces", "wrong_source_band"):
            with self.subTest(change=change):
                report = raft_report()
                group = report["objects"]["groups"][0]
                volume = group["source_surface_layer_alignment"]["volumes"][0]
                if change == "print_z_offset":
                    volume["max_horizontal_boundary_offset_mm"] = 1.0
                elif change == "unknown":
                    group["source_surface_layer_alignment"] = surface_alignment_group(complete=False)[
                        "source_surface_layer_alignment"]
                elif change == "wrong_faces":
                    volume["horizontal_facet_count"] = 2
                else:
                    group["cross_layer_geometry"]["layers"][-1]["height_mm"] = 0.15
                with self.assertRaises(AssertionError):
                    tools.verify_cura_raft(report)

    def test_raft_overlap_missing_or_treated_as_incomplete_is_rejected(self):
        for change in ("overlap", "complete", "support"):
            with self.subTest(change=change):
                report = raft_report()
                group = report["objects"]["groups"][0]
                if change == "overlap":
                    group["cross_layer_geometry"]["layers"][1]["previous_model_gap_mm"] = 0
                elif change == "complete":
                    group["cross_layer_geometry"]["layers"][1]["complete"] = False
                else:
                    report["gcode"]["extrusion_by_role"] = []
                with self.assertRaises(AssertionError):
                    tools.verify_cura_raft(report)

    def test_raft_first_layer_requires_known_zero_intervening_model_projection(self):
        for observation in ({"complete": True, "area_mm2": 0.01}, {"complete": False, "area_mm2": None}):
            with self.subTest(observation=observation):
                report = raft_report()
                group = report["objects"]["groups"][0]
                group["planned_support_projection"]["instances"][0]["layers"][0]["support_levels"][0][
                    "intervening_model_projection"] = observation
                # Both are legal generic observations, but not this single-object first-layer fixture.
                tools.verify_support_projection(group)
                with self.assertRaises(AssertionError):
                    tools.verify_cura_raft(report)

    def test_fresh_sampling_distinguishes_missed_and_sampled_thin_volumes(self):
        tools.verify_source_volume_sampling(volume_sampling_report())

    def test_missing_cache_is_not_counted_as_zero_and_wrong_identity_is_rejected(self):
        for change in ("unknown", "incomplete", "merged", "wrong_z", "no_path", "false_miss"):
            with self.subTest(change=change):
                report = volume_sampling_report()
                volumes = report["objects"]["groups"][0]["source_volume_sampling"]["volumes"]
                if change == "unknown":
                    volumes[1]["source_section_layer_count"] = None
                elif change == "incomplete":
                    volumes[1]["complete"] = False
                elif change == "merged":
                    volumes.pop()
                elif change == "wrong_z":
                    volumes[2]["z_min_mm"] = 1.21
                elif change == "no_path":
                    volumes[2]["planned_path_overlap_layer_count"] = 0
                else:
                    volumes[1]["source_section_layer_count"] = 1
                with self.assertRaises(AssertionError):
                    tools.verify_source_volume_sampling(report)

    def test_new_fixture_settings_are_explicit_without_mutating_input(self):
        original = {"raft_mode": "legacy", "raft_layers": "0", "enable_support": "1", "layer_height": "0.3",
                    "initial_layer_print_height": "0.3", "adaptive_layer_height": "1", "wall_loops": "2"}
        before = dict(original)
        raft = tools.fixture_process_settings("cura-raft", "classic", original)
        sampling = tools.fixture_process_settings("source-volume-sampling", "arachne", original)
        self.assertEqual(original, before)
        self.assertEqual(raft["raft_mode"], "cura_v1")
        self.assertEqual(raft["raft_layer_0_z_overlap"], "0.1")
        self.assertEqual([raft["raft_" + phase + "_layers"] for phase in ("base", "interface", "surface")],
                         ["1", "1", "1"])
        self.assertEqual(sampling["raft_mode"], "legacy")
        self.assertEqual(sampling["raft_layers"], "0")
        for settings in (raft, sampling):
            self.assertEqual(settings["enable_support"], "0")
            self.assertEqual(settings["layer_height"], "0.2")
            self.assertEqual(settings["initial_layer_print_height"], "0.2")
            self.assertEqual(settings["adaptive_layer_height"], "0")
            self.assertEqual(settings["wall_loops"], "2")

    def test_existing_fixture_settings_keep_original_behavior(self):
        original = {"layer_height": "0.2", "wall_loops": "2"}
        self.assertEqual(tools.fixture_process_settings("raised-stroke", "classic", original),
                         dict(original, wall_generator="classic"))
        off = tools.fixture_process_settings("bridge", "classic", original, False)
        on = tools.fixture_process_settings("bridge", "classic", original, True)
        self.assertEqual({key for key in off.keys() | on.keys() if off.get(key) != on.get(key)}, {"enable_support"})


if __name__ == "__main__":
    unittest.main()
