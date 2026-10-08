"""Native packaging/flag probes with tiny binaries, without building the slicer."""
import json
import os
from pathlib import Path
import platform
import plistlib
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("clang") and shutil.which("cmake"),
                     "requires macOS, clang and CMake")
class MacBuildCompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mac build compatibility ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def prepare_warning_project(self):
        project = self.root / "flags"
        project.mkdir()
        module = ROOT / "cmake/CompilerWarningCompatibility.cmake"
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.18)\nproject(Flags C CXX)\n'
            'if(DEFINED TEST_COMPILER_ID)\n'
            '  set(CMAKE_CXX_COMPILER_ID "${TEST_COMPILER_ID}")\n'
            '  set(CMAKE_CXX_COMPILER_VERSION "${TEST_COMPILER_VERSION}")\n'
            'endif()\n'
            f'include("{module}")\n'
            'add_executable(probe main.cpp helper.c)\n'
            'get_directory_property(options COMPILE_OPTIONS)\n'
            'get_directory_property(links LINK_OPTIONS)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/options.txt" "${options}\n${links}\n")\n')
        (project / "main.cpp").write_text('extern "C" int helper(void); int main() { return helper(); }\n')
        (project / "helper.c").write_text('int helper(void) { return 0; }\n')
        return project

    def test_warning_flags_are_probed_and_language_scoped(self):
        project = self.prepare_warning_project()
        build = project / "build"
        subprocess.run(["cmake", "-S", str(project), "-B", str(build),
                        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], check=True, capture_output=True)
        result = subprocess.run(["cmake", "--build", str(build)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("unknown warning option", result.stdout + result.stderr)
        commands = json.loads((build / "compile_commands.json").read_text())
        c_command = next(c["command"] for c in commands if c["file"].endswith("helper.c"))
        self.assertNotIn("enum-constexpr-conversion", c_command)

    def test_warning_flags_respect_compiler_version_and_capability(self):
        project = self.prepare_warning_project()
        # Configure only: simulate both version and capability, independently
        # of the host compiler. Do not build flags it may not actually support.
        cases = (
            ("AppleClang", "14.0.3", True, False),
            ("AppleClang", "15.0.0", True, False),
            ("AppleClang", "15.0.1", True, True),
            ("AppleClang", "16.0.0", True, True),
            ("AppleClang", "16.0.0", False, False),
            ("Clang", "16.0.0", True, True),
            ("Clang", "16.0.0", False, False),
            ("GNU", "16.0.0", True, False),
        )
        for index, (compiler, version, supported, expected) in enumerate(cases):
            with self.subTest(compiler=compiler, version=version, supported=supported):
                build = project / f"build-{index}"
                subprocess.run([
                    "cmake", "-S", str(project), "-B", str(build),
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    f"-DTEST_COMPILER_ID={compiler}",
                    f"-DTEST_COMPILER_VERSION={version}",
                    "-DORCA_HAS_ENUM_CONSTEXPR_CONVERSION:INTERNAL=" + ("TRUE" if supported else "FALSE"),
                    "-DORCA_HAS_NO_WARN_DUPLICATE_LIBRARIES:INTERNAL=FALSE",
                ], check=True, capture_output=True)
                commands = {Path(c["file"]).name: c["command"]
                            for c in json.loads((build / "compile_commands.json").read_text())}
                self.assertEqual(set(commands), {"main.cpp", "helper.c"})
                self.assertNotIn("enum-constexpr-conversion", commands["helper.c"])
                self.assertEqual("-Wno-error=enum-constexpr-conversion" in commands["main.cpp"], expected)
                if not expected:
                    self.assertEqual((build / "options.txt").read_text(), "\n\n")

    def prepare_build_entry(self):
        scripts = self.root / "scripts"
        scripts.mkdir()
        shutil.copyfile(ROOT / "build_release_macos.sh", self.root / "build_release_macos.sh")
        shutil.copyfile(ROOT / "scripts/bundle_macos_zstd.sh", scripts / "bundle_macos_zstd.sh")
        (scripts / "run_gettext.sh").write_text("#!/bin/sh\nexit 0\n")
        (scripts / "run_gettext.sh").chmod(0o755)
        resources = self.root / "resources"
        resources.mkdir()
        (resources / "sample.txt").write_text("untouched repository resource")
        # Mock only expensive build tools; actual Mach-O linking, bundling,
        # universal merging, resources and signing use the production entry.
        helper = self.root / "fixture.py"
        helper.write_text('''import json, pathlib, plistlib, subprocess, sys
root = pathlib.Path(__file__).parent
def compile(source, *args):
    # Fat binaries compile the source once per architecture; stdin would be
    # consumed by the first compiler process and leave the second slice empty.
    source_path = root / "probe.c"
    source_path.write_text(source)
    subprocess.run(["clang", str(source_path), *args],
                   text=True, check=True, capture_output=True)
if sys.argv[1] == "runtime":
    archs, minimum, work = sys.argv[2:]
    install = pathlib.Path(work) / "install"
    library = install / "lib/libzstd.1.dylib"
    (install / "lib/cmake/zstd").mkdir(parents=True, exist_ok=True)
    flags = [flag for arch in archs.split() for flag in ("-arch", arch)]
    compile("int zstd_probe(void) { return 0; }", "-dynamiclib", *flags,
            "-mmacosx-version-min=" + minimum, "-install_name", str(library), "-o", str(library))
    (install / "zstd-LICENSE.txt").write_text("test runtime license")
else:
    args = sys.argv[2:]
    with (root / "cmake-calls.jsonl").open("a") as log:
        log.write(json.dumps(args) + "\\n")
    build = pathlib.Path.cwd()
    cache = build / "CMakeCache.txt"
    if "--build" not in args:
        directory = next(a.split("=", 1)[1] for a in args if a.startswith("-Dzstd_DIR:PATH="))
        cache.write_text("zstd_DIR:PATH=" + directory + "\\n")
    else:
        directory = cache.read_text().strip().split("=", 1)[1]
        library = pathlib.Path(directory).parents[1] / "libzstd.1.dylib"
        for name in ("OrcaSlicer", "OrcaSlicer_profile_validator"):
            app = build / "src/Release" / (name + ".app")
            binary = app / "Contents/MacOS" / name
            binary.parent.mkdir(parents=True, exist_ok=True)
            compile("int zstd_probe(void); int main(void) { return zstd_probe(); }",
                    "-arch", build.name, "-mmacosx-version-min=11.3", "-o", str(binary), str(library))
            (app / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": name}))
            resource_link = app / "Contents/Resources"
            if not resource_link.exists():
                resource_link.symlink_to(root / "resources", target_is_directory=True)
''')
        runner = shlex.quote(sys.executable) + " " + shlex.quote(str(helper))
        (scripts / "build_macos_zstd.sh").write_text('#!/bin/sh\nexec ' + runner + ' runtime "$@"\n')
        tools = self.root / "tools"
        tools.mkdir()
        (tools / "cmake").write_text('#!/bin/sh\nexec ' + runner + ' cmake "$@"\n')
        (tools / "cmake").chmod(0o755)
        self.environment = {**os.environ, "PATH": str(tools) + os.pathsep + os.environ["PATH"]}
        for key in ("ARCH", "OSX_DEPLOYMENT_TARGET", "BUILD_ONLY", "PACK_DEPS", "BUILD_TARGET"):
            self.environment.pop(key, None)

    def run_build_entry(self, arch, *args):
        return subprocess.run(["bash", str(self.root / "build_release_macos.sh"),
                               "-sxa", arch, "-c", "Release", *args],
                              env=self.environment, text=True, capture_output=True, timeout=90)

    def check_bundle(self, arch, name):
        app = self.root / "build" / arch / "OrcaSlicer" / (name + ".app")
        binary = app / "Contents/MacOS" / name
        links = subprocess.check_output(["otool", "-L", str(binary)], text=True)
        self.assertIn("@executable_path/../Frameworks/libzstd.1.dylib", links)
        self.assertNotIn("macos-zstd", links)
        self.assertFalse((app / "Contents/Resources").is_symlink())
        self.assertEqual((app / "Contents/Resources").stat().st_mode & 0o055, 0o055)
        self.assertTrue((app / "Contents/Resources/licenses/zstd-LICENSE.txt").is_file())
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True, capture_output=True)
        if arch == "universal":
            subprocess.run(["lipo", str(app / "Contents/Frameworks/libzstd.1.dylib"),
                            "-verify_arch", "arm64", "x86_64"], check=True, capture_output=True)
        environment = {k: v for k, v in os.environ.items() if not k.startswith("DYLD_")}
        subprocess.run([str(binary)], env=environment, check=True, capture_output=True)

    def test_normal_build_and_cached_build_package_both_apps(self):
        self.prepare_build_entry()
        arch = platform.machine()
        for flags in ((), ("-b",)):
            result = self.run_build_entry(arch, *flags)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runtime = self.root / "build" / arch / "macos-zstd"
        runtime.rename(runtime.with_name("runtime-hidden"))
        for name in ("OrcaSlicer", "OrcaSlicer_profile_validator"):
            self.check_bundle(arch, name)
        self.assertFalse((self.root / "resources/licenses").exists())

    def test_old_cached_homebrew_selection_requires_reconfigure(self):
        self.prepare_build_entry()
        arch = platform.machine()
        build = self.root / "build" / arch
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("zstd_DIR:PATH=/opt/homebrew/lib/cmake/zstd\n")
        result = self.run_build_entry(arch, "-b")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rerun without -b", result.stderr)
        self.assertFalse((build / "OrcaSlicer").exists())

    def test_universal_has_both_runtime_slices_and_valid_signature(self):
        self.prepare_build_entry()
        result = self.run_build_entry("universal")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runtime = self.root / "build/universal/macos-zstd"
        runtime.rename(runtime.with_name("runtime-hidden"))
        for name in ("OrcaSlicer", "OrcaSlicer_profile_validator"):
            self.check_bundle("universal", name)


if __name__ == "__main__":
    unittest.main()
