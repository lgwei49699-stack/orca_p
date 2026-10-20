#!/usr/bin/env python3
"""Bundle the MeshLib ELF closure without changing either hashed MR library.

Linux only; requires readelf, patchelf, ldconfig and ldd. The SDK is never installed or
modified. glibc and the compiler runtime remain system dependencies; other
DT_NEEDED libraries are copied from the selected SDK or the build host's
system library cache. Relocate the application and non-MR dependencies that have
their own search paths; verify the final extracted AppImage again before release.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT_LIBRARIES = ("libMRMesh.so", "libMRVoxels.so")
MANIFEST = "resources/meshlib-runtime.json"
RUNTIME = "lib/meshlib"
# Do not distribute another glibc, loader or libstdc++ alongside the target OS.
# Everything else in the MeshLib dependency closure must be present in the
# package, even when it was supplied by an Ubuntu build dependency.
SYSTEM_LIBRARIES = frozenset((
    "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1", "libc.so.6", "libm.so.6",
    "libdl.so.2", "libpthread.so.0", "librt.so.1", "libresolv.so.2", "libutil.so.1",
    "libgcc_s.so.1", "libstdc++.so.6", "libatomic.so.1",
))
MACHINES = {"Advanced Micro Devices X86-64": "x86_64", "AArch64": "aarch64"}


def run(*arguments, env=None):
    environment = dict(os.environ if env is None else env, LC_ALL="C")
    process = subprocess.run([str(argument) for argument in arguments], env=environment,
                             text=True, capture_output=True, check=False)
    if process.returncode:
        raise RuntimeError(f"{arguments[0]} failed: {process.stderr.strip() or process.stdout.strip()}")
    return process.stdout + (process.stderr if arguments[0] == "ldd" else "")


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def system_path(path):
    return any(path.resolve().is_relative_to(root) for root in
               (Path("/lib"), Path("/lib64"), Path("/usr/lib"), Path("/usr/lib64")))


def parse_elf(output, path, allow_build_rpath=False):
    elf_class = re.search(r"^\s*Class:\s*(\S+)", output, re.MULTILINE)
    machine = re.search(r"^\s*Machine:\s*(.+)$", output, re.MULTILINE)
    if not elf_class or elf_class[1] != "ELF64" or not machine or machine[1].strip() not in MACHINES:
        raise RuntimeError(f"Expected a supported 64-bit Linux ELF: {path}")
    data = {"architecture": MACHINES[machine[1].strip()], "needed": [], "soname": None}
    for tag, value in re.findall(r"\((NEEDED|SONAME|RPATH|RUNPATH)\).*?\[([^\]]*)\]", output):
        if tag in ("NEEDED", "SONAME"):
            if not value or "/" in value or value in (".", ".."):
                raise RuntimeError(f"Non-relocatable {tag} {value!r} in {path}; prepare SDK copies before configuring Orca")
            if tag == "NEEDED":
                data["needed"].append(value)
            else:
                data["soname"] = value
        elif tag == "RPATH":
            data["rpath"] = value
            # Unlike RUNPATH, old DT_RPATH can override LD_LIBRARY_PATH and
            # unexpectedly load a different SDK before the package's libraries.
            for entry in value.split(":"):
                if (not allow_build_rpath and not entry.startswith(("$ORIGIN", "${ORIGIN}"))
                        and not (entry and system_path(Path(entry)))):
                    raise RuntimeError(f"Non-relocatable DT_RPATH {entry!r} in {path}; prepare SDK copies before configuring Orca")
        elif tag == "RUNPATH":
            data["runpath"] = value
    return data


def inspect(path, allow_build_rpath=False):
    return parse_elf(run("readelf", "--wide", "--file-header", "--dynamic", path), path, allow_build_rpath)


def system_cache():
    result = {}
    for line in run("ldconfig", "-p").splitlines():
        match = re.match(r"\s*(\S+)\s+\([^)]*\)\s+=>\s+(/.+)$", line)
        if match:
            path = Path(match[2])
            if path.is_file() and system_path(path):
                result.setdefault(match[1], []).append(path)
    return result


def resolve_dependency(name, directories, cache, architecture):
    # Prefer the exact selected SDK. Do not search PATH or arbitrary directories
    # from the caller's LD_LIBRARY_PATH for another installation.
    for directory in directories:
        candidate = directory / name
        if candidate.is_file():
            return candidate.resolve()
    for candidate in cache.get(name, []):
        if inspect(candidate, allow_build_rpath=True)["architecture"] == architecture:
            return candidate.resolve()
    raise RuntimeError(f"Missing MeshLib runtime dependency: {name}")


def collect(runtime, sdk_prefix, architecture):
    directories = (runtime, runtime / "lib", sdk_prefix / "lib")
    cache = system_cache()
    pending = [(name, runtime / name) for name in ROOT_LIBRARIES]
    libraries, aliases, external = {}, {}, set()
    while pending:
        alias, source = pending.pop()
        source = source.resolve(strict=True)
        metadata = inspect(source, allow_build_rpath=alias not in ROOT_LIBRARIES)
        if metadata["architecture"] != architecture:
            raise RuntimeError(f"Architecture mismatch: {source}")
        name = source.name
        names = {alias, name}
        if metadata["soname"]:
            names.add(metadata["soname"])
        for entry in names:
            if entry in aliases and aliases[entry] != name:
                raise RuntimeError(f"Conflicting runtime SONAME: {entry}")
            aliases[entry] = name
        checksum = digest(source)
        if name in libraries:
            if libraries[name]["sha256"] != checksum:
                raise RuntimeError(f"Different libraries use the same filename: {name}")
            continue
        libraries[name] = {**metadata, "sha256": checksum, "source": str(source)}
        for needed in metadata["needed"]:
            if needed in SYSTEM_LIBRARIES:
                external.add(needed)
            else:
                pending.append((needed, resolve_dependency(needed, directories, cache, architecture)))
    return libraries, aliases, sorted(external)


def copy_licenses(sdk_prefix, package, libraries):
    destination = package / "resources/licenses/MeshLib"
    destination.mkdir(parents=True, exist_ok=True)
    notice = sdk_prefix / "share/MeshLib/THIRD-PARTY-NOTICES.txt"
    shutil.copyfile(notice, destination / notice.name)
    for metadata in libraries.values():
        source = Path(metadata["source"])
        if not system_path(source):
            continue
        try:
            ownership = run("dpkg-query", "-S", source).splitlines()[0].split(": ", 1)[0]
            package_name = ownership.split(":", 1)[0]
            copyright_file = Path("/usr/share/doc") / package_name / "copyright"
            shutil.copyfile(copyright_file, destination / (package_name + "-copyright"))
        except (OSError, RuntimeError, IndexError) as error:
            print(f"Warning: could not copy system dependency copyright for {source}: {error}", file=sys.stderr)
    return {str(path.relative_to(package)): digest(path) for path in sorted(destination.iterdir())}


def bundle(executable, sdk_prefix, runtime):
    executable = executable.resolve(strict=True)
    sdk_prefix, runtime = sdk_prefix.resolve(strict=True), runtime.resolve(strict=True)
    package = executable.parent.parent
    destination = package / RUNTIME
    if executable.parent.name != "bin":
        raise RuntimeError("The packaged executable must be in <package>/bin")
    if destination.exists() or destination.is_symlink() or (package / MANIFEST).exists():
        raise RuntimeError("MeshLib package destination already exists; rebuild the package directory")
    if package.is_relative_to(sdk_prefix) or sdk_prefix.is_relative_to(package):
        raise RuntimeError("Package and SDK must be separate directories")
    architecture = inspect(executable, allow_build_rpath=True)["architecture"]
    libraries, aliases, external = collect(runtime, sdk_prefix, architecture)
    destination.mkdir(parents=True)
    protected = {aliases[name] for name in ROOT_LIBRARIES}
    for name, metadata in sorted(libraries.items()):
        target = destination / name
        shutil.copyfile(metadata["source"], target)
        target.chmod(0o755)
        metadata["source_sha256"] = metadata["sha256"]
        # A child's RUNPATH disables inherited executable RPATH lookup for its
        # own dependencies (the official OpenVDB contains /home/MeshLib/lib).
        # Relocate only copies of non-MR libraries; the adapter hashes MR bytes.
        if name not in protected and ("runpath" in metadata or "rpath" in metadata):
            run("patchelf", "--force-rpath", "--set-rpath", "$ORIGIN", target)
            metadata["sha256"] = digest(target)
    for alias, name in sorted(aliases.items()):
        if alias != name:
            (destination / alias).symlink_to(name)
    # Deployment also invokes bin/orca-slicer directly, bypassing AppRun.
    # Only edit the packaged application, never either hashed MR library.
    run("patchelf", "--force-rpath", "--set-rpath", "$ORIGIN/../lib/meshlib", executable)
    manifest = {"format": 1, "sdk_version": "3.1.3.429", "architecture": architecture,
                "executable": {"path": str(executable.relative_to(package)), "sha256": digest(executable)},
                "libraries": libraries, "aliases": aliases, "system_dependencies": external,
                "licenses": copy_licenses(sdk_prefix, package, libraries)}
    (package / MANIFEST).parent.mkdir(parents=True, exist_ok=True)
    (package / MANIFEST).write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify(package)
    print(f"Bundled {len(libraries)} MeshLib runtime libraries: {destination}")


def loader_paths(output):
    result = {}
    for line in output.splitlines():
        if "not found" in line:
            raise RuntimeError(f"Unresolved packaged dependency: {line.strip()}")
        match = re.match(r"\s*(\S+)\s+=>\s+(/.+?)\s+\(0x[0-9a-fA-F]+\)", line)
        if match:
            result[match[1]] = Path(match[2]).resolve()
        else:
            loader = re.match(r"\s*(/\S+)\s+\(0x[0-9a-fA-F]+\)", line)
            if loader:
                path = Path(loader[1])
                result[path.name] = path.resolve()
    return result


def verify(package):
    package = package.resolve(strict=True)
    manifest = json.loads((package / MANIFEST).read_text(encoding="utf-8"))
    if manifest["format"] != 1 or manifest["sdk_version"] != "3.1.3.429":
        raise RuntimeError("Unsupported MeshLib runtime manifest")
    executable = (package / manifest["executable"]["path"]).resolve(strict=True)
    runtime = (package / RUNTIME).resolve(strict=True)
    if not executable.is_relative_to(package) or not runtime.is_relative_to(package):
        raise RuntimeError("Packaged executable/runtime escapes the package")
    if digest(executable) != manifest["executable"]["sha256"]:
        raise RuntimeError("Packaged executable changed after runtime bundling")
    if inspect(executable).get("rpath") != "$ORIGIN/../lib/meshlib":
        raise RuntimeError("Packaged executable must use its relative MeshLib RPATH")
    for name, metadata in manifest["libraries"].items():
        path = (runtime / name).resolve(strict=True)
        if not path.is_relative_to(runtime) or digest(path) != metadata["sha256"]:
            raise RuntimeError(f"Runtime changed after bundling: {name}")
    for alias, name in manifest["aliases"].items():
        if (runtime / alias).resolve(strict=True) != runtime / name:
            raise RuntimeError(f"Broken runtime SONAME link: {alias}")
    for relative, checksum in manifest["licenses"].items():
        if digest(package / relative) != checksum:
            raise RuntimeError(f"Packaged dependency license changed: {relative}")
    environment = {key: value for key, value in os.environ.items() if not key.startswith("LD_")}
    loaded = loader_paths(run("ldd", executable, env=environment))
    for name, path in loaded.items():
        if name in manifest["aliases"]:
            expected = runtime / manifest["aliases"][name]
            if path != expected:
                raise RuntimeError(f"Runtime resolved outside package: {name} -> {path}")
        elif not system_path(path):
            raise RuntimeError(f"Unbundled non-system dependency: {name} -> {path}")
    for root in ROOT_LIBRARIES:
        name = manifest["aliases"][root]
        soname = manifest["libraries"][name]["soname"] or root
        if soname not in loaded:
            raise RuntimeError(f"Executable does not load the bundled MeshLib backend: {soname}")
    for metadata in manifest["libraries"].values():
        for name in metadata["needed"]:
            if name not in loaded:
                raise RuntimeError(f"Missing runtime loader result: {name}")
    print(f"Verified MeshLib package dependencies and hashes: {package}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("bundle")
    for argument in ("executable", "sdk-prefix", "runtime-dir"):
        create.add_argument("--" + argument, type=Path, required=True)
    check = commands.add_parser("verify")
    check.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    try:
        if sys.platform != "linux":
            raise RuntimeError("MeshLib ELF packaging/verification requires Linux")
        if args.command == "bundle":
            bundle(args.executable, args.sdk_prefix, args.runtime_dir)
        else:
            verify(args.package)
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"MeshLib packaging error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
