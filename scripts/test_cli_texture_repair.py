#!/usr/bin/env python3
"""Exercise texture repair with an actual CLI in isolated directories; no printer or presets required."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def write_glb(path, faces):
    """Small textured tetrahedron with two material colors; faces may deliberately be missing."""
    blob = bytearray()
    views, accessors = [], []

    def view(data):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        blob.extend(data)
        return len(views) - 1

    def accessor(data, component, count, kind, **bounds):
        accessors.append({"bufferView": view(data), "componentType": component, "count": count, "type": kind, **bounds})
        return len(accessors) - 1

    vertices = [(0, 0, 0), (10, 0, 0), (0, 10, 0), (0, 0, 10)]
    pos = accessor(struct.pack("<12f", *(v for p in vertices for v in p)), 5126, 4, "VEC3", min=[0, 0, 0], max=[10, 10, 10])
    uv = accessor(struct.pack("<8f", *([0.5] * 8)), 5126, 4, "VEC2")
    primitives = []
    for material, group in enumerate((faces[:2], faces[2:])):
        if not group:
            continue
        indices = [i for face in group for i in face]
        index = accessor(struct.pack(f"<{len(indices)}H", *indices), 5123, len(indices), "SCALAR")
        primitives.append({"attributes": {"POSITION": pos, "TEXCOORD_0": uv}, "indices": index, "material": material})

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", 2, 2, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress((b"\x00" + b"\xff" * 6) * 2)) + chunk(b"IEND", b""))
    image_view = view(png)
    model = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": primitives}], "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views, "accessors": accessors,
        "images": [{"bufferView": image_view, "mimeType": "image/png"}], "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": color, "baseColorTexture": {"index": 0}}}
                      for color in ([1, 0, 0, 1], [0, 0, 1, 1])],
    }
    header = json.dumps(model).encode()
    header += b" " * (-len(header) % 4)
    blob += b"\x00" * (-len(blob) % 4)
    data = struct.pack("<I4s", len(header), b"JSON") + header + struct.pack("<I4s", len(blob), b"BIN\x00") + blob
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(data)) + data)


def open_edges(path):
    vertices, faces = [], []
    for line in path.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields and fields[0] == "f":
            faces.append([vertices[int(item.split("/")[0]) - 1] for item in fields[1:]])
    assert faces, "Export has no faces"
    edges = Counter(tuple(sorted((face[i], face[(i + 1) % 3]))) for face in faces for i in range(3))
    return sum(count != 2 for count in edges.values())


def colored_surface(path):
    """Compare oriented triangles and RGB, independent of export vertex/material numbering."""
    colors = {}
    current = None
    for line in path.with_suffix(".mtl").read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == "newmtl":
            current = fields[1]
        elif fields and fields[0] == "Kd":
            colors[current] = tuple(map(float, fields[1:4]))
    vertices, surface = [], Counter()
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "usemtl":
            current = colors[fields[1]]
        elif fields[0] == "f":
            face = tuple(vertices[int(value.split("/")[0]) - 1] for value in fields[1:])
            canonical = min(face, face[1:] + face[:1], face[2:] + face[:2])
            surface[canonical, current] += 1
    assert surface
    return surface


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="orca-texture-repair-"))
    print(f"Evidence directory: {work}", flush=True)
    faces = [(0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)]
    for name, source_faces in (("healthy", faces), ("open", faces[:3]), ("plane", faces[:1])):
        write_glb(work / f"{name}.glb", source_faces)
    source_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in work.glob("*.glb")}
    results = []

    def run(name, source, flags, success, export=True):
        case = work / name
        case.mkdir()
        output = case / "output"
        command = [str(binary), "--datadir", str(case / "data"), "--outputdir", str(output)]
        if export:
            command += ["--export-texture-obj", "result.obj", "--texture-color-count", "2",
                        "--texture-smooth", "0", "--texture-oversampling", "1"]
        command += [*flags, str(work / f"{source}.glb")]
        (case / "command.json").write_text(json.dumps(command, indent=2))
        with (case / "run.log").open("wb") as log:
            result = subprocess.run(command, cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
        assert result.returncode >= 0, f"{name}: crashed ({result.returncode})"
        assert (result.returncode == 0) == success, f"{name}: see {case / 'run.log'}"
        if not success:
            report = json.loads((output / "result.json").read_text())
            assert report["return_code"] != 0
            assert not list(output.glob("*.obj")) and not list(output.glob("*.mtl"))
        results.append({"name": name, "exit_code": result.returncode})
        return output / "result.obj"

    healthy = run("healthy-default", "healthy", [], True)
    repaired_healthy = run("healthy-repair", "healthy", ["--texture-repair"], True)
    assert colored_surface(healthy) == colored_surface(repaired_healthy), "Healthy colored surface changed after opting into repair"
    assert open_edges(healthy) == 0
    skipped = run("open-skip", "open", [], True)
    assert open_edges(skipped) > 0, "Default import must not silently close intentional openings"
    repaired = run("open-repair", "open", ["--texture-repair"], True)
    assert open_edges(repaired) == 0, "Explicit repair did not close the opening"
    colors = [tuple(map(float, line.split()[1:4])) for line in repaired.with_suffix(".mtl").read_text().splitlines()
              if line.startswith("Kd ")]
    assert any(r > 0.99 and g < 0.01 and b < 0.01 for r, g, b in colors), colors
    assert any(b > 0.99 and r < 0.01 and g < 0.01 for r, g, b in colors), colors
    run("plane-repair-fails", "plane", ["--texture-repair"], False)
    run("requires-texture-export", "open", ["--texture-repair", "--export-stl"], False, export=False)
    run("rejects-two-repair-entries", "open", ["--texture-repair", "--repair-model", "cgal"], False)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == expected for p, expected in source_hashes.items())
    (work / "results.json").write_text(json.dumps(results, indent=2))
    print(f"PASS: {len(results)} CLI scenarios, source preservation, topology and color assertions", flush=True)


if __name__ == "__main__":
    main()
