"""ELF packaging contract tests; mocked ELF tools do not prove Linux ABI safety."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("package_meshlib_linux", ROOT / "scripts/package_meshlib_linux.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def elf(needed=(), soname=None, extra="", machine="Advanced Micro Devices X86-64"):
    lines = ["ELF Header:", "  Class: ELF64", "  Machine: " + machine]
    lines += [f" 0x0000000000000001 (NEEDED) Shared library: [{name}]" for name in needed]
    if soname:
        lines.append(f" 0x000000000000000e (SONAME) Library soname: [{soname}]")
    return "\n".join(lines) + "\n" + extra


class ElfParsingTests(unittest.TestCase):
    def test_needed_soname_and_absolute_runpath(self):
        data = PACKAGE.parse_elf(elf(("libtbb.so.12",), "libMRMesh.so",
                                    " 0x1 (RUNPATH) Library runpath: [/original/sdk/lib]"), "library")
        self.assertEqual(data["needed"], ["libtbb.so.12"])
        self.assertEqual(data["soname"], "libMRMesh.so")
        self.assertEqual(data["architecture"], "x86_64")

    def test_origin_rpath_is_relocatable(self):
        PACKAGE.parse_elf(elf(extra=" 0x1 (RPATH) Library rpath: [$ORIGIN:$ORIGIN/lib]"), "library")

    def test_absolute_rpath_cannot_override_packaged_libraries(self):
        with self.assertRaisesRegex(RuntimeError, "DT_RPATH"):
            PACKAGE.parse_elf(elf(extra=" 0x1 (RPATH) Library rpath: [/old/sdk]"), "library")

    def test_build_executable_rpath_can_be_read_before_relocation(self):
        PACKAGE.parse_elf(elf(extra=" 0x1 (RPATH) Library rpath: [/old/sdk]"), "application", allow_build_rpath=True)

    def test_absolute_needed_requires_prepared_runtime(self):
        with self.assertRaisesRegex(RuntimeError, "Non-relocatable NEEDED"):
            PACKAGE.parse_elf(elf(("/old/sdk/libMRMesh.so",)), "library")

    def test_unsupported_machine_and_32_bit_rejected(self):
        for output in (elf(machine="Intel 80386"), elf().replace("ELF64", "ELF32")):
            with self.assertRaisesRegex(RuntimeError, "64-bit Linux ELF"):
                PACKAGE.parse_elf(output, "library")

    def test_ldd_missing_dependency_is_failure(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved packaged dependency"):
            PACKAGE.loader_paths(" libMRMesh.so => not found\n")

    def test_ldd_loader_without_arrow_is_recorded(self):
        paths = PACKAGE.loader_paths(" /lib64/ld-linux-x86-64.so.2 (0x00001234)\n")
        self.assertIn("ld-linux-x86-64.so.2", paths)

    def test_old_glibc_loader_version_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved packaged dependency"):
            PACKAGE.loader_paths("probe: libc.so.6: version `GLIBC_2.38' not found (required by libMRMesh.so)")

    def test_system_boundary_excludes_sdk_and_geometry_libraries(self):
        self.assertFalse(PACKAGE.system_path(Path("/usr/local/lib/MeshLib/libMRMesh.so")))
        for name in ("libtbb.so.12", "libopenvdb.so.13.0", "libcrypto.so.3", "libzstd.so.1"):
            self.assertNotIn(name, PACKAGE.SYSTEM_LIBRARIES)


class BundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="meshlib linux package ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sdk = self.root / "sdk/usr/local"
        self.runtime = self.sdk / "lib/MeshLib"
        self.package = self.root / "final package"
        self.executable = self.package / "bin/orca-slicer"
        self.executable.parent.mkdir(parents=True)
        self.executable.write_bytes(b"application ELF fixture")
        notices = self.sdk / "share/MeshLib/THIRD-PARTY-NOTICES.txt"
        notices.parent.mkdir(parents=True)
        notices.write_text("fixture notices\n")
        self.metadata = {"orca-slicer": elf(("libMRMesh.so.3", "libMRVoxels.so.3"))}
        self.library("libMRMesh.so.3.1", "libMRMesh.so.3", ("libc.so.6", "libstdc++.so.6"),
                     aliases=("libMRMesh.so",))
        self.library("libMRVoxels.so.3.1", "libMRVoxels.so.3", ("libMRMesh.so.3", "libtbb.so.12"),
                     aliases=("libMRVoxels.so",))
        self.library("libtbb.so.12.18", "libtbb.so.12", ("libc.so.6",), directory=self.runtime / "lib")
        self.library("libMRViewer.so", "libMRViewer.so", ())
        self.ldd_override = None
        self.environment = None
        self.tool_patch = patch.object(PACKAGE, "run", side_effect=self.tool)
        self.tool_patch.start()
        self.addCleanup(self.tool_patch.stop)

    def library(self, name, soname, needed, *, aliases=(), directory=None):
        directory = self.runtime if directory is None else directory
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / name
        source.write_bytes(("unchanged ELF bytes: " + name).encode())
        for alias in set(aliases) | {soname}:
            if alias != name:
                (directory / alias).symlink_to(name)
        self.metadata[name] = elf(needed, soname)
        return source

    def tool(self, *args, env=None):
        if args[0] == "readelf":
            return self.metadata[Path(args[-1]).resolve().name]
        if args[0] == "ldconfig":
            return "0 libs found in cache `/etc/ld.so.cache'\n"
        if args[0] == "patchelf":
            self.assertEqual(args[1:3], ("--force-rpath", "--set-rpath"))
            path = Path(args[-1])
            if path == self.executable.resolve():
                self.assertEqual(args[3], "$ORIGIN/../lib/meshlib")
                self.metadata["orca-slicer"] += "\n 0x1 (RPATH) Library rpath: [$ORIGIN/../lib/meshlib]"
            else:
                self.assertEqual(args[3], "$ORIGIN")
                self.assertEqual(path.parent, (self.package / PACKAGE.RUNTIME).resolve())
                self.assertFalse(path.name.startswith("libMR"))
            path.write_bytes(path.read_bytes() + b" relocated application RPATH")
            return ""
        self.assertEqual(args[0], "ldd")
        self.environment = env
        if self.ldd_override is not None:
            return self.ldd_override
        lines = []
        runtime = self.package / PACKAGE.RUNTIME
        for name in ("libMRMesh.so.3", "libMRVoxels.so.3", "libtbb.so.12"):
            lines.append(f" {name} => {runtime / name} (0x00001234)")
        for name in ("libc.so.6", "libstdc++.so.6"):
            lines.append(f" {name} => /usr/lib/x86_64-linux-gnu/{name} (0x00005678)")
        return "\n".join(lines)

    def bundle(self):
        PACKAGE.bundle(self.executable, self.sdk, self.runtime)

    def test_copy_only_dependency_closure_and_preserve_sonames_and_hashes(self):
        before = {path: path.read_bytes() for path in self.sdk.rglob("*") if path.is_file()}
        self.bundle()
        manifest = json.loads((self.package / PACKAGE.MANIFEST).read_text())
        self.assertEqual(len(manifest["libraries"]), 3)
        destination = self.package / PACKAGE.RUNTIME
        self.assertFalse((destination / "libMRViewer.so").exists())
        self.assertTrue((destination / "libMRMesh.so").is_symlink())
        self.assertTrue((destination / "libMRMesh.so.3").is_symlink())
        for name, metadata in manifest["libraries"].items():
            self.assertEqual((destination / name).read_bytes(), Path(metadata["source"]).read_bytes())
        self.assertEqual(before, {path: path.read_bytes() for path in before})
        self.assertNotIn("LD_LIBRARY_PATH", self.environment)
        self.assertNotIn("LD_PRELOAD", self.environment)

    def test_missing_transitive_dependency_fails_before_output(self):
        (self.runtime / "lib/libtbb.so.12").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing MeshLib runtime dependency: libtbb"):
            self.bundle()
        self.assertFalse((self.package / PACKAGE.RUNTIME).exists())

    def test_only_non_mr_dependency_with_runpath_is_relocated(self):
        self.metadata["libtbb.so.12.18"] += "\n 0x1 (RUNPATH) Library runpath: [/original/sdk/lib]"
        source = self.runtime / "lib/libtbb.so.12.18"
        before = source.read_bytes()
        self.bundle()
        manifest = json.loads((self.package / PACKAGE.MANIFEST).read_text())
        changed = manifest["libraries"]["libtbb.so.12.18"]
        self.assertNotEqual(changed["source_sha256"], changed["sha256"])
        self.assertEqual(before, source.read_bytes())
        for name in ("libMRMesh.so.3.1", "libMRVoxels.so.3.1"):
            self.assertEqual(manifest["libraries"][name]["sha256"], manifest["libraries"][name]["source_sha256"])

    def test_sdk_notice_is_preserved(self):
        self.bundle()
        notice = self.package / "resources/licenses/MeshLib/THIRD-PARTY-NOTICES.txt"
        self.assertEqual(notice.read_text(), "fixture notices\n")

    def test_non_mr_absolute_rpath_is_relocated_not_rejected(self):
        self.metadata["libtbb.so.12.18"] += "\n 0x1 (RPATH) Library rpath: [/old/sdk/lib]"
        self.bundle()

    def test_architecture_mismatch_fails_before_output(self):
        self.metadata["libtbb.so.12.18"] = elf(("libc.so.6",), "libtbb.so.12", machine="AArch64")
        with self.assertRaisesRegex(RuntimeError, "Architecture mismatch"):
            self.bundle()
        self.assertFalse((self.package / PACKAGE.RUNTIME).exists())

    def test_library_mutation_after_configure_is_detected(self):
        self.bundle()
        (self.package / PACKAGE.RUNTIME / "libMRMesh.so.3.1").write_bytes(b"stripped or patched")
        with self.assertRaisesRegex(RuntimeError, "Runtime changed after bundling"):
            PACKAGE.verify(self.package)

    def test_executable_mutation_is_detected(self):
        self.bundle()
        self.executable.write_bytes(b"changed by a later packaging stage")
        with self.assertRaisesRegex(RuntimeError, "executable changed"):
            PACKAGE.verify(self.package)

    def test_existing_package_not_overwritten(self):
        self.bundle()
        with self.assertRaisesRegex(RuntimeError, "destination already exists"):
            self.bundle()

    def test_sdk_loader_path_rejected_even_when_library_hash_is_correct(self):
        self.bundle()
        self.ldd_override = self.tool("ldd").replace(str(self.package / PACKAGE.RUNTIME), str(self.runtime))
        with self.assertRaisesRegex(RuntimeError, "Runtime resolved outside package"):
            PACKAGE.verify(self.package)

    def test_package_survives_missing_sdk(self):
        self.bundle()
        self.sdk.rename(self.root / "sdk-hidden")
        PACKAGE.verify(self.package)

    def test_broken_soname_cannot_escape_runtime(self):
        self.bundle()
        alias = self.package / PACKAGE.RUNTIME / "libMRMesh.so"
        alias.unlink()
        alias.symlink_to(self.runtime / "libMRMesh.so")
        with self.assertRaisesRegex(RuntimeError, "Broken runtime SONAME link"):
            PACKAGE.verify(self.package)

    def test_an_off_build_cannot_pass_meshlib_package_verification(self):
        self.ldd_override = "libc.so.6 => /usr/lib/x86_64-linux-gnu/libc.so.6 (0x00001234)"
        with self.assertRaisesRegex(RuntimeError, "does not load the bundled MeshLib backend"):
            self.bundle()


class PackagingTemplateTests(unittest.TestCase):
    def test_shell_syntax(self):
        for filename in ("build_linux_image.sh.in", "build_appimage.sh.in"):
            path = ROOT / "src/dev-utils/platform/unix" / filename
            subprocess.run(["bash", "-n", str(path)], check=True, capture_output=True)

    def test_bundle_runs_after_binary_copy_and_before_archive(self):
        source = (ROOT / "src/dev-utils/platform/unix/build_linux_image.sh.in").read_text()
        self.assertLess(source.index("cp -f src/Release/"), source.index("package_meshlib_linux.py"))
        self.assertLess(source.index("package_meshlib_linux.py"), source.index("tar -cvf"))


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cc") and shutil.which("patchelf"),
                     "requires Linux, patchelf and a C compiler")
class NativeElfTests(unittest.TestCase):
    def test_packaged_libraries_load_after_original_sdk_removed(self):
        # Tiny native ELF fixtures exercise the actual loader/tools. They are
        # not a MeshLib algorithm, SDK ABI or full Orca integration test.
        with tempfile.TemporaryDirectory(prefix="meshlib-native-package-") as temporary:
            root = Path(temporary)
            sdk = root / "sdk"
            runtime = sdk / "lib/MeshLib"
            package = root / "package"
            executable = package / "bin/probe"
            runtime.mkdir(parents=True)
            executable.parent.mkdir(parents=True)
            notices = sdk / "share/MeshLib/THIRD-PARTY-NOTICES.txt"
            notices.parent.mkdir(parents=True)
            notices.write_text("native fixture notices\n")

            def compile(source, *arguments):
                subprocess.run(["cc", "-x", "c", "-", "-x", "none", *map(str, arguments)],
                               input=source, text=True, check=True, capture_output=True)

            compile("int mesh(void) { return 42; }", "-shared", "-fPIC", "-Wl,-soname,libMRMesh.so",
                    "-o", runtime / "libMRMesh.so")
            compile("int leaf(void) { return 0; }", "-shared", "-fPIC", "-Wl,-soname,libfixtureLeaf.so",
                    "-o", runtime / "libfixtureLeaf.so")
            compile("int leaf(void); int dependency(void) { return leaf(); }", "-shared", "-fPIC",
                    "-Wl,-soname,libfixtureDependency.so", "-L" + str(runtime), "-lfixtureLeaf",
                    "-Wl,-rpath," + str(runtime), "-o", runtime / "libfixtureDependency.so")
            compile("int mesh(void); int dependency(void); int voxels(void) { return mesh() + dependency(); }",
                    "-shared", "-fPIC", "-Wl,-soname,libMRVoxels.so", "-L" + str(runtime),
                    "-lMRMesh", "-lfixtureDependency", "-o", runtime / "libMRVoxels.so")
            compile("int mesh(void); int voxels(void); int main(void) { return voxels() == mesh() ? 0 : 1; }",
                    "-L" + str(runtime), "-lMRVoxels", "-lMRMesh", "-Wl,-rpath," + str(runtime), "-o", executable)
            original = {path.name: path.read_bytes() for path in runtime.iterdir()}
            PACKAGE.bundle(executable, sdk, runtime)
            self.assertEqual(original, {path.name: path.read_bytes() for path in runtime.iterdir()})
            for name in PACKAGE.ROOT_LIBRARIES:
                self.assertEqual(original[name], (package / PACKAGE.RUNTIME / name).read_bytes())
            sdk.rename(root / "hidden-sdk")
            PACKAGE.verify(package)
            environment = {key: value for key, value in os.environ.items() if not key.startswith("LD_")}
            subprocess.run([str(executable)], env=environment, check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
