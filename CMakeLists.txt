cmake_minimum_required(VERSION 3.16)

# Compiler, Windows SDK, and INCLUDE / LIB / PATH / RC settings are resolved by the MSVC x64
# toolchain file (cmake/toolchains/msvc-x64.cmake), which the presets chainload through vcpkg
# (VCPKG_CHAINLOAD_TOOLCHAIN_FILE). The build is therefore self-contained and does NOT require
# a Visual Studio developer prompt / vcvars64. Always configure via a preset (msvc-debug-local,
# msvc-release-local, msvc-relwithdebinfo-local).

project(WoWModelViewer)

# Reject x86 – this project is x64-only.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "32-bit (x86) compiler detected but this project requires x64.\n"
    "Detected compiler: ${CMAKE_CXX_COMPILER}\n"
    "Open an 'x64 Native Tools Command Prompt' or let VS select the x64-Debug/x64-Release preset."
  )
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use dynamic CRT (/MD release, /MDd debug) to match vcpkg x64-windows-static-md libraries.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
message(STATUS "=== Configuring WoWModelViewer ===")

# Auto-install third-party dependencies (zlib, libpng, FBX SDK, vcredist)
# at configure time via pure CMake — no external scripts required.
include(${CMAKE_SOURCE_DIR}/cmake/dependencies.cmake)

# Dependencies.
include_directories("${CMAKE_SOURCE_DIR}/ThirdParty/include")


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(DEFINED BUILDNUMBER)
  add_definitions(-D_BUILDNUMBER=${BUILDNUMBER})
endif()

# glad2 — OpenGL loader generator
message(STATUS "Adding glad2...")
add_subdirectory(ThirdParty/glad/cmake)
glad_add_library(glad_gl
  REPRODUCIBLE
  LOADER
  API gl:compatibility=4.6
  API wgl=1.0
  EXTENSIONS
    GL_ARB_fragment_program GL_ARB_vertex_program
    GL_ARB_shading_language_100 GL_ARB_multitexture
    GL_ARB_texture_non_power_of_two GL_ARB_vertex_buffer_object
    GL_ARB_point_sprite GL_ARB_point_parameters
    GL_ARB_texture_rectangle GL_ARB_multisample
    GL_ARB_texture_compression GL_ARB_texture_cube_map
    GL_EXT_texture_filter_anisotropic GL_EXT_draw_range_elements
    GL_EXT_framebuffer_object GL_EXT_texture_compression_s3tc
    WGL_ARB_pbuffer WGL_ARB_render_texture
    WGL_ARB_multisample WGL_ARB_pixel_format
)
set_property(TARGET glad_gl PROPERTY FOLDER "ThirdParty libraries")

# CascLib
message(STATUS "Adding CascLib...")
set(CASC_UNICODE ON CACHE BOOL "Use Unicode in CascLib" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building")
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "Compile dynamically linked library")
include_directories(ThirdParty/casclib/src)
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
set(CMAKE_INSTALL_PREFIX ${CASCLIB_INSTALL_LOCATION})
# ZLIB_INCLUDE_DIR / ZLIB_LIBRARY are set by cmake/dependencies.cmake so
# CascLib's find_package(ZLIB) finds our vcpkg copy.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
add_subdirectory(ThirdParty/casclib)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
set_property(TARGET casc_static PROPERTY FOLDER "ThirdParty libraries")

# pugixml — lightweight XML parser
message(STATUS "Adding pugixml...")
add_subdirectory(ThirdParty/pugixml)
set_property(TARGET pugixml-static PROPERTY FOLDER "ThirdParty libraries")

# nlohmann/json — header-only JSON library
message(STATUS "Adding nlohmann/json...")
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(ThirdParty/json)
set_property(TARGET nlohmann_json PROPERTY FOLDER "ThirdParty libraries")

# GLFW — cross-platform window/context/input library
message(STATUS "Adding GLFW...")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(ThirdParty/glfw)
set_property(TARGET glfw PROPERTY FOLDER "ThirdParty libraries")

# Dear ImGui — immediate-mode GUI library (docking branch)
message(STATUS "Adding Dear ImGui...")
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/imgui")
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${IMGUI_DIR}/misc/cpp
)
target_link_libraries(imgui PUBLIC glfw glad_gl)
# Tell the OpenGL3 backend we supply our own loader (glad).
# Force-include glad/gl.h so GL types and functions are available in the backend sources.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
if(MSVC)
  target_compile_options(imgui PRIVATE /FIglad/gl.h)
endif()
set_property(TARGET imgui PROPERTY FOLDER "ThirdParty libraries")

# Documentation (optional — only when Doxygen is installed).
include(${CMAKE_SOURCE_DIR}/cmake/docs.cmake)

# Main source files.
message(STATUS "Adding Source...")
add_subdirectory(Source)

add_custom_target(release
  COMMAND "${CMAKE_COMMAND}" --build . --target clean   --config Release
  COMMAND "${CMAKE_COMMAND}" --build . --target install  --config Release
  COMMAND ${CMAKE_SOURCE_DIR}/Installers/NSIS/makensis.exe
          "${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
)
