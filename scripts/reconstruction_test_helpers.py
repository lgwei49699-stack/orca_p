"""Shared fixtures and artifact checks for reconstruction CLI tests."""

import hashlib
import json
import math
import struct

VERTICES = [(2., 2., 0.), (2., 0., 0.), (0., 0., 0.), (0., 2., 0.),
            (2., 2., 2.), (0., 2., 2.), (0., 0., 2.), (2., 0., 2.)]

FACES = [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7),
         (0, 4, 7), (0, 7, 1), (1, 7, 6), (1, 6, 2),
         (2, 6, 5), (2, 5, 3), (4, 0, 3), (4, 3, 5)]

def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")

def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()

def binary_stl(vertices=VERTICES, faces=FACES, *, attribute=0, header=b"Orca reconstruction regression"):
    data = bytearray(header[:80].ljust(80, b" ") + struct.pack("<I", len(faces)))
    for face in faces:
        xyz = [value for vertex in face for value in vertices[vertex]]
        data.extend(struct.pack("<12fH", 0., 0., 0., *xyz, attribute))
    return bytes(data)

def ascii_stl(name="cube", *, extra_line="", vertices=VERTICES, faces=FACES):
    lines = ["solid " + name]
    if extra_line:
        lines.append(extra_line)
    for face in faces:
        lines.extend(("  facet normal 0 0 0", "    outer loop"))
        lines.extend("      vertex " + " ".join(format(value, ".9g") for value in vertices[index]) for index in face)
        lines.extend(("    endloop", "  endfacet"))
    lines.append("endsolid " + name)
    return ("\n".join(lines) + "\n").encode("utf-8")

def extract_report(text):
    decoder = json.JSONDecoder()
    for index, character in enumerate(text):
        if character != "{":
            continue
        try:
            value, _ = decoder.raw_decode(text[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and value.get("schema") == "model_reconstruction_v1":
            return value
    return None

def check_report_contract(report):
    assert report.get("schema") == "model_reconstruction_v1", report
    assert report.get("trial_slice") == "not_run", report
    assert report.get("print_ready") is False, report
    assert report.get("applied") is False, report
    assert report.get("shape_confirmation") == "not_confirmed", report
    assert report.get("requires_manual_confirmation") is True, report
    assert isinstance(report.get("message"), str) and report["message"], report

def check_candidate_file(path):
    assert path.is_file(), "No candidate STL was exported"
    data = path.read_bytes()
    assert len(data) >= 84, "Truncated candidate STL"
    faces = struct.unpack_from("<I", data, 80)[0]
    assert 0 < faces <= 1000000 and len(data) == 84 + faces * 50, "Invalid candidate STL size/count"
    for offset in range(84, len(data), 50):
        values = struct.unpack_from("<12fH", data, offset)
        assert all(math.isfinite(value) for value in values[:12]), "Candidate contains non-finite numbers"
    return {"triangles": faces, "sha256": digest(path)}


def directory_snapshot(path):
    return {str(item.relative_to(path)): digest(item)
            for item in sorted(path.rglob("*")) if item.is_file()}
