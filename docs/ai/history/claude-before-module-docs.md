# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

OrcaSlicer is an open-source 3D slicer application forked from Bambu Studio, built using C++ with wxWidgets for the GUI and CMake as the build system. The project uses a modular architecture with separate libraries for core slicing functionality, GUI components, and platform-specific code.

## Build Commands

### Building on Windows
```bash
# Build everything
build_release_vs2022.bat

# Build with debug symbols
build_release_vs2022.bat debug

# Build only dependencies
build_release_vs2022.bat deps

# Build only slicer (after deps are built)
build_release_vs2022.bat slicer


```

### Building on macOS
```bash
# Build everything (dependencies and slicer)
./build_release_macos.sh

# Build only dependencies
./build_release_macos.sh -d

# Build only slicer (after deps are built)
./build_release_macos.sh -s

# Use Ninja generator for faster builds
./build_release_macos.sh -x

# Build for specific architecture
./build_release_macos.sh -a arm64    # or x86_64 or universal

# Build for specific macOS version target
./build_release_macos.sh -t 11.3
```

### Building on Linux
```bash
# First time setup - install system dependencies
./build_linux.sh -u

# Build dependencies and slicer
./build_linux.sh -dsi

# Build everything (alternative)
./build_linux.sh -dsi

# Individual options:
./build_linux.sh -d    # dependencies only
./build_linux.sh -s    # slicer only  
./build_linux.sh -i    # build AppImage

# Performance and debug options:
./build_linux.sh -j N  # limit to N cores
./build_linux.sh -1    # single core build
./build_linux.sh -b    # debug build
./build_linux.sh -c    # clean build
./build_linux.sh -r    # skip RAM/disk checks
./build_linux.sh -l    # use Clang instead of GCC
```

### Build System
- Uses CMake with minimum version 3.13 (maximum 3.31.x on Windows)
- Primary build directory: `build/`
- Dependencies are built in `deps/build/`
- The build process is split into dependency building and main application building
- Windows builds use Visual Studio generators
- macOS builds use Xcode by default, Ninja with -x flag
- Linux builds use Ninja generator

### Testing
Tests are located in the `tests/` directory and use the Catch2 testing framework. Test structure:
- `tests/libslic3r/` - Core library tests (21 test files)
  - Geometry processing, algorithms, file formats (STL, 3MF, AMF)
  - Polygon operations, clipper utilities, Voronoi diagrams
- `tests/fff_print/` - Fused Filament Fabrication tests (12 test files)
  - Slicing algorithms, G-code generation, print mechanics
  - Fill patterns, extrusion, support material
- `tests/sla_print/` - Stereolithography tests (4 test files)
  - SLA-specific printing algorithms, support generation
- `tests/libnest2d/` - 2D nesting algorithm tests
- `tests/slic3rutils/` - Utility function tests
- `tests/sandboxes/` - Experimental/sandbox test code

Run all tests after building:
```bash
cd build && ctest
```

Run tests with verbose output:
```bash
cd build && ctest --output-on-failure
```

Run individual test suites:
```bash
# From build directory
./tests/libslic3r/libslic3r_tests
./tests/fff_print/fff_print_tests
./tests/sla_print/sla_print_tests
```

## Architecture

