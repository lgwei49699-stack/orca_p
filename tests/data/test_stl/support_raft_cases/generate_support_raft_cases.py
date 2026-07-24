#!/usr/bin/env python3
"""Generate recognizable STL fixtures for support and raft/bed-adhesion scenarios."""

from __future__ import annotations

import math
from pathlib import Path


Vec3 = tuple[float, float, float]
Triangle = tuple[Vec3, Vec3, Vec3]


OUT_DIR = Path(__file__).resolve().parent
SEGMENTS = 72


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def fixed_normal(tri: Triangle) -> Vec3:
    n = cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))
    length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    if length == 0:
        return (0.0, 0.0, 0.0)
    return (n[0] / length, n[1] / length, n[2] / length)


def write_stl(path: Path, name: str, triangles: list[Triangle]) -> None:
    with path.open("w", encoding="ascii", newline="\n") as f:
        f.write(f"solid {name}\n")
        for tri in triangles:
            n = fixed_normal(tri)
            f.write(f"  facet normal {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")
            f.write("    outer loop\n")
            for v in tri:
                f.write(f"      vertex {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
            f.write("    endloop\n")
            f.write("  endfacet\n")
        f.write(f"endsolid {name}\n")


def add_quad(tris: list[Triangle], a: Vec3, b: Vec3, c: Vec3, d: Vec3) -> None:
    tris.append((a, b, c))
    tris.append((a, c, d))


def add_box(tris: list[Triangle], x0: float, x1: float, y0: float, y1: float, z0: float, z1: float) -> None:
    p000 = (x0, y0, z0)
    p100 = (x1, y0, z0)
    p110 = (x1, y1, z0)
    p010 = (x0, y1, z0)
    p001 = (x0, y0, z1)
    p101 = (x1, y0, z1)
    p111 = (x1, y1, z1)
    p011 = (x0, y1, z1)

    add_quad(tris, p000, p010, p110, p100)
    add_quad(tris, p001, p101, p111, p011)
    add_quad(tris, p000, p100, p101, p001)
    add_quad(tris, p100, p110, p111, p101)
    add_quad(tris, p110, p010, p011, p111)
    add_quad(tris, p010, p000, p001, p011)


def add_cylinder(
    tris: list[Triangle],
    radius: float,
    height: float,
    segments: int = SEGMENTS,
    cx: float = 0.0,
    cy: float = 0.0,
    z0: float = 0.0,
) -> None:
    add_frustum(tris, radius, radius, height, segments, cx, cy, z0)


def add_frustum(
    tris: list[Triangle],
    bottom_radius: float,
    top_radius: float,
    height: float,
    segments: int = SEGMENTS,
    cx: float = 0.0,
    cy: float = 0.0,
    z0: float = 0.0,
) -> None:
    z1 = z0 + height
    bottom_center = (cx, cy, z0)
    top_center = (cx, cy, z1)
    for i in range(segments):
        j = (i + 1) % segments
        a0 = 2.0 * math.pi * i / segments
        a1 = 2.0 * math.pi * j / segments
        b0 = (cx + bottom_radius * math.cos(a0), cy + bottom_radius * math.sin(a0), z0)
        b1 = (cx + bottom_radius * math.cos(a1), cy + bottom_radius * math.sin(a1), z0)
        t0 = (cx + top_radius * math.cos(a0), cy + top_radius * math.sin(a0), z1)
        t1 = (cx + top_radius * math.cos(a1), cy + top_radius * math.sin(a1), z1)
        tris.append((bottom_center, b1, b0))
        tris.append((top_center, t0, t1))
        add_quad(tris, b0, b1, t1, t0)


def add_gabled_roof(tris: list[Triangle], x0: float, x1: float, y0: float, y1: float, z0: float, z1: float) -> None:
    ridge_y = (y0 + y1) / 2.0
    left_front = (x0, y0, z0)
    right_front = (x1, y0, z0)
    left_back = (x0, y1, z0)
    right_back = (x1, y1, z0)
    ridge_front = (x0, ridge_y, z1)
    ridge_back = (x1, ridge_y, z1)
    add_quad(tris, left_front, right_front, ridge_back, ridge_front)
    add_quad(tris, right_back, left_back, ridge_front, ridge_back)
    tris.append((left_front, ridge_front, left_back))
    tris.append((right_front, right_back, ridge_back))


def add_triangular_prism_x(tris: list[Triangle], x0: float, x1: float, y0: float, y1: float, z0: float, z1: float) -> None:
    # Prism running along X, useful for awnings with a self-supporting triangular profile.
    ym = (y0 + y1) / 2.0
    a0 = (x0, y0, z0)
    b0 = (x0, y1, z0)
    c0 = (x0, ym, z1)
    a1 = (x1, y0, z0)
    b1 = (x1, y1, z0)
    c1 = (x1, ym, z1)
    tris.append((a0, c0, b0))
    tris.append((a1, b1, c1))
    add_quad(tris, a0, a1, c1, c0)
    add_quad(tris, b1, b0, c0, c1)
    add_quad(tris, b0, b1, a1, a0)


def add_nameplate_n(tris: list[Triangle], x: float, y: float, z: float, scale: float = 1.0) -> None:
    t = 0.9 * scale
    h = 8.0 * scale
    w = 6.0 * scale
    add_box(tris, x, x + t, y, y + t, z, z + h)
    add_box(tris, x + w - t, x + w, y, y + t, z, z + h)
    # Diagonal is approximated with staggered blocks; readable enough after slicing.
    for i in range(5):
        add_box(tris, x + t + i * (w - 2 * t) / 5.0, x + t + (i + 1) * (w - 2 * t) / 5.0, y, y + t, z + i * h / 5.0, z + (i + 2) * h / 5.0)


def add_nameplate_s(tris: list[Triangle], x: float, y: float, z: float, scale: float = 1.0) -> None:
    t = 0.9 * scale
    h = 8.0 * scale
    w = 6.0 * scale
    add_box(tris, x, x + w, y, y + t, z + h - t, z + h)
    add_box(tris, x, x + w, y, y + t, z + h / 2.0 - t / 2.0, z + h / 2.0 + t / 2.0)
    add_box(tris, x, x + w, y, y + t, z, z + t)
    add_box(tris, x, x + t, y, y + t, z + h / 2.0, z + h)
    add_box(tris, x + w - t, x + w, y, y + t, z, z + h / 2.0)


def normal_house() -> list[Triangle]:
    tris: list[Triangle] = []
    add_box(tris, -20.0, 20.0, -16.0, 16.0, 0.0, 3.0)
    add_box(tris, -15.0, 15.0, -12.0, 12.0, 3.0, 24.0)
    add_gabled_roof(tris, -18.0, 18.0, -15.0, 15.0, 24.0, 36.0)
    add_box(tris, -4.0, 4.0, -12.9, -12.0, 3.0, 15.0)
    add_box(tris, -13.0, -7.0, -12.9, -12.0, 14.0, 20.0)
    add_box(tris, 7.0, 13.0, -12.9, -12.0, 14.0, 20.0)
    add_box(tris, -13.0, -7.0, 12.0, 12.9, 14.0, 20.0)
    add_box(tris, 7.0, 13.0, 12.0, 12.9, 14.0, 20.0)
    add_box(tris, 8.0, 12.5, -2.5, 2.5, 36.0, 48.0)
    add_frustum(tris, 3.0, 2.0, 5.0, cx=10.25, cy=0.0, z0=48.0)
    add_nameplate_n(tris, -3.0, -17.2, 4.0, 0.9)
    return tris


def support_required_balcony() -> list[Triangle]:
    tris: list[Triangle] = []
    add_box(tris, -26.0, 26.0, -16.0, 16.0, 0.0, 3.0)
    for x in (-18.0, 18.0):
        for y in (-10.0, 10.0):
            add_frustum(tris, 2.7, 2.2, 30.0, cx=x, cy=y, z0=3.0)
    add_box(tris, -24.0, 24.0, -14.0, 14.0, 33.0, 39.0)
    add_box(tris, -42.0, -24.0, -8.0, 8.0, 33.0, 39.0)
    add_triangular_prism_x(tris, -24.0, 24.0, -16.0, 16.0, 39.0, 49.0)
    add_box(tris, -42.0, -24.0, -9.0, 9.0, 39.0, 45.0)
    add_box(tris, -38.0, -28.0, -9.8, -9.0, 35.0, 43.0)
    add_box(tris, -4.0, 4.0, -16.8, -16.0, 5.0, 13.0)
    add_nameplate_s(tris, -3.0, -17.2, 6.0, 0.9)
    return tris


def raft_recommended_needle_tower() -> list[Triangle]:
    tris: list[Triangle] = []
    add_cylinder(tris, 2.4, 12.0, z0=0.0)
    add_frustum(tris, 2.4, 2.0, 26.0, z0=12.0)
    add_frustum(tris, 2.0, 1.6, 22.0, z0=38.0)
    add_frustum(tris, 1.6, 0.45, 18.0, z0=60.0)
    # Slim vertical fins add recognizable detail without growing the bed footprint.
    add_box(tris, -0.5, 0.5, -2.8, -2.2, 18.0, 54.0)
    add_box(tris, -0.5, 0.5, 2.2, 2.8, 18.0, 54.0)
    add_box(tris, -2.8, -2.2, -0.5, 0.5, 18.0, 54.0)
    add_box(tris, 2.2, 2.8, -0.5, 0.5, 18.0, 54.0)
    return tris


def support_and_raft_signpost() -> list[Triangle]:
    tris: list[Triangle] = []
    add_cylinder(tris, 2.2, 58.0, z0=0.0)
    add_box(tris, -2.2, 45.0, -10.0, 10.0, 42.0, 51.0)
    add_triangular_prism_x(tris, 4.0, 45.0, -12.0, 12.0, 51.0, 60.0)
    add_box(tris, 34.0, 43.0, -11.0, 11.0, 35.0, 42.0)
    add_box(tris, 38.0, 43.0, -5.0, 5.0, 28.0, 35.0)
    add_box(tris, 8.0, 13.0, -10.8, -10.0, 45.0, 48.0)
    add_box(tris, 16.0, 21.0, -10.8, -10.0, 45.0, 48.0)
    add_box(tris, 24.0, 29.0, -10.8, -10.0, 45.0, 48.0)
    return tris


def main() -> None:
    for old in OUT_DIR.glob("*.stl"):
        old.unlink()
    cases = [
        ("normal_house.stl", "normal_house", normal_house()),
        ("support_required_balcony.stl", "support_required_balcony", support_required_balcony()),
        ("raft_recommended_needle_tower.stl", "raft_recommended_needle_tower", raft_recommended_needle_tower()),
        ("support_and_raft_signpost.stl", "support_and_raft_signpost", support_and_raft_signpost()),
    ]
    for filename, name, triangles in cases:
        write_stl(OUT_DIR / filename, name, triangles)


if __name__ == "__main__":
    main()
