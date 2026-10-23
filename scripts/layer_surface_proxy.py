"""Read-only same-source plane/staircase distance proxy for a local STL trial.

For a planar facet extruded vertically from a slice at s, the plane-normal
distance at source height z is abs(n_z) * abs(z - s). Integrate this exactly
over the original triangle's area, using both ACTUAL layer plans. This ideal
vertical-prism model extrapolates each local facet plane (it does not prove the
facet survives slicing) and excludes bead shape, path offsets/gaps, thermal and
machine effects. It is NOT predicted Ra, toolpath distance or print quality.
Horizontal caps are reported separately, never credited as improved slopes.
"""

import bisect
import math
import struct
import time


TOLERANCE = 1e-6  # Coordinate identity tolerance, not a quality threshold.
MAX_TRIANGLES = 1_000_000
MAX_CELLS = 5_000_000
MAX_BYTES = 256 * 1024 * 1024


def ascii_triangles(stream, deadline):
    reads = 0

    def tokens():
        nonlocal reads
        while True:
            reads += 1
            if deadline is not None and reads % 1024 == 0 and time.monotonic() > deadline:
                raise TimeoutError("ASCII STL parsing budget reached")
            line = stream.readline(1025)
            if len(line) > 1024:
                raise ValueError("ASCII STL line exceeds the parsing budget")
            if not line:
                return []
            parts = line.decode("utf-8").split()
            if parts:
                return parts

    def coordinates(parts, prefix):
        if parts[:len(prefix)] != prefix or len(parts) != len(prefix) + 3:
            raise ValueError("Invalid ASCII STL coordinate record")
        # The native mesh stores float32. Match that representation instead of
        # claiming extra source precision from an ASCII double parser.
        values = struct.unpack("<3f", struct.pack("<3f", *(float(v) for v in parts[len(prefix):])))
        if not all(math.isfinite(v) for v in values):
            raise ValueError("Non-finite ASCII STL coordinate")
        return values

    if tokens()[:1] != ["solid"]:
        raise ValueError("Expected one ASCII STL solid")
    count = 0
    while True:
        item = tokens()
        if item[:1] == ["endsolid"]:
            if not count or tokens():
                raise ValueError("Empty or multiple/trailing ASCII STL solids are unsupported")
            return
        coordinates(item, ["facet", "normal"])
        if tokens() != ["outer", "loop"]:
            raise ValueError("Invalid ASCII STL facet")
        points = tuple(coordinates(tokens(), ["vertex"]) for _ in range(3))
        if tokens() != ["endloop"] or tokens() != ["endfacet"]:
            raise ValueError("Incomplete ASCII STL facet")
        count += 1
        if count > MAX_TRIANGLES:
            raise ValueError("STL triangle budget reached")
        yield points


def triangles(path, deadline=None):
    size = path.stat().st_size
    if size > MAX_BYTES:
        raise ValueError("STL byte budget reached")
    with path.open("rb") as stream:
        header = stream.read(84)
        count = struct.unpack_from("<I", header, 80)[0] if len(header) == 84 else 0
        if not count or size != 84 + count * 50:
            stream.seek(0)
            yield from ascii_triangles(stream, deadline)
            return
        if count > MAX_TRIANGLES:
            raise ValueError("STL triangle budget reached")
        for _ in range(count):
            record = stream.read(50)
            if len(record) != 50:
                raise ValueError("Binary STL changed or was truncated")
            values = struct.unpack("<12fH", record)
            points = tuple(tuple(values[start:start + 3]) for start in (3, 6, 9))
            if not all(math.isfinite(v) for p in points for v in p):
                raise ValueError("Non-finite STL coordinate")
            yield points


def facet(points):
    u = [points[1][i] - points[0][i] for i in range(3)]
    v = [points[2][i] - points[0][i] for i in range(3)]
    cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.hypot(*cross)
    return length / 2, abs(cross[2]) / length if length else 0


