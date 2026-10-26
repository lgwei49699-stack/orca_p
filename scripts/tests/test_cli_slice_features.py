import importlib.util
import copy
from collections import Counter
import math
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("feature_fixture_tools", SCRIPTS / "test_cli_slice_features.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)
from test_cli_slice_analysis import (CORE_LAYER_FIELDS, CORE_GCODE_FIELDS, CORE_NORMAL_FIELDS, CORE_MATERIAL_FIELDS,
                                     REMOVED_GROUP_METRICS, validate_core_report_group,
                                     validate_gcode_observations, validate_plate_bed_footprint, validate_report, validate_solid_fill_peak,
                                     validate_width_height_observation, validate_residual_peak,
                                     validate_layer_time_threshold_observation,
                                     validate_estimated_peak_flow, validate_estimated_flow_cap_distance, validate_metric_values)


def core_layer(**changes):
    layer = {key: 0 for key in CORE_LAYER_FIELDS}
    layer.update(layer_index=0, slice_z_mm=.1, print_z_mm=.2, bottom_z_mm=0., height_mm=.2,
                 complete=True, section_area_mm2=8., planned_path_area_mm2=8.,
                 section_island_count=1, section_min_island_gap_mm=None,
                 parallel_boundary_clearance={"scope": "same_section_island_parallel_voronoi_sites",
                     "complete": True, "min_material_width_mm": None, "min_void_width_mm": None})
    layer.update(changes)
    return layer


class CoreReportFieldsTest(unittest.TestCase):
    def fixture(self):
        return {"model_layer_count": 1, "planned_layer_coverage": {
            "scope": "per_instance_model_paths_before_gcode", "layers": [core_layer()]}}

    def test_current_core_fields_are_complete_without_retired_summaries(self):
        validate_core_report_group(self.fixture())

    def test_every_retired_group_is_rejected_even_when_null_or_empty(self):
        for key in REMOVED_GROUP_METRICS:
            for value in (None, {}, {"complete": False}):
                with self.subTest(key=key, value=value), self.assertRaises(AssertionError):
                    validate_core_report_group(dict(self.fixture(), **{key: value}))

    def test_retired_layer_fields_are_rejected(self):
        for key in ("section_uncovered_area_mm2", "section_covered_area_mm2",
                    "top_fill_target_area_mm2", "top_fill_uncovered_area_mm2",
                    "bottom_fill_target_area_mm2", "bottom_fill_uncovered_area_mm2",
                    "section_islands_with_multiple_planned_regions_count",
                    "planned_regions_overlapping_multiple_section_islands_count",
                    "closed_voids_with_multiple_remaining_regions_count",
                    "planned_enclosed_exterior_region_count", "planned_enclosed_exterior_area_mm2"):
            group = self.fixture()
            group["planned_layer_coverage"]["layers"][0][key] = None
            with self.subTest(key=key), self.assertRaises(AssertionError):
                validate_core_report_group(group)

    def test_required_core_fields_and_layer_identity_are_not_optional(self):
        for key in CORE_LAYER_FIELDS:
            group = self.fixture()
            del group["planned_layer_coverage"]["layers"][0][key]
            with self.subTest(key=key), self.assertRaises(AssertionError):
                validate_core_report_group(group)
        for change in ({"layer_index": True}, {"layer_index": 1}, {"complete": 1}):
            group = self.fixture()
            group["planned_layer_coverage"]["layers"][0].update(change)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_core_report_group(group)

    def test_real_report_entry_rejects_retired_groups_before_reading_artifacts(self):
        group = dict(self.fixture(), nominal_target_path_connectivity={})
        report = {"schema_version": 1, "scope": "sliced_print_metrics", "plate_index": 1,
                  "objects": {"groups": [group]}}
        with self.assertRaisesRegex(AssertionError, "Removed diagnostic groups"):
            validate_report(report, Path("."), {"id": 1}, {})


class SolidFillPeakTest(unittest.TestCase):
    def fixture(self):
        return {"scope": "largest_solid_uncovered_layer_exclusive_target_kinds", "complete": True,
                "layer_index": 1, "print_z_mm": .4, "uncovered_area_mm2": 10.,
                "surface_area_mm2": 1., "internal_solid_area_mm2": 2., "bridge_area_mm2": 6., "mixed_area_mm2": 1.}

    def layers(self):
        return [core_layer(solid_fill_uncovered_area_mm2=5.),
                core_layer(layer_index=1, print_z_mm=.4, solid_fill_uncovered_area_mm2=10.)]

    def test_disjoint_categories_match_the_actual_maximum_and_layer(self):
        validate_solid_fill_peak(self.fixture(), self.layers())
        for change in ({"layer_index": 0}, {"print_z_mm": .41}, {"bridge_area_mm2": 10.},
                       {"surface_area_mm2": -1.}, {"mixed_area_mm2": float("nan")}, {"uncovered_area_mm2": 5.}):
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_solid_fill_peak({**self.fixture(), **change}, self.layers())

    def test_incomplete_nonpeak_layer_does_not_certify_the_maximum(self):
        layers = self.layers()
        layers[0]["complete"] = False
        value = {key: None for key in self.fixture()}
        value.update(scope=self.fixture()["scope"], complete=False)
        validate_solid_fill_peak(value, layers)
        for key in ("layer_index", "surface_area_mm2", "uncovered_area_mm2"):
            with self.subTest(key=key), self.assertRaises(AssertionError):
                validate_solid_fill_peak({**value, key: 0}, layers)

    def test_zero_gap_has_no_problem_layer(self):
        value = {key: 0 for key in self.fixture()}
        value.update(scope=self.fixture()["scope"], complete=True, layer_index=None, print_z_mm=None)
        validate_solid_fill_peak(value, [core_layer()])
        with self.assertRaises(AssertionError):
            validate_solid_fill_peak({**value, "layer_index": 0, "print_z_mm": .2}, [core_layer()])


class ResidualEvidenceTest(unittest.TestCase):
    def test_paired_bead_does_not_use_global_extrema(self):
        row = {"complete": True, "role": "gap_infill", "move_count": 3, "path_length_mm": 15,
               "nominal_width_mm": {"min": .065, "max": .4}, "nominal_height_mm": {"min": .08, "max": .32},
               "width_height_observation": {"scope": "same_move_nominal_width_height_non_bridge", "complete": True,
                   "applicable": True, "below_height_move_count": 1, "below_height_path_length_mm": 2,
                   "min_ratio": .065 / .08, "width_at_min_mm": .065, "height_at_min_mm": .08, "print_z_at_min_mm": 3}}
        validate_width_height_observation(row)
        for change in ({"min_ratio": .065 / .32}, {"below_height_move_count": 4}, {"below_height_path_length_mm": 16},
                       {"below_height_move_count": 0}, {"print_z_at_min_mm": float("nan")}, {"height_at_min_mm": .01}):
            altered = copy.deepcopy(row)
            altered["width_height_observation"].update(change)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                validate_width_height_observation(altered)

    def test_residual_first_peak_and_partition(self):
        for hole in (False, True):
            total = "intrusion_area_mm2" if hole else "unprinted_area_mm2"
            layer_key = "hole_intrusion_area_mm2" if hole else "completely_unprinted_island_area_mm2"
            value = {"scope": "largest_hole_intrusion_layer_exclusive_path_kinds" if hole else "largest_unprinted_layer_region_slices",
                     "complete": True, "layer_index": 0, "print_z_mm": .2, total: 1.}
            parts = {"wall_area_mm2": .5, "gap_fill_area_mm2": .2, "bridge_area_mm2": .2, "other_fill_area_mm2": 0., "mixed_area_mm2": .1} if hole else {
                "retained_region_area_mm2": .5, "absent_region_area_mm2": .5}
            value.update(parts)
            layers = [core_layer(**{layer_key: 1.}), core_layer(layer_index=1, print_z_mm=.4, **{layer_key: 1.})]
            validate_residual_peak(value, layers, hole)
            for change in ({"layer_index": 1, "print_z_mm": .4}, {next(iter(parts)): 2.}, {total: .5}):
                with self.subTest(hole=hole, change=change), self.assertRaises(AssertionError):
                    validate_residual_peak({**value, **change}, layers, hole)
            for key in (*parts, total): value[key] = 0.
            value.update(layer_index=None, print_z_mm=None)
            validate_residual_peak(value, [core_layer()], hole)
            for key in (*parts, total): value[key] = None
            value["complete"] = False
            validate_residual_peak(value, [], hole)


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

    def test_retired_connected_regions_and_bounds_are_not_emitted(self):
        for key in ("model_regions", "support_and_raft_regions", "brim_regions", "combined_regions", "bounds_mm"):
            with self.subTest(key=key), self.assertRaises(AssertionError):
                validate_plate_bed_footprint(dict(self.fixture(), **{key: None}))


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

    def test_full_report_entry_checks_core_material_flow_without_mutation(self):
        def samples(value, sample_count=1):
            return {"count": sample_count, "invalid_count": 0, "min": value, "max": value}

        group = {"source_object_id": 1, "group_index": 0, "instance_count": 1, "model_layer_count": 3,
                 "support_layer_count": 0, "model_layer_height_mm": samples(0.2, 3),
                 "first_model_layer_section_area_mm2_per_instance": 8,
                 "planned_layer_coverage": {"scope": "per_instance_model_paths_before_gcode",
                     "layers": [core_layer(layer_index=index) for index in range(3)]}}
        material = {"extruder_id": 0, "configured_filament_max_volumetric_speed_mm3_s": 8,
                    "estimated_peak_flow": self.fixture(),
                    "estimated_flow_cap_distance": EstimatedFlowCapDistanceTest().fixture(),
                    "layer_time_threshold_observation": {**LayerTimeThresholdObservationTest().fixture(),
                        "active_layer_sequence_count": 1, "below_threshold_layer_ratio": 1},
                    "estimated_volume_mm3": {"total": 1, "other": 1, "support": 0, "wipe_tower": 0, "flush": 0}}
        gcode = {"normal_estimate": {"total_time_s": 1, "layer_sequence_count": 1}, "extruders": [material],
                 "small_perimeter_selection": {"scope": "native_small_perimeter_selection_before_speed_overrides", "complete": True,
                    "selected_loop_count": 0, "selected_path_count": 0, "selected_path_length_mm": 0,
                    "requested_speed_mm_s": {"min": None, "max": None}},
                 "quality_paths": {"scope": "final_gcode_positive_extrusion_moves", "rows": []}}
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
        with self.assertRaisesRegex(AssertionError, "Material core fields"):
            validate_metric_values(report, plate, facts)
        material["estimated_flow_cap_distance"] = EstimatedFlowCapDistanceTest().fixture()
        material["estimated_peak_flow"]["max_to_configured_cap_ratio"] = None
        with self.assertRaises(AssertionError):
            validate_metric_values(report, plate, facts)
        del material["estimated_peak_flow"]
        with self.assertRaisesRegex(AssertionError, "Material core fields"):
            validate_metric_values(report, plate, facts)


class GcodeCoreFieldsTest(unittest.TestCase):
    def fixture(self):
        return {"normal_estimate": {"total_time_s": 1, "layer_sequence_count": 1},
                "small_perimeter_selection": {"scope": "native_small_perimeter_selection_before_speed_overrides", "complete": True,
                    "selected_loop_count": 0, "selected_path_count": 0, "selected_path_length_mm": 0,
                    "requested_speed_mm_s": {"min": None, "max": None}},
                "quality_paths": {"scope": "final_gcode_positive_extrusion_moves", "rows": []},
                "extruders": [{key: None for key in CORE_MATERIAL_FIELDS}]}

    def test_only_retained_observation_groups_remain(self):
        value = self.fixture()
        before = copy.deepcopy(value)
        validate_gcode_observations(value)
        self.assertEqual(value, before)

    def test_small_perimeter_selection_positive_and_unknown_are_distinct(self):
        value = self.fixture()
        selected = value["small_perimeter_selection"]
        selected.update(selected_loop_count=2, selected_path_count=3, selected_path_length_mm=12,
                        requested_speed_mm_s={"min": 15, "max": 20})
        validate_gcode_observations(value)
        for key, bad in (("selected_path_count", 1), ("selected_path_length_mm", 0),
                         ("requested_speed_mm_s", {"min": 20, "max": 15})):
            invalid = copy.deepcopy(value)
            invalid["small_perimeter_selection"][key] = bad
            with self.subTest(key=key), self.assertRaises(AssertionError):
                validate_gcode_observations(invalid)
        selected.update(complete=False, selected_loop_count=None, selected_path_count=None,
                        selected_path_length_mm=None, requested_speed_mm_s={"min": None, "max": None})
        validate_gcode_observations(value)
        selected["selected_loop_count"] = 0
        with self.assertRaises(AssertionError):
            validate_gcode_observations(value)

    def test_missing_core_groups_cannot_silently_become_zero(self):
        for keys, select in ((CORE_GCODE_FIELDS, lambda value: value),
                             (CORE_NORMAL_FIELDS, lambda value: value["normal_estimate"]),
                             (CORE_MATERIAL_FIELDS, lambda value: value["extruders"][0])):
            for key in keys:
                value = self.fixture()
                del select(value)[key]
                with self.subTest(key=key), self.assertRaises(AssertionError):
                    validate_gcode_observations(value)
        with self.assertRaises(AssertionError):
            validate_gcode_observations(dict(self.fixture(), extruders=[]))

    def test_retired_statistics_are_rejected_even_as_null(self):
        for keys, select in (
                (("move_counts", "travel", "seam_record_count", "seam_positions_mm", "seam_position_bounds_mm",
                  "extrusion_ranges", "extrusion_by_role", "filament_by_role"), lambda value: value),
                (("prepare_time_s", "layer_duration_s", "layer_duration_sequence_s"), lambda value: value["normal_estimate"]),
                (("extrusion_ranges", "max_nominal_flow_to_cap_ratio"), lambda value: value["extruders"][0])):
            for key in keys:
                value = self.fixture()
                select(value)[key] = None
                with self.subTest(key=key), self.assertRaises(AssertionError):
                    validate_gcode_observations(value)


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

    def test_actual_role_evidence_ignores_comments_travel_and_pure_e_recovery(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "roles.gcode"
            path.write_text("G90\nM83\n;TYPE:Bridge\nG1 X1\nG1 E1\nG1 X2 E-1\n"
                            ";TYPE:Support\nG1 X3 E0\n"
                            ";TYPE:Support interface\nG1 X4 E.1\n"
                            ";TYPE:Custom\nG1 X5 E.1\n", encoding="utf-8")
            self.assertEqual(tools.gcode_extrusion_roles(path), {"Support interface", "Custom"})

    def test_actual_role_evidence_respects_modes_resets_and_arcs_without_estimating_length(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "roles.gcode"
            path.write_text("G90\nM82\nG92 E10\n;TYPE:Bridge\nG1 X1 E9\nG1 X2 E9.5\n"
                            "G92 E0\n;TYPE:Support\nG1 X3 E0\n"
                            "G91\nM83\n;TYPE:Support transition\nG1 X+1 E.1\n"
                            ";TYPE:Support interface\nG2 I1 J0 E.1\n", encoding="utf-8")
            self.assertEqual(tools.gcode_extrusion_roles(path), {"Bridge", "Support transition", "Support interface"})

    def test_fixture_role_parser_does_not_silently_accept_inches(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "roles.gcode"
            path.write_text("G20\nM83\n;TYPE:Bridge\nG1 X1 E1\n", encoding="utf-8")
            with self.assertRaises(AssertionError):
                tools.gcode_extrusion_roles(path)

    def test_sparse_top_requires_uncovered_solid_target_on_the_final_layer(self):
        layers = [core_layer(layer_index=index, solid_fill_target_area_mm2=20,
                             solid_fill_uncovered_area_mm2=12) for index in range(2)]
        report = {"objects": {"groups": [{"instance_count": 1, "model_layer_count": 2,
                    "planned_layer_coverage": {"scope": "per_instance_model_paths_before_gcode", "layers": layers}}]}}
        tools.verify_feature_metrics("sparse-top", {"classic": report}, {})
        layers[-1]["solid_fill_uncovered_area_mm2"] = 0
        with self.assertRaises(AssertionError):
            tools.verify_feature_metrics("sparse-top", {"classic": report}, {})

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


def support_report(name="bridge", support=False):
    previous_area, section_area = {"bridge": (48, 96), "cantilever": (24, 72), "new-island": (24, 48)}[name]
    layers = [core_layer(layer_index=index, slice_z_mm=3.9 + index * .2, print_z_mm=4. + index * .2,
                         bottom_z_mm=3.8 + index * .2, section_area_mm2=area)
              for index, area in enumerate((previous_area, section_area))]
    return {"objects": {"groups": [{"model_layer_count": 2, "support_layer_count": 1 if support else 0,
                                    "planned_layer_coverage": {"scope": "per_instance_model_paths_before_gcode", "layers": layers}}]},
            "gcode": {"extruders": [{"estimated_volume_mm3": {"support": 2 if support else None}}]}}


class NativeSupportChecksTest(unittest.TestCase):
    def test_known_transition_sections_and_disabled_support_remain_checked(self):
        for name in tools.SUPPORT_FIXTURES:
            with self.subTest(name=name):
                report = support_report(name)
                roles = {"classic": {"Bridge"} if name == "bridge" else set()}
                tools.verify_support_fixture(name, {"classic": report}, False, roles)
                report["objects"]["groups"][0]["planned_layer_coverage"]["layers"][1]["section_area_mm2"] += 1
                with self.assertRaises(AssertionError):
                    tools.verify_support_fixture(name, {"classic": report}, False, roles)

    def test_support_paths_and_volume_required_but_bridge_may_disappear(self):
        report = support_report(support=True)
        tools.verify_support_fixture("bridge", {"classic": report}, True, {"classic": {"Support interface"}})
        for missing in ("path", "volume"):
            with self.subTest(missing=missing):
                changed = copy.deepcopy(report)
                roles = {"classic": {"Support interface"}}
                if missing == "path":
                    roles["classic"] = set()
                else:
                    changed["gcode"]["extruders"][0]["estimated_volume_mm3"]["support"] = None
                with self.assertRaises(AssertionError):
                    tools.verify_support_fixture("bridge", {"classic": changed}, True, roles)

    def test_bridge_must_reach_final_gcode(self):
        report = support_report()
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("bridge", {"classic": report}, False, {"classic": set()})

    def test_disabled_support_cannot_hide_actual_support_extrusion(self):
        with self.assertRaises(AssertionError):
            tools.verify_support_fixture("bridge", {"classic": support_report()}, False,
                                         {"classic": {"Bridge", "Support"}})

    def test_support_does_not_change_source_section_geometry(self):
        off = {generator: support_report("new-island") for generator in ("classic", "arachne")}
        on = {generator: support_report("new-island", True) for generator in ("classic", "arachne")}
        tools.verify_support_geometry_unchanged(off, on)
        on["classic"]["objects"]["groups"][0]["planned_layer_coverage"]["layers"][1]["section_area_mm2"] = 0
        with self.assertRaises(AssertionError):
            tools.verify_support_geometry_unchanged(off, on)


def raft_report():
    report = support_report(support=True)
    group = report["objects"]["groups"][0]
    layers = []
    for index in range(20):
        top = 1.2 + index * 0.2 - (0.1 if index else 0)
        layers.append(core_layer(layer_index=index, slice_z_mm=0.1 + index * .2, print_z_mm=top,
                                 bottom_z_mm=top - .2, section_area_mm2=96))
    group.update(model_layer_count=20, support_layer_count=3)
    group["planned_layer_coverage"]["layers"] = layers
    return report


def topology_counts(islands=1, voids=0, filled=0, complete=True):
    return {"complete": complete, "section_island_count": islands, "closed_void_region_count": voids,
            "closed_void_fully_covered_count": filled}


class TopologyCountsTest(unittest.TestCase):
    def test_core_counts_allow_zero_and_fully_covered_holes(self):
        tools.verify_topology_counts(topology_counts())
        tools.verify_topology_counts(topology_counts(islands=2, voids=3, filled=1))

    def test_incomplete_core_counts_remain_unknown(self):
        layer = topology_counts(islands=None, voids=None, filled=None, complete=False)
        tools.verify_topology_counts(layer)
        for key in ("section_island_count", "closed_void_region_count", "closed_void_fully_covered_count"):
            with self.subTest(key=key), self.assertRaises(AssertionError):
                tools.verify_topology_counts(dict(layer, **{key: 0}))

    def test_covered_holes_cannot_exceed_source_holes(self):
        with self.assertRaises(AssertionError):
            tools.verify_topology_counts(topology_counts(voids=1, filled=2))

    def test_missing_noninteger_negative_or_unknown_complete_counts_are_rejected(self):
        for key in ("section_island_count", "closed_void_region_count", "closed_void_fully_covered_count"):
            for value in (None, True, .5, -1):
                with self.subTest(key=key, value=value), self.assertRaises(AssertionError):
                    tools.verify_topology_counts(dict(topology_counts(), **{key: value}))
            layer = topology_counts()
            del layer[key]
            with self.assertRaises(KeyError):
                tools.verify_topology_counts(layer)


class OpenSlotGeometryTest(unittest.TestCase):
    def test_open_slots_remain_source_openings_not_closed_holes(self):
        for name, area in tools.OPEN_SLOT_AREAS.items():
            layers = [{"slice_z_mm": 0.1 + index * 0.2, "section_area_mm2": area, "section_island_count": 1,
                       "closed_void_region_count": 0, "hole_area_mm2": 0, "complete": True} for index in range(12)]
            tools.verify_open_slot_layers(name, layers)
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


class RaftAndFixtureSettingsTest(unittest.TestCase):
    def test_tree_settings_are_explicit_without_changing_existing_normal_support(self):
        original = {"layer_height": "0.2", "wall_loops": "2", "enable_support": "0", "raft_mode": "legacy"}
        tree = tools.fixture_process_settings("new-island-tree", "arachne", original)
        self.assertEqual(original["enable_support"], "0")
        self.assertEqual(tree, dict(original, wall_generator="arachne", enable_support="1", support_type="tree(auto)",
                                    support_style="tree_slim", support_on_build_plate_only="1", bridge_no_support="0", raft_layers="0"))
        normal = tools.fixture_process_settings("new-island", "arachne", original, True)
        self.assertEqual(normal["support_type"], "normal(auto)")
        self.assertEqual(normal["support_style"], "default")

    def test_wedge_config_is_isolated_and_explicit(self):
        original = {"layer_height": "0.3", "initial_layer_print_height": "0.3", "wall_loops": "2",
                    "adaptive_layer_height": "1", "enable_support": "1", "raft_layers": "3",
                    "xy_contour_compensation": "0.1", "elefant_foot_compensation": "0.15"}
        before = dict(original)
        classic = tools.fixture_process_settings("slope-wedge", "classic", original)
        arachne = tools.fixture_process_settings("slope-wedge", "arachne", original)
        self.assertEqual(original, before)
        self.assertEqual({key for key in classic if classic[key] != arachne[key]}, {"wall_generator"})
        for key, value in (("layer_height", "0.2"), ("initial_layer_print_height", "0.2"),
                           ("adaptive_layer_height", "0"), ("enable_support", "0"), ("raft_layers", "0"),
                           ("slicing_mode", "regular"), ("xy_contour_compensation", "0"),
                           ("elefant_foot_compensation", "0"), ("wall_loops", "2")):
            self.assertEqual(classic[key], value)

    def test_wedge_retains_only_its_analytical_two_dimensional_sections(self):
        layers = [core_layer(layer_index=index, slice_z_mm=.1 + index * .2,
                             section_area_mm2=6 * (4 - (.1 + index * .2))) for index in range(20)]
        group = {"model_layer_count": 20, "planned_layer_coverage": {
            "scope": "per_instance_model_paths_before_gcode", "layers": layers}}
        tools.verify_wedge_alignment(group)
        layers[0]["section_area_mm2"] += 1
        with self.assertRaises(AssertionError):
            tools.verify_wedge_alignment(group)

    def test_raft_negative_gap_is_valid_and_complete(self):
        report = raft_report()
        tools.verify_cura_raft(report, {"Support interface"})
        layer = report["objects"]["groups"][0]["planned_layer_coverage"]["layers"][1]
        self.assertLess(layer["bottom_z_mm"] - report["objects"]["groups"][0]["planned_layer_coverage"]["layers"][0]["print_z_mm"], 0)
        self.assertTrue(layer["complete"])

    def test_raft_overlap_missing_or_treated_as_incomplete_is_rejected(self):
        for change in ("overlap", "complete", "support"):
            with self.subTest(change=change):
                report = raft_report()
                roles = {"Support interface"}
                group = report["objects"]["groups"][0]
                if change == "overlap":
                    group["planned_layer_coverage"]["layers"][1]["bottom_z_mm"] = group["planned_layer_coverage"]["layers"][0]["print_z_mm"]
                elif change == "complete":
                    group["planned_layer_coverage"]["layers"][1]["complete"] = False
                else:
                    roles = set()
                with self.assertRaises(AssertionError):
                    tools.verify_cura_raft(report, roles)

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
