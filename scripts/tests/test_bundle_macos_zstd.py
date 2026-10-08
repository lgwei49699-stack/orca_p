"""Native macOS regression tests; no application build or Homebrew required."""
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "bundle_macos_zstd.sh"


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("clang"), "requires macOS and clang")
class BundleZstdTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="zstd bundle test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / "Example.app"
        self.binary = self.app / "Contents/MacOS/Example"
        self.binary.parent.mkdir(parents=True)
        with (self.app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "Example"}, stream)
        self.library = self.root / "libzstd.1.dylib"
        self.compile_library(platform.machine())
        self.compile("int zstd_probe(void); int main(void) { return zstd_probe(); }",
                     "-mmacosx-version-min=11.3", "-o", str(self.binary), str(self.library))

    def compile(self, source, *args):
        subprocess.run(["clang", "-x", "c", "-", "-x", "none", *args], input=source,
                       text=True, check=True, capture_output=True)

    def compile_library(self, architecture, minimum="11.3"):
        self.compile("int zstd_probe(void) { return 0; }", "-dynamiclib", "-arch", architecture,
                     "-mmacosx-version-min=" + minimum,
                     "-install_name", str(self.library), "-o", str(self.library))

    def bundle(self):
        return subprocess.run(["bash", str(SCRIPT), str(self.app)], text=True, capture_output=True)

    def test_runs_without_original_library_and_handles_spaces(self):
        result = self.bundle()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        bundled = self.app / "Contents/Frameworks/libzstd.1.dylib"
        self.assertTrue(bundled.is_file())
        self.assertFalse(bundled.is_symlink())
        links = subprocess.check_output(["otool", "-L", str(self.binary)], text=True)
        self.assertIn("@executable_path/../Frameworks/libzstd.1.dylib", links)
        self.assertNotIn(str(self.library), links)
        self.library.rename(self.root / "original-library-hidden")
        environment = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        result = subprocess.run([str(self.binary)], env=environment, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        # Running again is harmless once references are bundle-relative.
        self.assertEqual(self.bundle().returncode, 0)

    def test_missing_library_fails_before_packaging(self):
        self.library.rename(self.root / "missing-library")
        result = self.bundle()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("zstd library not found", result.stderr)

    def test_wrong_architecture_fails_before_packaging(self):
        self.compile_library("x86_64" if platform.machine() == "arm64" else "arm64")
        result = self.bundle()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("zstd library lacks", result.stderr)

    def test_newer_macos_runtime_is_rejected(self):
        self.compile_library(platform.machine(), "26.0")
        result = self.bundle()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("zstd requires macOS 26.0 but app targets 11.3", result.stderr)

    def test_already_bundled_runtime_is_also_checked(self):
        result = self.bundle()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.compile_library(platform.machine(), "26.0")
        shutil.copyfile(self.library, self.app / "Contents/Frameworks/libzstd.1.dylib")
        result = self.bundle()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("zstd requires macOS 26.0", result.stderr)

    def test_source_checksum_mismatch_fails_before_build(self):
        archive = self.root / "invalid.tar.gz"
        archive.write_text("not the pinned upstream source")
        result = subprocess.run(
            ["bash", str(SCRIPT.with_name("build_macos_zstd.sh")), platform.machine(),
             "11.3", str(self.root / "runtime")], text=True, capture_output=True,
            env={**os.environ, "WISEBEGINNER_ZSTD_ARCHIVE": str(archive)})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)

    def test_invalid_executable_metadata_is_rejected(self):
        with (self.app / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "../../outside"}, stream)
        result = self.bundle()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid CFBundleExecutable", result.stderr)

    def test_scripts_reject_non_macos_before_changing_files(self):
        tools_dir = self.root / "tools"
        tools_dir.mkdir()
        uname = tools_dir / "uname"
        uname.write_text("#!/bin/sh\nprintf 'Linux\\n'\n")
        uname.chmod(0o755)
        environment = {**os.environ, "PATH": str(tools_dir) + os.pathsep + os.environ["PATH"]}
        commands = [
            [str(SCRIPT), str(self.app)],
            [str(SCRIPT.with_name("build_macos_zstd.sh")), "arm64", "11.3", str(self.root / "runtime")],
            [str(SCRIPT.with_name("package_macos_dmg.sh")), "--app", str(self.app)],
        ]
        for command in commands:
            with self.subTest(script=command[0]):
                result = subprocess.run(["bash", *command], env=environment, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("macOS", result.stderr)
        self.assertFalse((self.root / "runtime").exists())

    def test_application_name_cannot_escape_staging_directory(self):
        result = subprocess.run(
            ["bash", str(SCRIPT.with_name("package_macos_dmg.sh")), "--app", str(self.app),
             "--name", "../outside", "--output-dir", str(self.root / "output")],
            text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a path", result.stderr)
        self.assertFalse((self.root / "output").exists())

    def test_packaging_checks_bundle_and_includes_simple_guide(self):
        self.check_packaged_guide("WiseBeginnerSlicer")

    def test_orca_packaging_uses_orca_name_in_guide(self):
        self.check_packaged_guide("OrcaSlicer")

    def check_packaged_guide(self, app_name):
        # A program with no zstd still needs its complete bundle sealed.
        self.compile("int main(void) { return 0; }", "-mmacosx-version-min=11.3",
                     "-o", str(self.binary))
        resources = self.app / "Contents/Resources"
        resources.mkdir()
        (resources / "example.txt").write_text("test resource")
        # A developer tool beside the app must not enter a normal user DMG.
        shutil.copytree(self.app, self.root / "OrcaSlicer_profile_validator.app")
        original_binary = self.binary.read_bytes()
        output = self.root / "output"
        # The guide may describe manual commands, but packaging must never
        # execute them or change the developer's security settings.
        security_tools = self.root / "security-tools"
        security_tools.mkdir()
        security_log = self.root / "security-command.log"
        for name in ("spctl", "xattr", "sudo"):
            command = security_tools / name
            command.write_text('#!/bin/sh\nprintf "%s\\n" "$0" >> "$PACKAGING_SECURITY_COMMAND_LOG"\nexit 1\n')
            command.chmod(0o755)
        environment = {**os.environ,
                       "PATH": str(security_tools) + os.pathsep + os.environ["PATH"],
                       "PACKAGING_SECURITY_COMMAND_LOG": str(security_log)}
        result = subprocess.run(
            ["bash", str(SCRIPT.with_name("package_macos_dmg.sh")),
             "--app", str(self.app), "--name", app_name, "--output-dir", str(output)],
            env=environment, text=True, capture_output=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(security_log.exists(), "packaging must not execute security-setting commands")
        self.assertIn("LOCAL TEST PACKAGE ONLY", result.stdout)
        dmg = next(output.glob("*.dmg"))
        self.assertTrue(Path(str(dmg) + ".sha256").is_file())
        subprocess.run(["shasum", "-a", "256", "-c", dmg.name + ".sha256"], cwd=output,
                       check=True, capture_output=True)
        self.assertEqual(self.binary.read_bytes(), original_binary)
        # Keep the mount point isolated; detach only the image this test mounts.
        mount = self.root / "mounted"
        mount.mkdir()
        subprocess.run(["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint",
                        str(mount), str(dmg)], check=True, capture_output=True)
        try:
            self.assertEqual({entry.name for entry in mount.iterdir() if not entry.name.startswith(".")},
                             {f"{app_name}.app", "Applications", "安装与首次启动说明.txt"})
            self.assertFalse((mount / "Diagnose-macOS.command").exists())
            guide = (mount / "安装与首次启动说明.txt").read_text(encoding="utf-8")
            template = SCRIPT.with_name("macos-install-guide.txt").read_text(encoding="utf-8")
            self.assertEqual(guide, template.replace("@APP_NAME@", app_name).replace("@APP_DISPLAY_NAME@", "智小白切片软件" if app_name == "WiseBeginnerSlicer" else app_name))
            self.assertIn(f'xattr -dr com.apple.quarantine "/Applications/{app_name}.app"', guide)
            self.assertNotIn("Diagnose-macOS", guide)
            self.assertIn("sudo spctl --global-disable", guide)
            self.assertIn("仍需回到系统设置", guide)
            self.assertNotIn("恢复保护", guide)
            self.assertIn("不必开启", guide)
            headings = [line for line in guide.splitlines() if line.startswith(("1.", "2.", "3."))]
            self.assertEqual(len(headings), 3)
            self.assertEqual(headings[0], "1. 开启“任何来源”")
            self.assertTrue(headings[1].startswith("2. 在系统设置中允许打开"))
            self.assertTrue(headings[2].startswith("3. 若仍被下载隔离标记阻止"))
            self.assertNotIn("First launch", guide)
            self.assertNotIn("Restore protection", guide)
            self.assertNotIn("@APP_", guide)
            bundle = mount / f"{app_name}.app"
            verified = subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)],
                                      capture_output=True, text=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)
        finally:
            subprocess.run(["hdiutil", "detach", str(mount)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