def integral(zs, lo, hi, center=None):
    """Exact normalized triangle area (or |z-center| integral) in a Z interval.

    Uniform surface area has triangular, piecewise-linear Z density. Integrate
    locally to avoid subtracting nearly equal global cumulative moments.
    """
    z0, z1, z2 = zs
    lo, hi = max(lo, z0), min(hi, z2)
    if hi <= lo or z2 <= z0:
        return 0.
    cuts = sorted({lo, hi, *[z for z in (z1, center) if z is not None and lo < z < hi]})
    result = 0.
    for left, right in zip(cuts, cuts[1:]):
        mid, width = (left + right) / 2, right - left
        if mid < z1:
            slope = 2 / ((z1 - z0) * (z2 - z0))
            density = (mid - z0) * slope
        else:
            slope = -2 / ((z2 - z0) * (z2 - z1))
            density = (mid - z2) * slope
        value = width * density if center is None else abs(width * ((mid - center) * density + slope * width * width / 12))
        result += value
    return result


def plan_rows(layers):
    rows, previous = [], 0.
    for layer in layers:
        lo, hi, sample, height = (layer.get(key) for key in ("bottom_z_mm", "print_z_mm", "slice_z_mm", "height_mm"))
        if (not all(type(x) in (int, float) and math.isfinite(x) for x in (lo, hi, sample, height))
                or hi <= lo or not lo < sample < hi or abs(lo - previous) > TOLERANCE
                or abs(hi - lo - height) > TOLERANCE):
            raise ValueError("Surface proxy requires contiguous, bed-zero, unscaled model layers without raft offset")
        rows.append((lo, hi, sample))
        previous = hi
    if not rows:
        raise ValueError("Empty layer plan")
    return rows


