cmake_minimum_required(VERSION 3.21)
project(WoWModelViewer C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Platforms: Windows (MSVC, x64), macOS (Apple clang, arm64 / x86_64) and Linux (GCC or clang).
# The viewport renders natively with Vulkan (MoltenVK on macOS); there is no Unity player.
#
# Dependencies are found through CMAKE_PREFIX_PATH on every platform (Homebrew, vcpkg, a Qt
# installer or the distribution's packages). The Windows variables below keep the original
# developer layout as a default and can be overridden on the command line.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WMV_X64 ON)
  set(WMV_VCPKG_X64 "C:/Users/Rasmus Lund/vcpkg/installed/x64-windows" CACHE PATH "vcpkg x64-windows install")
  set(WMV_VCPKG_X64_STATIC "C:/Users/Rasmus Lund/vcpkg/installed/x64-windows-static-md" CACHE PATH
      "vcpkg x64-windows-static-md install")
  set(QT_LOCATION "C:/Qt/5.13.2/msvc2017_64" CACHE PATH "Qt 5 install (the folder that holds lib/cmake)")
  list(APPEND CMAKE_PREFIX_PATH ${QT_LOCATION}/lib/cmake ${WMV_VCPKG_X64})
  if(EXISTS ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets33/lib/vc14x_x64_dll)
    # wxWidgets 3.3.x prebuilt x64 DLL build (vc14x), unpacked in ThirdParty/wxWidgets33.
    set(wxWidgets_ROOT_DIR ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets33)
    set(wxWidgets_LIB_DIR ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets33/lib/vc14x_x64_dll)
    set(wxWidgets_CONFIGURATION mswu)
  endif()
elseif(APPLE)
  # Homebrew's qt@5 is keg-only, so it is not on the default prefix path.
  foreach(_brew /opt/homebrew /usr/local)
    if(EXISTS ${_brew}/opt/qt@5/lib/cmake)
      list(APPEND CMAKE_PREFIX_PATH ${_brew}/opt/qt@5 ${_brew})
    endif()
  endforeach()
endif()

find_package(Qt5 5.13 COMPONENTS Core Network Widgets Xml Gui REQUIRED)
get_target_property(_qtcore_location Qt5::Core LOCATION)
get_filename_component(QT_LIB_DIR ${_qtcore_location} DIRECTORY)
if(NOT QT_LOCATION)
  get_filename_component(QT_LOCATION ${Qt5_DIR}/../../.. ABSOLUTE)
endif()

set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets 3.2 COMPONENTS net aui xml adv core base REQUIRED)
include(${wxWidgets_USE_FILE})
# (ThirdParty/include, old copies of libpng/libjpeg/zlib headers and the FBX SDK's, is not on the global path: those
# headers would shadow the libraries' own. Its users add it themselves.)
include_directories(${wxWidgets_INCLUDE_DIRS})

# The viewport.
find_package(Vulkan REQUIRED COMPONENTS glslc)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER QTAutoMoc)

if(DEFINED BUILDNUMBER)
  add_definitions(-D_BUILDNUMBER=${BUILDNUMBER})
endif()

# The vendored CascLib and StormLib declare an old cmake_minimum_required that CMake 4 refuses.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# add external libraries compilation
add_subdirectory(ThirdParty/CxImage)

# configure CASC lib for our usage
# NOTE: these must be CACHE ... FORCE sets, not option(VAR ON) — option()'s 2nd arg
# is the help string, so option(CASC_UNICODE ON) silently leaves CASC_UNICODE OFF.
# With CascLib 3.0 that causes a UNICODE-mismatch #error since WMV builds _UNICODE.
# CascLib's TCHAR is wchar_t only on Windows; elsewhere it is char and the option does not apply.
set(CASC_UNICODE ${WIN32} CACHE BOOL "Force CASC Unicode build to match WMV" FORCE)
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "Do not build the CASC shared lib" FORCE)
set(CASC_BUILD_TESTS OFF CACHE BOOL "Do not build CASC tests" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building" FORCE)
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
set(CMAKE_INSTALL_PREFIX ${CASCLIB_INSTALL_LOCATION})
add_subdirectory(ThirdParty/casclib)
set_property(TARGET casc_static PROPERTY FOLDER "ThirdParty libraries")

# configure StormLib (legacy MoPaQ archives, for old Vanilla/TBC/WotLK clients) -- mirrors the
# casclib vendoring above. StormLib MUST be built UNICODE to match WMV's _UNICODE build: its
# archive-path API is TCHAR-based, so an ANSI StormLib linked against a _UNICODE consumer would
# mismatch TCHAR (wchar_t vs char) exactly like the CASC_UNICODE case noted above. Static lib,
# no tests, no install.
set(STORM_UNICODE ${WIN32} CACHE BOOL "Force StormLib Unicode build to match WMV" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "Do not build StormLib tests" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "Do not install StormLib" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build StormLib as a static library" FORCE)
add_subdirectory(ThirdParty/stormlib)
set_property(TARGET storm PROPERTY FOLDER "ThirdParty libraries")

# Main source files.
add_subdirectory(Source)


