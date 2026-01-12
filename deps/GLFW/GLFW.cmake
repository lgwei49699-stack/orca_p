if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Enable both X11 and OSMesa support for maximum flexibility
    # X11 is used when DISPLAY is available (xvfb-run, normal X session)
    # OSMesa is used only in true headless environments
    set(_glfw_build_x11 "-DGLFW_BUILD_X11=ON")
    set(_glfw_disable_wayland "-DGLFW_BUILD_WAYLAND=OFF")
    set(_glfw_use_osmesa "-DGLFW_USE_OSMESA=ON")
else()
    set(_glfw_build_x11 "")
    set(_glfw_disable_wayland "")
    set(_glfw_use_osmesa "")
endif()

orcaslicer_add_cmake_project(GLFW
    URL https://github.com/glfw/glfw/archive/refs/tags/3.3.7.zip
    URL_HASH SHA256=e02d956935e5b9fb4abf90e2c2e07c9a0526d7eacae8ee5353484c69a2a76cd0
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=${_build_shared} 
        -DGLFW_BUILD_DOCS=OFF
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        ${_glfw_build_x11}
        ${_glfw_disable_wayland}
        ${_glfw_use_osmesa}
)

if (MSVC)
    add_debug_dep(dep_GLFW)
endif ()
