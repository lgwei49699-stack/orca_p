"""Evaluate the actual GMP recipe without downloading or compiling dependencies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


RECIPE = Path(__file__).resolve().parents[2] / "deps/GMP/GMP.cmake"
OLD_URL = "https://github.com/SoftFever/OrcaSlicer_deps/releases/download/gmp-6.2.1/gmp-6.2.1.tar.bz2"
OLD_HASH = "eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c"
NEW_URL = "https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz"
NEW_HASH = "a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"


@unittest.skipUnless(shutil.which("cmake"), "requires CMake")
class GmpPlatformSelectionTests(unittest.TestCase):
    def recipe(self, system, host, target="", cross=False, msvc=False, deps_arch="x64", git=True):
        # Mock only the build registration: run the production conditionals and
        # inspect the arguments that would reach ExternalProject/add_custom_*.
        script = r'''
function(ExternalProject_Add)
    cmake_parse_arguments(EP "BUILD_IN_SOURCE" "URL;URL_HASH;DOWNLOAD_DIR"
        "PATCH_COMMAND;CONFIGURE_COMMAND;BUILD_COMMAND;INSTALL_COMMAND" ${ARGN})
    foreach(key URL URL_HASH PATCH_COMMAND CONFIGURE_COMMAND)
        message("capture.${key}=${EP_${key}}")
    endforeach()
endfunction()
function(add_custom_command)
    message("capture.COPY=${ARGV}")
endfunction()
function(add_custom_target)
    message("capture.TARGET=${ARGV}")
endfunction()
include("${RECIPE}")
'''
        values = {
            "RECIPE": RECIPE.as_posix(), "CMAKE_SYSTEM_NAME": system,
            "CMAKE_SYSTEM_PROCESSOR": host, "CMAKE_OSX_ARCHITECTURES": target,
            "APPLE": system == "Darwin", "MSVC": msvc,
            "IS_CROSS_COMPILE": cross, "CMAKE_CROSSCOMPILING": False,
            "DEP_OSX_TARGET": "11.3", "DESTDIR": "/test/dest",
            "DEP_DOWNLOAD_DIR": "/test/cache", "DEPS_ARCH": deps_arch,
            "IN_GIT_REPO": git, "BINARY_DIR_REL": "deps/build/test",
        }
        with tempfile.TemporaryDirectory(prefix="gmp-recipe-test-") as directory:
            path = Path(directory) / "test.cmake"
            # Set normal variables after CMake has initialized host variables
            # such as APPLE; -D alone would only set a shadowed cache entry.
            setup = "".join(f'set({key} "${{TEST_{key}}}")\n' for key in values)
            path.write_text(setup + script, encoding="utf-8")
            command = ["cmake", *(f"-DTEST_{k}={int(v) if isinstance(v, bool) else v}" for k, v in values.items()),
                       "-P", str(path)]
            result = subprocess.run(command, text=True, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return dict(line[len("capture."):].split("=", 1)
                    for line in (result.stdout + result.stderr).splitlines() if line.startswith("capture."))

    def assert_source(self, result, new=False):
        self.assertEqual(result["URL"], NEW_URL if new else OLD_URL)
        self.assertEqual(result["URL_HASH"], "SHA256=" + (NEW_HASH if new else OLD_HASH))
        if new:
            self.assertEqual(result["PATCH_COMMAND"], "")
        else:
            self.assertTrue(result["PATCH_COMMAND"].startswith("git;apply;"))
            self.assertTrue(result["PATCH_COMMAND"].endswith("/0001-GMP_GCC15.patch"))
        self.assertIn("--enable-shared=no;--enable-cxx=yes;--enable-static=yes", result["CONFIGURE_COMMAND"])

    def test_native_macos_arm64_uses_fixed_source(self):
        self.assert_source(self.recipe("Darwin", "arm64", "arm64"), new=True)

    def test_implicit_macos_arm64_target_uses_fixed_source(self):
        self.assert_source(self.recipe("Darwin", "arm64"), new=True)

    def test_intel_host_cross_compiling_arm64_uses_fixed_source(self):
        result = self.recipe("Darwin", "x86_64", "arm64", cross=True)
        self.assert_source(result, new=True)
        self.assertIn("--host=aarch64-apple-darwin", result["CONFIGURE_COMMAND"])

    def test_arm64_host_cross_compiling_intel_keeps_old_source(self):
        result = self.recipe("Darwin", "arm64", "x86_64", cross=True)
        self.assert_source(result)
        self.assertIn("--host=x86_64-apple-darwin", result["CONFIGURE_COMMAND"])

    def test_native_macos_intel_keeps_old_source(self):
        self.assert_source(self.recipe("Darwin", "x86_64", "x86_64"))

    def test_linux_x86_64_keeps_old_source_and_patch(self):
        self.assert_source(self.recipe("Linux", "x86_64"))

    def test_linux_aarch64_keeps_old_source_and_patch(self):
        self.assert_source(self.recipe("Linux", "aarch64"))

    def test_linux_armv7_keeps_old_flags(self):
        result = self.recipe("Linux", "armv7l")
        self.assert_source(result)
        self.assertIn("-march=armv7-a", result["CONFIGURE_COMMAND"])

    def test_windows_msvc_keeps_bundled_libraries(self):
        for arch in ("x64", "x86"):
            with self.subTest(arch=arch):
                result = self.recipe("Windows", "AMD64", msvc=True, deps_arch=arch)
                self.assertNotIn("URL", result)
                self.assertIn(f"/lib/win-{arch}/libgmp-10.lib", result["COPY"])
                self.assertIn(f"/lib/win-{arch}/libgmp-10.dll", result["COPY"])
                self.assertIn("dep_GMP;SOURCES", result["TARGET"])

    def test_source_archive_without_git_keeps_patch_behavior(self):
        result = self.recipe("Linux", "x86_64", git=False)
        self.assert_source(result)
        self.assertNotIn("--directory", result["PATCH_COMMAND"])


if __name__ == "__main__":
    unittest.main()