def compare_surface(path, baseline, candidate, *, timeout=90):
    start = time.monotonic()
    result = {"scope": "same_source_facet_ideal_vertical_prism_normal_distance", "complete": False,
              "physical_quality_verified": False, "limits": {"triangles": MAX_TRIANGLES, "cells": MAX_CELLS,
                                                               "bytes": MAX_BYTES, "timeout_seconds": timeout}}
    try:
        a, b = plan_rows(baseline), plan_rows(candidate)
        min_z, max_z, count = math.inf, -math.inf, 0
        for points in triangles(path, start + timeout):
            count += 1
            min_z = min(min_z, *(p[2] for p in points))
            max_z = max(max_z, *(p[2] for p in points))
            if count % 4096 == 0 and time.monotonic() - start > timeout:
                raise TimeoutError("Surface proxy budget reached")
        top = min(max_z - min_z, a[-1][1], b[-1][1])
        if top <= 0:
            raise ValueError("No shared positive-height source range")
        cuts = sorted({0., top, *[row[1] for row in a + b if 0 < row[1] < top]})
        bands, ia, ib = [], 0, 0
        for lo, hi in zip(cuts, cuts[1:]):
            mid = (lo + hi) / 2
            while a[ia][1] <= mid:
                ia += 1
            while b[ib][1] <= mid:
                ib += 1
            bands.append((lo, hi, a[ia][2], b[ib][2]))
        ends = [row[1] for row in bands]
        local = [{"bottom_z_mm": lo, "top_z_mm": hi, "baseline_slice_z_mm": sa, "candidate_slice_z_mm": sb,
                  "sloped_area_mm2": 0., "baseline_distance_area_mm3": 0., "candidate_distance_area_mm3": 0.}
                 for lo, hi, sa, sb in bands]
        totals = dict.fromkeys(("source_area_mm2", "horizontal_area_excluded_mm2", "sloped_area_compared_mm2",
                               "vertical_area_mm2", "area_above_shared_top_mm2", "improved_sloped_area_mm2",
                               "worsened_sloped_area_mm2", "unchanged_sloped_area_mm2",
                               "baseline_distance_area_mm3", "candidate_distance_area_mm3"), 0.)
        cells, degenerate = 0, 0
        for index, points in enumerate(triangles(path, start + timeout)):
            if index % 1024 == 0 and time.monotonic() - start > timeout:
                raise TimeoutError("Surface proxy budget reached")
            area, nz = facet(points)
            totals["source_area_mm2"] += area
            if area == 0:
                degenerate += 1
                continue
            zs = sorted(p[2] - min_z for p in points)
            if zs[2] == zs[0]:
                totals["horizontal_area_excluded_mm2"] += area
                if zs[0] > top:
                    totals["area_above_shared_top_mm2"] += area
                continue
            totals["area_above_shared_top_mm2"] += area * integral(zs, top, zs[2])
            if nz == 0:
                totals["vertical_area_mm2"] += area * integral(zs, 0., top)
                continue
            first = bisect.bisect_right(ends, zs[0])
            for band_index in range(first, len(bands)):
                lo, hi, sa, sb = bands[band_index]
                lo, hi = max(lo, zs[0]), min(hi, zs[2])
                if hi <= lo:
                    break
                cells += 1
                if cells % 4096 == 0 and time.monotonic() - start > timeout:
                    raise TimeoutError("Surface integration budget reached")
                if cells > MAX_CELLS:
                    raise ValueError("Surface proxy cell budget reached")
                fraction = integral(zs, lo, hi)
                totals["sloped_area_compared_mm2"] += area * fraction
                old_distance = area * nz * integral(zs, lo, hi, sa)
                new_distance = area * nz * integral(zs, lo, hi, sb)
                totals["baseline_distance_area_mm3"] += old_distance
                totals["candidate_distance_area_mm3"] += new_distance
                local[band_index]["sloped_area_mm2"] += area * fraction
                local[band_index]["baseline_distance_area_mm3"] += old_distance
                local[band_index]["candidate_distance_area_mm3"] += new_distance
                # Distances switch order only at the midpoint of the two slice
                # planes. Subdivide there; no tessellation-dependent vote count.
                if abs(sa - sb) <= 64 * max(math.ulp(sa), math.ulp(sb)):
                    totals["unchanged_sloped_area_mm2"] += area * fraction
                else:
                    split = (sa + sb) / 2
                    segments = sorted({lo, hi, *([split] if lo < split < hi else [])})
                    for left, right in zip(segments, segments[1:]):
                        mid = (left + right) / 2
                        key = "improved_sloped_area_mm2" if abs(mid - sb) < abs(mid - sa) else "worsened_sloped_area_mm2"
                        totals[key] += area * integral(zs, left, right)
        compared = totals["sloped_area_compared_mm2"]
        result.update(complete=True, triangle_count=count, degenerate_triangle_count=degenerate, evaluated_cells=cells,
                      source_height_mm=max_z - min_z, shared_top_mm=top,
                      baseline_top_offset_mm=a[-1][1] - (max_z - min_z),
                      candidate_top_offset_mm=b[-1][1] - (max_z - min_z), **totals)
        result["baseline_mean_distance_mm"] = totals["baseline_distance_area_mm3"] / compared if compared else None
        result["candidate_mean_distance_mm"] = totals["candidate_distance_area_mm3"] / compared if compared else None
        result["local_z_bands"] = local
        result["limitations"] = ["Not bead shape, roughness, final path fidelity or physical quality",
                                  "Extrapolates local facet planes; does not prove features survive the slice planes",
                                  "Only shared source Z range; horizontal caps and top offsets are separate",
                                  "One unscaled binary or single-solid ASCII STL; Z is shifted by source minimum; XY arrangement is irrelevant",
                                  "Area integral over stored facets; self intersections/duplicate/internal surfaces are not classified",
                                  "A lower mean can coexist with locally worsened areas; never auto-approve from this metric"]
    except (ValueError, OSError, TimeoutError, OverflowError, struct.error) as error:
        result["error"] = f"{type(error).__name__}: {error}"
    result["elapsed_seconds"] = time.monotonic() - start
    return result
