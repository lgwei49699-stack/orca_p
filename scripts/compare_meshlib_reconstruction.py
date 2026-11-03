#!/usr/bin/env python3
"""Compare two local Orca executables on identical STLs. The first model runs A/B/B/A."""

import argparse
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

from reconstruction_test_helpers import check_candidate_file, digest, save_json
from test_cli_model_check import ISSUE_KEYS


def reconstruct(binary, model, expected_input, output):
    command = [str(binary), "--reconstruct-model", str(model), "--reconstruction-voxel-mm", "0.1",
               "--reconstruction-memory-mb", "2048", "--reconstruction-timeout", "180",
               "--accept-reconstruction-risk", "--outputdir", str(output)]
    log = output.with_suffix(".log")
    with log.open("w", encoding="utf-8") as stream:
        started = time.monotonic()
        subprocess.run(command, cwd=output.parent, stdout=stream, stderr=subprocess.STDOUT, timeout=240, check=True)
        seconds = time.monotonic() - started
    report = json.loads((output / "reconstruction.json").read_text(encoding="utf-8"))
    if (report["status"] != "candidate_ready" or report["mesh_validated"] is not True
            or report["applied"] is not False or report["export_round_trip"] != "passed"):
        raise ValueError(f"{output}: candidate validation failed")
    issues = report["engine"]["final_mesh_check"]["inspection"]["issues"]
    if set(issues) != ISSUE_KEYS or any(issue["complete"] is not True or issue["count"] != 0 for issue in issues.values()):
        raise ValueError(f"{output}: candidate has incomplete or nonzero mesh checks")
    candidate = check_candidate_file(output / "candidate.stl")
    if report["artifacts"]["input_sha256"] != expected_input or report["artifacts"]["candidate_sha256"] != candidate["sha256"]:
        raise ValueError(f"{output}: input or candidate file does not match the report")
    sdk = report["engine"]["sdk"]
    if report["backend"] != "meshlib" or sdk["runtime_identity_verified"] is not True:
        raise ValueError(f"{output}: MeshLib runtime identity was not verified")
    return {"command": command, "wall_seconds": seconds, "sha256": candidate["sha256"],
            "sdk_version": report["sdk_version"], "settings": sdk["parameters"],
            "libraries": {lib["name"]: lib["sha256"] for lib in sdk["runtime_libraries"]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="New directory for logs, candidates and summary.json")
    parser.add_argument("models", type=Path, nargs="+", help="First STL for timing, followed by geometry controls")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    binaries = {"A": args.baseline.resolve(strict=True), "B": args.candidate.resolve(strict=True)}
    # Extracted Linux AppImages can have identical AppRun scripts but different executables.
    identities = {}
    for label, binary in binaries.items():
        core = binary.parent / "bin" / "orca-slicer" if binary.name == "AppRun" else binary
        identities[label] = {"path": str(binary), "core": str(core), "sha256": digest(core)}
    if identities["A"]["sha256"] == identities["B"]["sha256"]:
        parser.error("baseline and candidate have identical executable bytes")
    models = [p.resolve(strict=True) for p in args.models]
    if output.exists():
        parser.error("output directory already exists; use a new evidence directory")
    output.mkdir(parents=True, exist_ok=False)
    summary = {"platform": platform.platform(), "binaries": identities, "models": [], "passed": False}
    try:
        for i, model in enumerate(models):
            input_hash = digest(model)
            case = {"path": str(model), "input_sha256": input_hash, "runs": []}
            summary["models"].append(case)
            for n, label in enumerate("ABBA" if i == 0 else "AB"):
                dest = output / f"{i+1:02d}-{n+1}-{label}"
                run = reconstruct(binaries[label], model, input_hash, dest)
                case["runs"].append(dict(run, binary=label))
                first = case["runs"][0]
                for key in ("sha256", "settings", "sdk_version", "libraries"):
                    if run[key] != first[key]:
                        raise ValueError(f"{model.name}: {key} differs at {dest.name}")
                print(f"{model.name} {label}: {run['wall_seconds']:.2f} s", flush=True)
            if digest(model) != input_hash:
                raise ValueError(f"{model.name}: source file changed")
            means = {k: statistics.mean(v["wall_seconds"] for v in case["runs"] if v["binary"] == k) for k in binaries}
            case["mean_seconds"] = means
            case["wall_reduction_percent"] = 100 * (1 - means["B"] / means["A"])
        summary["passed"] = True
    except (OSError, ValueError, KeyError, AssertionError, subprocess.SubprocessError) as error:
        summary["error"] = str(error)
        print(error, file=sys.stderr)
    finally:
        save_json(output / "summary.json", summary)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
