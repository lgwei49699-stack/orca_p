#!/usr/bin/env python3
"""Prepare immutable MeshLib dylibs before CMake hashes them; audit completed apps.

Only local ad-hoc MeshLib packages are supported. Never edit the source SDK or
re-sign its prepared copies after the adapter's runtime identity is compiled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


MANIFEST = "meshlib-runtime.json"
ROOTS = ("libMRMesh.dylib", "libMRVoxels.dylib")
LOAD_COMMANDS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"}


def run(*args):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def version(value):
    if not re.fullmatch(r"\d+(\.\d+){0,2}", value):
        raise RuntimeError(f"Invalid macOS version: {value}")
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def inside(path, directory):
    return path.resolve().is_relative_to(directory.resolve())


def system_path(value):
    return value.startswith(("/System/Library/", "/usr/lib/"))


def inspect(path, arch):
    """Read each slice's load commands, excluding its own LC_ID_DYLIB."""
    arches = run("lipo", "-archs", path).split()
    if arch not in arches:
        raise RuntimeError(f"Missing architecture {arch}: {path} ({' '.join(arches)})")
    data = {"deps": [], "rpaths": [], "minimum": None}
    command = None
    for line in run("otool", "-arch", arch, "-l", path).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line.split()[1]
        elif command in LOAD_COMMANDS and line.startswith("name "):
            data["deps"].append(line[5:].rsplit(" (offset ", 1)[0])
        elif command == "LC_RPATH" and line.startswith("path "):
            data["rpaths"].append(line[5:].rsplit(" (offset ", 1)[0])
        elif command == "LC_BUILD_VERSION" and line.startswith("minos "):
            data["minimum"] = line.split()[1]
        elif command == "LC_VERSION_MIN_MACOSX" and line.startswith("version "):
            data["minimum"] = line.split()[1]
    if data["minimum"] is None:
        raise RuntimeError(f"Missing macOS deployment version: {path}")
    return data


def check_minimum(path, metadata, minimum):
    if version(metadata["minimum"]) > version(minimum):
        raise RuntimeError(f"{path} requires macOS {metadata['minimum']}, app targets {minimum}")


