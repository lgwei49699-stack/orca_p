"""Exercise real MeshLib CMake selection with explicitly fake SDK fixtures.

These are configure-only probes, not SDK compilation, linking, loading, target
OS execution, ABI validation, or package validation. Only target-registration
commands are captured: the production platform checks, file discovery, version
checks, file hashes, and generated identity header execute unchanged. No SDK is
downloaded and the main Orca build directory is never accessed.
"""

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "src/meshlib"
VERSION = "3.1.3.429"


def cmake_quote(value):
    return str(value).replace("\\", "/").replace('"', '\\"')


@unittest.skipUnless(shutil.which("cmake"), "requires CMake; does not require a compiler or a real SDK")
class MeshLibPlatformConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="meshlib-config-probe-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.next_probe = 0

    def sdk(self, system, *, layout="prefix", version=VERSION, missing=()):
        root = self.root / f"SDK 空格 {system} {layout} {self.next_probe}"
        prefix = root / "install" if layout == "install" else root / "usr/local" if layout == "linux-deb" else root
        include = prefix / ("include/MeshLib" if system == "Linux" else "include")
        lib = prefix / ("lib/MeshLib" if system == "Linux" else "lib")
        files = {
            include / "MRMesh/MRMesh.h": b"// Fake SDK fixture; never compiled.\n",
            include / "MRMesh/config_cmake.h": f'#define MESHLIB_VERSION "v{version}"\n'.encode("utf-8"),
        }
        if system == "Windows":
            runtime = prefix / "app/Release"
            link = lib / "Release"
            files.update({runtime / "mr.version": (version + "\n").encode("utf-8")})
            for name in ("MRMesh", "MRVoxels"):
                files[runtime / (name + ".dll")] = ("FAKE DLL " + name).encode("ascii")
                files[link / (name + ".lib")] = ("FAKE IMPORT LIB " + name).encode("ascii")
        else:
            runtime = lib
            suffix = ".dylib" if system == "Darwin" else ".so"
            for name in ("MRMesh", "MRVoxels"):
                files[runtime / ("lib" + name + suffix)] = ("FAKE SHARED LIB " + name).encode("ascii")
        for path, data in files.items():
            if any(path.name == omitted for omitted in missing):
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (include / "include").mkdir(parents=True, exist_ok=True)
        return root, prefix, runtime

    def configure(self, system="Darwin", sdk_root=None, *, enabled=True, arch=None, extra=None, dependency=None):
        self.next_probe += 1
        project = self.root / f"probe-{self.next_probe}"
        project.mkdir()
        build = project / "build"
        values = {
            "ORCA_MESHLIB_LOCAL_EVALUATION": enabled,
            "ORCA_MESHLIB_SDK_ROOT": str(sdk_root) if sdk_root is not None else "",
            "ORCA_MESHLIB_EXTRA_INCLUDE_DIRS": "",
            "ORCA_MESHLIB_RUNTIME_DIR": "",
            "SLIC3R_STATIC": True,
            "CMAKE_SYSTEM_NAME": system,
            "CMAKE_SYSTEM_PROCESSOR": arch or ("arm64" if system == "Darwin" else "AMD64" if system == "Windows" else "x86_64"),
            "CMAKE_SIZEOF_VOID_P": 8,
            "APPLE": system == "Darwin",
            "WIN32": system == "Windows",
            "UNIX": system != "Windows",
            "MSVC": system == "Windows",
            "MSVC_VERSION": 1940,
            "CMAKE_CXX_COMPILER_ID": "MSVC" if system == "Windows" else "AppleClang" if system == "Darwin" else "GNU",
            "CMAKE_CXX_COMPILER_VERSION": "19.40" if system == "Windows" else "16.0",
            "CMAKE_CXX_COMPILER_ARCHITECTURE_ID": "x64" if system == "Windows" else "",
            "CMAKE_OSX_ARCHITECTURES": arch or "arm64",
            "CMAKE_OSX_DEPLOYMENT_TARGET": "14.0",
            "CMAKE_BUILD_TYPE": "Release",
            "CMAKE_CONFIGURATION_TYPES": "",
            "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL",
            "CMAKE_DL_LIBS": "dl" if system == "Linux" else "",
        }
        values.update(extra or {})
        cached_names = {"ORCA_MESHLIB_SDK_ROOT", "ORCA_MESHLIB_EXTRA_INCLUDE_DIRS", "ORCA_MESHLIB_RUNTIME_DIR"}
        setup = "".join(f'set({key} "{cmake_quote(int(value) if isinstance(value, bool) else value)}")\n'
                        for key, value in values.items() if key not in cached_names)
        # Dependency fixtures are real imported CMake targets. The production
        # TARGET/type/location validation must run, not be replaced by a stub.
        crypto = project / "libcrypto.a"
        crypto.write_bytes(b"FAKE static Crypto import; configuration probe only\n")
        dependencies = ('add_library(OpenSSL::Crypto STATIC IMPORTED)\n'
                        f'set_property(TARGET OpenSSL::Crypto PROPERTY IMPORTED_LOCATION "{cmake_quote(crypto)}")\n')
        if dependency:
            kind, filename, configuration = dependency
            location = project / filename
            location.write_bytes(b"FAKE dependency; configuration probe only\n")
            dependencies += f'add_library(TBB::tbb {kind} IMPORTED)\n'
            if configuration:
                dependencies += f'set_property(TARGET TBB::tbb PROPERTY IMPORTED_CONFIGURATIONS "{configuration}")\n'
            suffix = "_" + configuration if configuration else ""
            dependencies += f'set_property(TARGET TBB::tbb PROPERTY IMPORTED_LOCATION{suffix} "{cmake_quote(location)}")\n'
        # No compiler is enabled. Capture only CMake build registration APIs;
        # do not replace file()/find_*()/EXISTS or any production conditionals.
        captures = "\n".join(
            f'function({name})\n  message("capture.{name}=${{ARGV}}")\nendfunction()'
            for name in ("add_library", "set_target_properties", "target_compile_definitions", "target_compile_options",
                         "target_include_directories", "target_link_libraries", "target_link_options"))
        captures += '\nfunction(set_property)\n  if(ARGV0 STREQUAL "TARGET")\n    message("capture.set_property=${ARGV}")\n  else()\n    _set_property(${ARGV})\n  endif()\nendfunction()\n'
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.18)\nproject(MeshLibConfigProbe LANGUAGES NONE)\n'
            + setup + dependencies + captures + f'\nadd_subdirectory("{cmake_quote(MODULE)}" "${{CMAKE_BINARY_DIR}}/adapter")\n',
            encoding="utf-8")
        result = subprocess.run(["cmake", "-S", str(project), "-B", str(build),
                                 *(f"-D{name}:STRING={values[name]}" for name in sorted(cached_names))],
                                text=True, encoding="utf-8", errors="replace", capture_output=True, timeout=20)
        output = result.stdout + result.stderr
        registrations = [line for line in output.splitlines() if line.startswith("capture.")]
        identity = build / "adapter/runtime_identity.hpp"
        return result.returncode, output, registrations, identity.read_text(encoding="utf-8") if identity.is_file() else ""

    def accepted(self, result):
        code, output, registrations, identity = result
        self.assertEqual(code, 0, output)
        self.assertTrue(any("orca_meshlib_adapter" in row for row in registrations), output)
        return "\n".join(registrations), identity, output

    def rejected(self, result, fragment, *, before_adapter=True):
        code, output, registrations, identity = result
        self.assertNotEqual(code, 0, output)
        self.assertIn(fragment.lower(), output.lower())
        if before_adapter:
            self.assertFalse(any("MeshLibAdapter.cpp" in row for row in registrations), output)

    def test_macos_prepared_runtime_hashes_and_transitive_search_path(self):
        sdk, _, _ = self.sdk("Darwin")
        prepared = self.root / "prepared runtime/MeshLib"
        prepared.mkdir(parents=True)
        options = {"ORCA_MESHLIB_RUNTIME_DIR": str(prepared)}
        self.rejected(self.configure(sdk_root=sdk, extra=options), "prepared runtime directory")
        (prepared / "meshlib-runtime.json").write_text('{}\n')
        for name in ("libMRMesh.dylib", "libMRVoxels.dylib"):
            (prepared / name).write_bytes(("FAKE prepared signed runtime " + name).encode())
        registrations, identity, _ = self.accepted(self.configure(sdk_root=sdk, extra=options))
        self.assertIn("capture.target_link_libraries=orca_meshlib_adapter;INTERFACE;-Wl,-rpath," + str(prepared.parent),
                      registrations)
        for name in ("libMRMesh.dylib", "libMRVoxels.dylib"):
            self.assertIn(hashlib.sha256((prepared / name).read_bytes()).hexdigest(), identity)

    def test_sdk_off_does_not_require_sdk_on_any_platform(self):
        for system in ("Darwin", "Windows", "Linux", "UnsupportedOS"):
            with self.subTest(system=system):
                calls, identity, _ = self.accepted(self.configure(system, "does-not-exist", enabled=False))
                self.assertIn("MeshLibAdapterStub.cpp", calls)
                self.assertNotIn("MRMesh", calls)
                self.assertNotIn("target_link_libraries", calls)
                self.assertEqual(identity, "")

    def test_macos_arm64_fixed_sdk_and_identity(self):
        root, prefix, runtime = self.sdk("Darwin")
        calls, identity, _ = self.accepted(self.configure("Darwin", root))
        self.assertIn("CXX_STANDARD;20", calls)
        self.assertIn("-fno-lto", calls)
        self.assertIn("LINKER:-rpath,", calls)
        self.assertNotIn(".dll", calls)
        for name in ("libMRMesh.dylib", "libMRVoxels.dylib"):
            path = runtime / name
            self.assertIn(path.as_posix(), calls)
            self.assertNotIn(str(root), identity)
            self.assertIn(hashlib.sha256(path.read_bytes()).hexdigest(), identity)

    def test_macos_old_deployment_rejected(self):
        root, _, _ = self.sdk("Darwin")
        self.rejected(self.configure("Darwin", root, extra={"CMAKE_OSX_DEPLOYMENT_TARGET": "11.3"}), "14.0")

    def test_macos_unsupported_architecture_rejected(self):
        root, _, _ = self.sdk("Darwin")
        for arch in ("x86_64", "arm64;x86_64"):
            with self.subTest(arch=arch):
                self.rejected(self.configure("Darwin", root, arch=arch), "arm64")

    def test_sdk_root_must_be_absolute(self):
        self.rejected(self.configure("Linux", "relative-sdk"), "absolute")

    def test_wrong_sdk_version_rejected(self):
        for system in ("Darwin", "Windows", "Linux"):
            with self.subTest(system=system):
                root, _, _ = self.sdk(system, version="9.9.9")
                self.rejected(self.configure(system, root), "version")

    def test_missing_sdk_runtime_rejected(self):
        for system, missing in (("Darwin", "libMRVoxels.dylib"), ("Windows", "MRVoxels.dll"), ("Linux", "libMRVoxels.so")):
            with self.subTest(system=system):
                root, _, _ = self.sdk(system, missing=(missing,))
                self.rejected(self.configure(system, root), "MRVoxels")

    def test_missing_sdk_version_file_rejected(self):
        for system, missing in (("Darwin", "config_cmake.h"), ("Windows", "mr.version"), ("Linux", "config_cmake.h")):
            with self.subTest(system=system):
                root, _, _ = self.sdk(system, missing=(missing,))
                self.rejected(self.configure(system, root), missing)

    def test_missing_sdk_headers_rejected(self):
        for system in ("Darwin", "Windows", "Linux"):
            with self.subTest(system=system):
                root, _, _ = self.sdk(system, missing=("MRMesh.h",))
                self.rejected(self.configure(system, root), "MRMesh.h")

    def test_windows_import_libraries_and_runtime_dlls_are_distinct(self):
        for layout in ("prefix", "install"):
            with self.subTest(layout=layout):
                root, prefix, runtime = self.sdk("Windows", layout=layout)
                calls, identity, output = self.accepted(self.configure("Windows", root))
                self.assertIn("MRMesh.lib", calls)
                self.assertIn("MRVoxels.lib", calls)
                self.assertIn((runtime / "MRMesh.dll").as_posix(), calls)
                self.assertIn((runtime / "MRVoxels.dll").as_posix(), calls)
                self.assertNotIn(str(root), identity)
                self.assertIn(hashlib.sha256((runtime / "MRMesh.dll").read_bytes()).hexdigest(), identity)
                self.assertNotIn("-fno-lto", calls)
                self.assertNotIn("-Wl,", calls)
                self.assertIn("/MD", calls)
                self.assertIn("MR_ITERATOR_DEBUG_LEVEL=0", calls)
                self.assertIn("MSVC_RUNTIME_LIBRARY;MultiThreadedDLL", calls)

    def test_windows_missing_import_library_rejected(self):
        root, _, _ = self.sdk("Windows", missing=("MRMesh.lib",))
        self.rejected(self.configure("Windows", root), "MRMesh")

    def test_windows_release_family_configurations(self):
        root, _, _ = self.sdk("Windows")
        for configuration in ("Release", "RelWithDebInfo", "MinSizeRel"):
            with self.subTest(configuration=configuration):
                self.accepted(self.configure("Windows", root, extra={"CMAKE_BUILD_TYPE": configuration}))

    def test_windows_debug_and_static_crt_are_rejected(self):
        root, _, _ = self.sdk("Windows")
        self.rejected(self.configure("Windows", root, extra={"CMAKE_BUILD_TYPE": "Debug"}), "Debug")
        self.rejected(self.configure("Windows", root, extra={"CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded"}), "DLL")

    def test_windows_incompatible_flags_are_rejected(self):
        root, _, _ = self.sdk("Windows")
        for name in ("CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE"):
            for flag in ("/MT", "/MTd", "/MDd", "-MT", "/D_ITERATOR_DEBUG_LEVEL=1", "-D_ITERATOR_DEBUG_LEVEL=2", "-D_ITERATOR_DEBUG_LEVEL=9"):
                with self.subTest(name=name, flag=flag):
                    self.rejected(self.configure("Windows", root, extra={name: flag}), "incompatible")

    def test_windows_multi_config_and_empty_configuration(self):
        root, _, _ = self.sdk("Windows")
        self.accepted(self.configure("Windows", root, extra={"CMAKE_CONFIGURATION_TYPES": "Release;RelWithDebInfo"}))
        self.rejected(self.configure("Windows", root, extra={"CMAKE_CONFIGURATION_TYPES": "Release;Debug"}), "Debug")
        self.rejected(self.configure("Windows", root, extra={"CMAKE_BUILD_TYPE": ""}), "explicit")

    def test_windows_requires_msvc_and_64_bit_target(self):
        root, _, _ = self.sdk("Windows")
        self.rejected(self.configure("Windows", root, extra={"MSVC": False}), "MSVC")
        self.rejected(self.configure("Windows", root, extra={"CMAKE_CXX_COMPILER_ID": "Clang"}), "MSVC")
        self.rejected(self.configure("Windows", root, extra={"CMAKE_SIZEOF_VOID_P": 4}), "64")
        self.rejected(self.configure("Windows", root, arch="ARM64", extra={"CMAKE_CXX_COMPILER_ARCHITECTURE_ID": "ARM64"}), "x64")

    def test_linux_package_and_prefix_layouts(self):
        for layout in ("prefix", "linux-deb"):
            with self.subTest(layout=layout):
                root, _, runtime = self.sdk("Linux", layout=layout)
                calls, identity, _ = self.accepted(self.configure("Linux", root))
                self.assertIn("libMRMesh.so", calls)
                self.assertIn("libMRVoxels.so", calls)
                self.assertIn("-fno-lto", calls)
                self.assertNotIn(".dylib", calls)
                self.assertNotIn(".dll", calls)
                self.assertIn((runtime / "libMRMesh.so").as_posix(), calls)
                self.assertNotIn(str(root), identity)
                self.assertIn(hashlib.sha256((runtime / "libMRMesh.so").read_bytes()).hexdigest(), identity)

    def test_linux_64_bit_architectures(self):
        root, _, _ = self.sdk("Linux")
        for arch in ("x86_64", "amd64", "aarch64", "arm64"):
            with self.subTest(arch=arch):
                self.accepted(self.configure("Linux", root, arch=arch))
        self.rejected(self.configure("Linux", root, arch="riscv64"), "x64 and arm64")

    def test_linux_requires_supported_compiler_and_64_bit_target(self):
        root, _, _ = self.sdk("Linux")
        self.accepted(self.configure("Linux", root, extra={"CMAKE_CXX_COMPILER_ID": "Clang"}))
        self.rejected(self.configure("Linux", root, extra={"CMAKE_CXX_COMPILER_ID": "Intel"}), "GCC or Clang")
        self.rejected(self.configure("Linux", root, extra={"CMAKE_SIZEOF_VOID_P": 4}), "64-bit")

    def test_linux_static_dependency_validation_uses_real_imported_targets(self):
        root, _, _ = self.sdk("Linux")
        for kind, filename, configuration in (("STATIC", "libtbb.a", ""), ("UNKNOWN", "libtbb.a", "RELEASE")):
            with self.subTest(kind=kind, configuration=configuration):
                calls, _, _ = self.accepted(self.configure("Linux", root, dependency=(kind, filename, configuration)))
                self.assertIn("LINKER:--exclude-libs,$<TARGET_FILE_NAME:TBB::tbb>", calls)
        for kind, filename, configuration in (("SHARED", "libtbb.so", ""), ("UNKNOWN", "libtbb.so", "RELEASE")):
            with self.subTest(kind=kind, configuration=configuration):
                self.rejected(self.configure("Linux", root, dependency=(kind, filename, configuration)), "static Orca dependency", before_adapter=False)

    def test_shared_orca_dependencies_rejected(self):
        for system in ("Darwin", "Windows", "Linux"):
            with self.subTest(system=system):
                root, _, _ = self.sdk(system)
                self.rejected(self.configure(system, root, extra={"SLIC3R_STATIC": False}), "static")

    def test_unknown_platform_rejected(self):
        root, _, _ = self.sdk("Linux")
        self.rejected(self.configure("UnsupportedOS", root), "supports macOS")


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    print("MeshLib configuration probes only: fake SDK files; no target compilation, SDK loading, or packaging.", flush=True)
    unittest.main()