### Core Libraries
- **libslic3r/**: Core slicing engine and algorithms (platform-independent)
  - Main slicing logic, geometry processing, G-code generation
  - Key classes: Print, PrintObject, Layer, GCode, Config
  - Modular design with specialized subdirectories:
    - `GCode/` - G-code generation, cooling, pressure equalization, thumbnails
    - `Fill/` - Infill pattern implementations (gyroid, honeycomb, lightning, etc.)
    - `Support/` - Tree supports and traditional support generation
    - `Geometry/` - Advanced geometry operations, Voronoi diagrams, medial axis
    - `Format/` - File I/O for 3MF, AMF, STL, OBJ, STEP formats
    - `SLA/` - SLA-specific print processing and support generation
    - `Arachne/` - Advanced wall generation using skeletal trapezoidation

- **src/slic3r/**: Main application framework and GUI
  - GUI application built with wxWidgets
  - Integration between libslic3r core and user interface
  - Located in `src/slic3r/GUI/` (not shown in this directory but exists)

### Key Algorithmic Components
- **Arachne Wall Generation**: Variable-width perimeter generation using skeletal trapezoidation
- **Tree Supports**: Organic support generation algorithm  
- **Lightning Infill**: Sparse infill optimization for internal structures
- **Adaptive Slicing**: Variable layer height based on geometry
- **Multi-material**: Multi-extruder and soluble support processing
- **G-code Post-processing**: Cooling, fan control, pressure advance, conflict checking

### File Format Support
- **3MF/BBS_3MF**: Native format with extensions for multi-material and metadata
- **STL**: Standard tessellation language for 3D models
- **AMF**: Additive Manufacturing Format with color/material support  
- **OBJ**: Wavefront OBJ with material definitions
- **STEP**: CAD format support for precise geometry
- **G-code**: Output format with extensive post-processing capabilities

### External Dependencies
- **Clipper2**: Advanced 2D polygon clipping and offsetting
- **libigl**: Computational geometry library for mesh operations
- **TBB**: Intel Threading Building Blocks for parallelization
- **wxWidgets**: Cross-platform GUI framework
- **OpenGL**: 3D graphics rendering and visualization
- **CGAL**: Computational Geometry Algorithms Library (selective use)
- **OpenVDB**: Volumetric data structures for advanced operations
- **Eigen**: Linear algebra library for mathematical operations

## File Organization

### Resources and Configuration
- `resources/profiles/` - Printer and material profiles organized by manufacturer
- `resources/printers/` - Printer-specific configurations and G-code templates  
- `resources/images/` - UI icons, logos, calibration images
- `resources/calib/` - Calibration test patterns and data
- `resources/handy_models/` - Built-in test models (benchy, calibration cubes)

### Internationalization and Localization  
- `localization/i18n/` - Source translation files (.pot, .po)
- `resources/i18n/` - Runtime language resources
- Translation managed via `scripts/run_gettext.sh` / `scripts/run_gettext.bat`

### Platform-Specific Code
- `src/libslic3r/Platform.cpp` - Platform abstractions and utilities
- `src/libslic3r/MacUtils.mm` - macOS-specific utilities (Objective-C++)
- Windows-specific build scripts and configurations
- Linux distribution support scripts in `scripts/linux.d/`

### Build and Development Tools
- `cmake/modules/` - Custom CMake find modules and utilities
- `scripts/` - Python utilities for profile generation and validation  
- `tools/` - Windows build tools (gettext utilities)
- `deps/` - External dependency build configurations

## Development Workflow

### Code Style and Standards
- **C++17 standard** with selective C++20 features
- **Naming conventions**: PascalCase for classes, snake_case for functions/variables
- **Header guards**: Use `#pragma once`
- **Thread safety**: Use TBB for parallelization, be mindful of shared state

### Adding New Print Settings
1. Define setting in `src/libslic3r/PrintConfig.cpp` with bounds and defaults
2. Add UI controls in `src/slic3r/GUI/`
3. Update serialization in config save/load

### Adding Printer Profiles
Create JSON in `resources/profiles/[manufacturer].json` following existing structure. Start/end G-code templates go in `resources/printers/`.

## CLI Mode Architecture

`src/OrcaSlicer.cpp` contains the full CLI implementation in `CLI::run()`. Key patterns:

- `--load-settings` → machine/process JSON configs loaded into `m_extra_config`
- `--load-filaments` → filament JSONs stored in `load_filaments_config[]` (1-indexed, maps to extruder N)
- `--load-filament-ids` → per-object extruder assignment (object-level, not face-level)
- `--slice N` → slice plate N (0 = all plates)

**Important**: `load_config_file` is a **lambda** defined at line ~1872 inside `CLI::run()`. Code before that point must use `DynamicPrintConfig::load_from_json()` directly.

### OBJ Multi-color CLI Flow
OBJ face colors come from MTL files. The mapping pipeline:
1. `load_obj()` → parses MTL → stores per-face `RGBA` in `ObjInfo.face_colors`
2. `Model::read_from_file()` → calls `ObjImportColorFn` callback if provided
3. Callback receives `vector<RGBA> input_colors` → fills `filament_ids[]` + `first_extruder_id`
4. `obj_import_face_color_deal()` → writes `mmu_segmentation_facets` on the mesh volume

`ObjImportColorFn` signature (defined in `src/libslic3r/Format/OBJ.hpp`):
```cpp
void(vector<RGBA>& input_colors, bool is_single_color,
     vector<unsigned char>& filament_ids, unsigned char& first_extruder_id)
```
`RGBA` = `std::array<float,4>` (0–1 range). Use `decode_color(hex_string, ColorRGBA&)` to parse hex colors.

In GUI mode, this callback shows `ObjColorDialog` (see `src/slic3r/GUI/Plater.cpp` ~line 4173). In CLI mode, `cli_obj_color_fn` (added at line ~1356) does nearest-neighbor RGB matching against `--load-filaments` colors.

Helper script: `scripts/extract_obj_colors.py` — previews MTL→filament color matching and prints the CLI command.

## Important Notes

### Codebase Navigation
- Codebase has 500k+ lines — use search tools extensively
- `src/OrcaSlicer.cpp` — application startup + full CLI implementation
- `src/libslic3r/Print.cpp` — orchestrates the slicing pipeline
- `src/libslic3r/PrintConfig.cpp` — all print/printer/material settings
- `src/slic3r/GUI/Plater.cpp` — main workspace, model loading with GUI callbacks

### File Format Notes
- 3MF files must be the **first** input file in CLI mode when mixed with other formats
- `bbs_3mf.cpp` is 8900+ lines — the primary serialization format
- Profile JSON files use `"from": "system"` or `"from": "User"` to distinguish system vs user presets