def prepare(sdk, output, minimum):
    if version(minimum) < version("14.0"):
        raise RuntimeError("MeshLib requires macOS 14.0 or newer")
    source = (sdk / "lib").resolve()
    if inside(output, sdk) or inside(sdk, output):
        raise RuntimeError("MeshLib staging and source SDK must be separate directories")
    pending = [source / name for name in ROOTS]
    libraries = {}
    while pending:
        path = pending.pop()
        if not path.is_file() or not inside(path, source):
            raise RuntimeError(f"Missing or external SDK dependency: {path}")
        name = path.name
        if name in libraries:
            if libraries[name][0].resolve() != path.resolve():
                raise RuntimeError(f"Ambiguous SDK library name: {name}")
            continue
        metadata = inspect(path, "arm64")
        check_minimum(path, metadata, minimum)
        libraries[name] = (path, metadata)
        for dependency in metadata["deps"]:
            if system_path(dependency):
                continue
            if dependency.startswith("@rpath/"):
                target = source / dependency[len("@rpath/"):]
            elif dependency.startswith("@loader_path/"):
                target = path.parent / dependency[len("@loader_path/"):]
            else:
                target = Path(dependency)
                if not target.is_absolute():
                    raise RuntimeError(f"Unsupported SDK dependency: {dependency}")
            pending.append(target)
    inputs = {"format": 1, "tool": sha(Path(__file__)), "minimum": minimum,
              "architecture": "arm64", "signing": "ad-hoc",
              "sources": {name: sha(path) for name, (path, _) in sorted(libraries.items())}}
    manifest_path = output / MANIFEST
    if manifest_path.is_file():
        old = json.loads(manifest_path.read_text())
        if old.get("inputs") == inputs:
            verify_manifest(output)
            print(f"MeshLib runtime unchanged: {output}")
            return
    elif output.exists():
        raise RuntimeError(f"Refusing to replace unrecognized staging directory: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="meshlib-prepare-", dir=output.parent) as temporary:
        stage = Path(temporary) / "runtime"
        stage.mkdir()
        for name, (path, metadata) in sorted(libraries.items()):
            dest = stage / name
            shutil.copyfile(path, dest)
            dest.chmod(0o755)
            args = ["install_name_tool", "-id", f"@rpath/MeshLib/{name}"]
            for dependency in metadata["deps"]:
                if not system_path(dependency):
                    args += ["-change", dependency, "@loader_path/" + Path(dependency).name]
            for rpath in dict.fromkeys(metadata["rpaths"]):
                args += ["-delete_rpath", rpath]
            run(*args, dest)
            run("codesign", "--force", "--sign", "-", dest)
        manifest = {"inputs": inputs, "libraries": {name: sha(stage / name) for name in sorted(libraries)}}
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n")
        if output.exists():
            shutil.rmtree(output)
        stage.rename(output)
    print(f"Prepared {len(libraries)} MeshLib runtime libraries: {output}")


def verify_manifest(directory, manifest_path=None):
    manifest = json.loads((manifest_path or directory / MANIFEST).read_text())
    libraries = manifest["libraries"]
    if not all(name in libraries for name in ROOTS):
        raise RuntimeError("MeshLib manifest is missing its root libraries")
    for name, expected in libraries.items():
        if Path(name).name != name or not name.endswith(".dylib"):
            raise RuntimeError(f"Invalid MeshLib manifest filename: {name}")
        path = directory / name
        if not path.is_file() or path.is_symlink() or sha(path) != expected:
            raise RuntimeError(f"MeshLib runtime hash mismatch: {path}; reconfigure and rebuild the app")
    return manifest


def executable(app):
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    name = info["CFBundleExecutable"]
    if not name or Path(name).name != name:
        raise RuntimeError("Invalid CFBundleExecutable")
    return app / "Contents/MacOS" / name


def expand(value, loader, main):
    for prefix, directory in (("@loader_path", loader.parent), ("@executable_path", main.parent)):
        if value == prefix or value.startswith(prefix + "/"):
            return (directory / value[len(prefix):].lstrip("/")).resolve()
    if value.startswith("/"):
        return Path(value).resolve()
    raise RuntimeError(f"Unsupported runtime path: {value}")


def bundle(app, stage):
    main = executable(app)
    metadata = inspect(main, "arm64")
    if not any(Path(dep).name in ROOTS for dep in metadata["deps"]):
        return  # The profile validator may not link the optional adapter.
    verify_manifest(stage)
    # A compiled absolute/original SDK reference would not carry staged hashes.
    if any(dep != f"@rpath/MeshLib/{Path(dep).name}"
           for dep in metadata["deps"] if Path(dep).name in ROOTS):
        raise RuntimeError("App was linked against the original SDK; reconfigure and rebuild before packaging")
    dest = app / "Contents/Frameworks/MeshLib"
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(stage, dest, ignore=shutil.ignore_patterns(MANIFEST))
    resources = app / "Contents/Resources"
    resources.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(stage / MANIFEST, resources / MANIFEST)
    args = ["install_name_tool"]
    for rpath in dict.fromkeys(metadata["rpaths"]):
        if not inside(expand(rpath, main, main), app):
            args += ["-delete_rpath", rpath]
    if "@executable_path/../Frameworks" not in metadata["rpaths"]:
        args += ["-add_rpath", "@executable_path/../Frameworks"]
    if len(args) > 1:
        run(*args, main)


def audit(app):
    main = executable(app)
    meshlib = app / "Contents/Frameworks/MeshLib"
    if meshlib.exists():
        verify_manifest(meshlib, app / "Contents/Resources" / MANIFEST)
    count = 0
    for arch in run("lipo", "-archs", main).split():
        minimum = inspect(main, arch)["minimum"]
        visited = set()
        pending = [(main, ())]
        while pending:
            path, inherited = pending.pop()
            path = path.resolve()
            if path in visited:
                continue
            if not inside(path, app) or not path.is_file():
                raise RuntimeError(f"Dependency escapes app or is missing: {path}")
            visited.add(path)
            metadata = inspect(path, arch)
            check_minimum(path, metadata, minimum)
            rpaths = tuple(expand(value, path, main) for value in metadata["rpaths"])
            for rpath in rpaths:
                if not inside(rpath, app):
                    raise RuntimeError(f"External RPATH in {path}: {rpath}")
            search = rpaths + inherited
            for dep in metadata["deps"]:
                if system_path(dep):
                    continue
                if Path(dep).name in ROOTS and not meshlib.exists():
                    raise RuntimeError("MeshLib is not bundled; reconfigure and rebuild the app first")
                if dep.startswith("@rpath/"):
                    candidates = [directory / dep[len("@rpath/"):] for directory in search]
                    target = next((p for p in candidates if p.is_file()), None)
                    if target is None:
                        raise RuntimeError(f"Unresolved dependency in {path}: {dep}")
                else:
                    if dep.startswith("/"):
                        raise RuntimeError(f"External absolute dependency in {path}: {dep}")
                    target = expand(dep, path, main)
                pending.append((target, search))
        count += len(visited)
    print(f"Runtime audit passed: {app} ({count} Mach-O slices)")


def finalize(app, identity):
    main = executable(app)
    # Discard build-only search paths after explicit dependencies have been
    # bundled. Missing @rpath dependencies must then fail the audit below.
    rpaths = set()
    minimums = []
    for arch in run("lipo", "-archs", main).split():
        metadata = inspect(main, arch)
        rpaths.update(metadata["rpaths"])
        minimums.append(metadata["minimum"])
    external = [value for value in sorted(rpaths) if not inside(expand(value, main, main), app)]
    if external:
        args = ["install_name_tool"]
        for value in external:
            args += ["-delete_rpath", value]
        run(*args, main)
    audit(app)
    info_path = app / "Contents/Info.plist"
    info_bytes = info_path.read_bytes()
    info = plistlib.loads(info_bytes)
    info["LSMinimumSystemVersion"] = max(minimums, key=version)
    info_path.write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY if info_bytes.startswith(b"bplist") else plistlib.FMT_XML))
    meshlib = app / "Contents/Frameworks/MeshLib"
    args = ["codesign", "--force", "--sign", identity]
    if meshlib.exists():
        if identity != "-":
            raise RuntimeError("MeshLib local packages currently require ad-hoc signing; Developer ID needs a separate signed-runtime build")
        # Nested libraries are already signed and hashed by the adapter. --deep
        # would rewrite them and invalidate the identity compiled into the app.
    else:
        args += ["--deep"]
    if identity != "-":
        args += ["--options", "runtime", "--timestamp"]
    run(*args, app)
    run("codesign", "--verify", "--deep", "--strict", app)
    if meshlib.exists():
        verify_manifest(meshlib, app / "Contents/Resources" / MANIFEST)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    preparation = commands.add_parser("prepare-meshlib")
    preparation.add_argument("--sdk", required=True, type=Path)
    preparation.add_argument("--output", required=True, type=Path)
    preparation.add_argument("--minimum", required=True)
    for name in ("bundle-meshlib", "audit", "finalize"):
        command = commands.add_parser(name)
        command.add_argument("--app", required=True, type=Path)
        if name == "bundle-meshlib":
            command.add_argument("--stage", required=True, type=Path)
        if name == "finalize":
            command.add_argument("--identity", default="-")
    args = parser.parse_args()
    try:
        if sys.platform != "darwin":
            raise RuntimeError("macOS runtime packaging requires macOS")
        if args.command == "prepare-meshlib":
            prepare(args.sdk.resolve(), args.output.resolve(), args.minimum)
        elif args.command == "bundle-meshlib":
            bundle(args.app.resolve(), args.stage.resolve())
        elif args.command == "audit":
            audit(args.app.resolve())
        else:
            finalize(args.app.resolve(), args.identity)
    except (RuntimeError, OSError, ValueError, KeyError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    main()
