#!/usr/bin/env python3
"""Import and round-trip named plates with an already built CLI; never builds or slices.

Supply a loadable model 3MF (preferably with two or more plates). Copies only
change plate names; profiles and geometry stay intact. The input is not modified.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile


CASES = {"blank": lambda i: "", "ascii": lambda i: f"Plate {i + 1}",
         "unicode": lambda i: f'测试盘 & "{i + 1}"', "duplicate": lambda i: "Same name"}


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_plates(path):
    with zipfile.ZipFile(path) as archive:
        settings = ET.fromstring(archive.read("Metadata/model_settings.config"))
    return settings.findall("plate")


def named_copy(source, destination, names):
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
        for entry in original.infolist():
            data = original.read(entry.filename)
            if entry.filename == "Metadata/model_settings.config":
                settings = ET.fromstring(data)
                plates = settings.findall("plate")
                assert len(plates) == len(names)
                for plate, name in zip(plates, names):
                    node = plate.find("metadata[@key='plater_name']")
                    if node is None:
                        node = ET.SubElement(plate, "metadata", key="plater_name")
                    node.set("value", name)
                data = ET.tostring(settings, encoding="utf-8", xml_declaration=True)
            output.writestr(entry, data)


def check_project(path, names, object_counts):
    plates = read_plates(path)
    assert len(plates) == len(names), "Plate count changed"
    for plate, expected, count in zip(plates, names, object_counts):
        metadata = {node.get("key"): node.get("value") for node in plate.findall("metadata")}
        assert metadata.get("plater_name", "") == expected, metadata
        assert len(plate.findall("model_instance")) == count, "Object plate membership changed"


def normalize_transforms(report):
    transforms = report.get("model_transforms", [])
    assert transforms, "No model transforms returned (sliced-only 3MF is not a model fixture)"
    return sorted((item["object_name"], item["plate_index"],
                   *(tuple(round(value, 5) for value in item[key]) for key in ("position", "rotation", "scale")))
                  for item in transforms)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--case", choices=CASES, action="append", help="Blank control always runs first; default: all cases")
    parser.add_argument("--outputdir", type=Path, help="New directory for fixtures, isolated data and results")
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    binary, project = args.binary.resolve(strict=True), args.project.resolve(strict=True)
    plates = read_plates(project)
    if not plates:
        parser.error("Project must contain model plates")
    object_counts = [len(plate.findall("model_instance")) for plate in plates]
    if not sum(object_counts):
        parser.error("Use a model project, not a sliced-only G-code 3MF")
    if args.outputdir:
        work = args.outputdir.resolve()
        work.mkdir(parents=True, exist_ok=False)
    else:
        work = Path(tempfile.mkdtemp(prefix="orca-cli-plate-names-"))
    print(f"Evidence directory: {work}", flush=True)
    results, baseline = [], None
    cases = list(dict.fromkeys(["blank", *(args.case or CASES)]))
    for case in cases:
        names = [CASES[case](i) for i in range(len(plates))]
        source = work / f"{case}.3mf"
        named_copy(project, source, names)
        check_project(source, names, object_counts)
        try:
            for stage in ("import", "reopen"):
                run = work / f"{case}-{stage}"
                run.mkdir()
                (run / "data").mkdir()
                output = run / "output"
                output.mkdir()
                command = [str(binary), "--debug", "3", "--datadir", str(run / "data"), "--outputdir", str(output),
                           "--info", "--export-model-transforms", "--export-3mf", "roundtrip.3mf", str(source)]
                save_json(run / "command.json", command)
                with (run / "run.log").open("wb") as log:
                    process = subprocess.run(command, cwd=run, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                save_json(run / "exit.json", {"return_code": process.returncode})
                assert process.returncode == 0, f"{stage} process returned {process.returncode} (negative means signal)"
                report = json.loads((output / "result.json").read_text(encoding="utf-8"))
                assert report["return_code"] == 0, report
                transforms = normalize_transforms(report)
                assert len(transforms) == sum(object_counts), "Model count changed"
                if baseline is None:
                    baseline = transforms
                assert transforms == baseline, "Names changed model placement, scale or plate membership"
                source = output / "roundtrip.3mf"
                check_project(source, names, object_counts)
            results.append({"case": case, "passed": True})
        except (AssertionError, OSError, ValueError, KeyError, ET.ParseError, zipfile.BadZipFile, subprocess.TimeoutExpired) as error:
            results.append({"case": case, "passed": False, "error": str(error)})
        print(results[-1], flush=True)
        if case == "blank" and not results[-1]["passed"]:
            break  # A broken input/control cannot validate name handling.
    save_json(work / "validation.json", {"binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                                        "project": str(project), "project_sha256": hashlib.sha256(project.read_bytes()).hexdigest(),
                                        "cases": results, "built_by_script": False})
    return 0 if all(item["passed"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
