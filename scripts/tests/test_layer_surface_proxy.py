import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import layer_surface_proxy as proxy


def plan(heights):
    result, z = [], 0.
    for height in heights:
        result.append({"bottom_z_mm": z, "print_z_mm": z + height, "slice_z_mm": z + height / 2, "height_mm": height})
        z += height
    return result


SLOPE = ((0., 0., 0.), (1., 0., 1.), (0., 1., 1.))


class SurfaceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "test.stl"

    def write(self, facets):
        with self.path.open("wb") as stream:
            stream.write(b"test".ljust(80, b"\0") + struct.pack("<I", len(facets)))
            for points in facets:
                stream.write(struct.pack("<12fH", 0, 0, 0, *(v for p in points for v in p), 0))

    def test_triangle_density_area_and_moment_analytic(self):
        # Includes equal-height pairs at either end; no division by zero.
        for zs in ((0., 1., 1.), (0., 0., 1.), (0., .3, 1.)):
            self.assertAlmostEqual(proxy.integral(zs, 0., 1.), 1.)
            self.assertAlmostEqual(proxy.integral(zs, 0., 1., 0.), sum(zs) / 3)
            parts = sum(proxy.integral(zs, i / 100, (i + 1) / 100, .45) for i in range(100))
            self.assertAlmostEqual(proxy.integral(zs, 0., 1., .45), parts)
        self.assertEqual(proxy.integral((0., 1., 1.), 0., 1., .5), .25)
        self.assertEqual(proxy.integral((0., 0., 0.), 0., 1.), 0.)

    def test_same_plan_no_manufactured_gain(self):
        self.write([SLOPE])
        value = proxy.compare_surface(self.path, plan([1.]), plan([1.]))
        self.assertTrue(value["complete"])
        self.assertEqual(value["baseline_mean_distance_mm"], value["candidate_mean_distance_mm"])
        self.assertEqual(value["improved_sloped_area_mm2"], 0.)
        self.assertEqual(value["worsened_sloped_area_mm2"], 0.)
        self.assertAlmostEqual(value["unchanged_sloped_area_mm2"], math.sqrt(3) / 2)

    def test_analytic_slope_refinement_and_local_worsening(self):
        self.write([SLOPE])
        value = proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))
        self.assertAlmostEqual(value["baseline_mean_distance_mm"], .25 / math.sqrt(3))
        self.assertAlmostEqual(value["candidate_mean_distance_mm"], .125 / math.sqrt(3))
        self.assertGreater(value["worsened_sloped_area_mm2"], 0.)
        self.assertGreater(value["improved_sloped_area_mm2"], value["worsened_sloped_area_mm2"])
        self.assertAlmostEqual(sum(value[key] for key in ("improved_sloped_area_mm2", "worsened_sloped_area_mm2",
                                                        "unchanged_sloped_area_mm2")), value["sloped_area_compared_mm2"])
        self.assertFalse(value["physical_quality_verified"])
        for key in ("baseline_distance_area_mm3", "candidate_distance_area_mm3"):
            self.assertAlmostEqual(sum(row[key] for row in value["local_z_bands"]), value[key])
        self.assertAlmostEqual(sum(row["sloped_area_mm2"] for row in value["local_z_bands"]), value["sloped_area_compared_mm2"])

    def test_tessellation_translation_and_winding_do_not_change_proxy(self):
        self.write([SLOPE])
        expected = proxy.compare_surface(self.path, plan([.4, .6]), plan([.2, .4, .4]))
        midpoint = tuple((SLOPE[1][i] + SLOPE[2][i]) / 2 for i in range(3))
        split = [(SLOPE[0], SLOPE[1], midpoint), (SLOPE[0], midpoint, SLOPE[2])]
        self.write([tuple((p[0] + 10, p[1] - 3, p[2] + 12) for p in reversed(points)) for points in split])
        actual = proxy.compare_surface(self.path, plan([.4, .6]), plan([.2, .4, .4]))
        for key in ("baseline_mean_distance_mm", "candidate_mean_distance_mm", "sloped_area_compared_mm2",
                    "improved_sloped_area_mm2", "worsened_sloped_area_mm2"):
            self.assertAlmostEqual(actual[key], expected[key], places=12)

    def test_vertical_walls_and_horizontal_caps_not_credited_as_slopes(self):
        self.write([((0, 0, 0), (0, 1, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 1), (0, 1, 1))])
        value = proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))
        self.assertEqual(value["sloped_area_compared_mm2"], 0.)
        self.assertEqual(value["horizontal_area_excluded_mm2"], .5)
        self.assertEqual(value["vertical_area_mm2"], .5)
        self.assertIsNone(value["candidate_mean_distance_mm"])

    def test_uncovered_top_is_not_hidden_in_average(self):
        self.write([SLOPE])
        value = proxy.compare_surface(self.path, plan([1.]), plan([.4, .4]))
        self.assertEqual(value["shared_top_mm"], .8)
        self.assertAlmostEqual(value["candidate_top_offset_mm"], -.2)
        self.assertGreater(value["area_above_shared_top_mm2"], 0.)
        self.assertAlmostEqual(value["sloped_area_compared_mm2"] + value["area_above_shared_top_mm2"], value["source_area_mm2"])

    def test_reject_raft_offset_gap_invalid_geometry_and_budgets(self):
        self.write([SLOPE])
        invalid = plan([.5, .5])
        invalid[0]["bottom_z_mm"] = .1
        self.assertFalse(proxy.compare_surface(self.path, plan([1.]), invalid)["complete"])
        with patch.object(proxy, "MAX_CELLS", 0):
            value = proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))
            self.assertFalse(value["complete"])
            self.assertNotIn("candidate_mean_distance_mm", value)
        self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([1.]), timeout=-1)["complete"])
        self.path.write_bytes(b"not a supported binary STL")
        self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([1.]))["complete"])

    def test_ascii_matches_binary_and_native_float_precision(self):
        source = "solid test\nfacet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex 1 0 1\nvertex 0 1 1\nendloop\nendfacet\nendsolid test\n"
        self.write([SLOPE])
        expected = proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))
        self.path.write_text(source)
        actual = proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))
        self.assertTrue(actual["complete"])
        self.assertEqual(actual["local_z_bands"], expected["local_z_bands"])
        self.path.write_text(source.replace("vertex 1 0 1", "vertex 1.123456789 0 1"))
        self.assertEqual(next(proxy.triangles(self.path))[1][0], struct.unpack("<f", struct.pack("<f", 1.123456789))[0])

    def test_malformed_and_over_budget_ascii_is_not_partial_success(self):
        source = "solid test\nfacet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex 1 0 1\nvertex 0 1 1\nendloop\nendfacet\nendsolid test\n"
        for value in (source.replace("endfacet", ""), source.replace("endsolid test", ""), source + source,
                      source.replace("vertex 1 0 1", "vertex nan 0 1"), source.replace("solid test", "solid " + "x" * 1100, 1),
                      source.replace("vertex 1 0 1", "vertex 1e300 0 1")):
            self.path.write_text(value)
            self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([.5, .5]))["complete"])
        self.path.write_text(source)
        with patch.object(proxy, "MAX_BYTES", 10):
            self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([1.]))["complete"])
        with patch.object(proxy, "MAX_TRIANGLES", 0):
            self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([1.]))["complete"])
        self.write([((0, 0, 0), (1, 0, 1), (0, float("nan"), 1))])
        self.assertFalse(proxy.compare_surface(self.path, plan([1.]), plan([1.]))["complete"])


if __name__ == "__main__":
    unittest.main()
