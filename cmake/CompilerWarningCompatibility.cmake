# Probe optional diagnostics instead of assuming AppleClang and upstream Clang
# version numbers imply the same warning support.
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 15)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Wenum-constexpr-conversion" ORCA_HAS_ENUM_CONSTEXPR_CONVERSION)
    if(ORCA_HAS_ENUM_CONSTEXPR_CONVERSION)
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-error=enum-constexpr-conversion>")
    endif()
endif()

# Apple ld warns about repeated archives in transitive dependencies. Keep the
# existing ordering and only quiet this diagnostic when the linker supports it.
# CheckLinkerFlag is unavailable on the project's oldest supported CMake.
if(APPLE AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" ORCA_HAS_NO_WARN_DUPLICATE_LIBRARIES)
    if(ORCA_HAS_NO_WARN_DUPLICATE_LIBRARIES)
        add_link_options("LINKER:-no_warn_duplicate_libraries")
    endif()
endif()
