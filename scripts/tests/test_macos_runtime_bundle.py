"""Native dependency closure, relocation and immutable-runtime signature probes."""
import importlib.util
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "macos_runtime_bundle.py"
spec = importlib.util.spec_from_file_location("macos_runtime_bundle", SCRIPT)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("clang"), "requires macOS native tools")
class MacRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="meshlib package test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / "sdk"
        self.lib = self.sdk / "lib"
        self.lib.mkdir(parents=True)
        self.stage = self.root / "prepared/MeshLib"
        self.app = self.root / "Probe.app"
        (self.app / "Contents/MacOS").mkdir(parents=True)
        (self.app / "Contents/Resources").mkdir()
        with (self.app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "Probe", "CFBundleIdentifier": "org.orca.runtime-probe",
                          "CFBundlePackageType": "APPL"}, stream)
        self.main = self.app / "Contents/MacOS/Probe"

    def compile(self, source, output, *args, minimum="14.0", arch="arm64"):
        src = self.root / "probe.c"
        src.write_text(source)
        runtime.run("clang", "-arch", arch, "-mmacosx-version-min=" + minimum,
                    src, "-o", output, *args)

    def sdk_fixture(self, minimum="14.0", arch="arm64"):
        for name, source, deps in (
            ("libzstd.1.dylib", "int nested(void) { return 7; }", []),
            ("libMRMesh.dylib", "extern int nested(void); int mesh(void) { return nested(); }",
             [str(self.lib / "libzstd.1.dylib")]),
            ("libMRVoxels.dylib", "extern int mesh(void); int voxels(void) { return mesh(); }",
             [str(self.lib / "libMRMesh.dylib")]),
        ):
            self.compile(source, self.lib / name, "-dynamiclib", "-install_name", "@rpath/" + name,
                         "-Wl,-rpath,/unavailable/developer/sdk", *deps, minimum=minimum, arch=arch)

    def link_app(self, directory, minimum="14.0"):
        self.compile("extern int mesh(void); extern int voxels(void); int main(void) { return mesh() + voxels() == 14 ? 0 : 1; }",
                     self.main, directory / "libMRMesh.dylib", directory / "libMRVoxels.dylib",
                     "-Wl,-rpath," + str(directory.parent), "-Wl,-rpath," + str(directory), minimum=minimum)

    def prepare(self):
        runtime.prepare(self.sdk, self.stage, "14.0")

    def test_relocated_signed_app_has_complete_immutable_closure(self):
        self.sdk_fixture()
        original = {p.name: runtime.sha(p) for p in self.lib.iterdir()}
        self.prepare()
        prepared = runtime.verify_manifest(self.stage)
        self.assertEqual(set(prepared["libraries"]), set(original))
        self.assertEqual(original, {p.name: runtime.sha(p) for p in self.lib.iterdir()})
        timestamp = (self.stage / runtime.MANIFEST).stat().st_mtime_ns
        self.prepare()
        self.assertEqual(timestamp, (self.stage / runtime.MANIFEST).stat().st_mtime_ns)
        self.link_app(self.stage)
        runtime.bundle(self.app, self.stage)
        # A second same-basename app runtime must not intercept the SDK copy.
        self.compile("int nested(void) { return 100; }", self.app / "Contents/Frameworks/libzstd.1.dylib",
                     "-dynamiclib", "-install_name", "@rpath/libzstd.1.dylib")
        runtime.run("codesign", "--force", "--sign", "-", self.app / "Contents/Frameworks/libzstd.1.dylib")
        runtime.finalize(self.app, "-")
        self.assertEqual(plistlib.loads((self.app / "Contents/Info.plist").read_bytes())["LSMinimumSystemVersion"], "14.0")
        self.assertEqual(prepared, runtime.verify_manifest(self.app / "Contents/Frameworks/MeshLib",
                                                          self.app / "Contents/Resources" / runtime.MANIFEST))
        moved = self.root / "moved elsewhere/Probe.app"
        moved.parent.mkdir()
        shutil.copytree(self.app, moved)
        shutil.rmtree(self.sdk)
        shutil.rmtree(self.stage)
        runtime.finalize(moved, "-")  # Same path used for the DMG's copied app.
        self.assertEqual(prepared, runtime.verify_manifest(moved / "Contents/Frameworks/MeshLib",
                                                          moved / "Contents/Resources" / runtime.MANIFEST))
        env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        subprocess.run([str(runtime.executable(moved))], env=env, check=True, capture_output=True)

    def test_rejects_missing_transitive_dependency(self):
        self.sdk_fixture()
        (self.lib / "libzstd.1.dylib").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing or external SDK"):
            self.prepare()
        self.assertFalse(self.stage.exists())

    def test_rejects_incompatible_minimum_and_architecture(self):
        for minimum, arch, message in (("15.0", "arm64", "requires macOS 15"),
                                         ("14.0", "x86_64", "Missing architecture arm64")):
            with self.subTest(minimum=minimum, arch=arch):
                self.sdk_fixture(minimum=minimum, arch=arch)
                with self.assertRaisesRegex(RuntimeError, message):
                    self.prepare()

    def test_rejects_original_sdk_linkage_and_unbundled_app(self):
        self.sdk_fixture()
        self.prepare()
        self.link_app(self.lib)
        with self.assertRaisesRegex(RuntimeError, "original SDK"):
            runtime.bundle(self.app, self.stage)
        with self.assertRaisesRegex(RuntimeError, "MeshLib is not bundled"):
            runtime.finalize(self.app, "-")

    def test_rejects_changed_runtime_after_compilation(self):
        self.sdk_fixture()
        self.prepare()
        self.link_app(self.stage)
        runtime.bundle(self.app, self.stage)
        library = self.app / "Contents/Frameworks/MeshLib/libMRMesh.dylib"
        with library.open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaisesRegex(RuntimeError, "hash mismatch"):
            runtime.finalize(self.app, "-")

    def test_final_audit_rejects_missing_rpath_dependency(self):
        self.sdk_fixture()
        self.prepare()
        self.link_app(self.stage)
        runtime.bundle(self.app, self.stage)
        runtime.run("install_name_tool", "-change", "@rpath/MeshLib/libMRMesh.dylib",
                    "@rpath/missing.dylib", self.main)
        with self.assertRaisesRegex(RuntimeError, "Unresolved dependency"):
            runtime.audit(self.app)

    def test_final_audit_rejects_app_older_than_dependencies(self):
        self.sdk_fixture()
        self.prepare()
        self.link_app(self.stage, minimum="11.3")
        runtime.bundle(self.app, self.stage)
        with self.assertRaisesRegex(RuntimeError, "requires macOS 14"):
            runtime.audit(self.app)


if __name__ == "__main__":
    unittest.main()
