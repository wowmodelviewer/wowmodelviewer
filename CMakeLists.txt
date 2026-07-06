cmake_minimum_required(VERSION 3.16)
project(WoWModelViewer)

# Dependencies.
# Select 32-bit vs 64-bit toolchain by the generator platform (-A Win32 | -A x64). The x64 build
# uses x64 Qt + wxWidgets 3.x + vcpkg natives; the Win32 build keeps the original 32-bit deps so
# both stay buildable from the same tree.
if(CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
  set(WMV_X64 ON)
  set(WMV_VCPKG_X64 "C:/Users/Rasmus Lund/vcpkg/installed/x64-windows")
  # static GLEW (libglew32.lib, /MD CRT) -- avoids the dynamic import-lib symbol mismatch; matches
  # the proven 32-bit static-GLEW setup.
  set(WMV_VCPKG_X64_STATIC "C:/Users/Rasmus Lund/vcpkg/installed/x64-windows-static-md")
endif()

# Qt5
if(WMV_X64)
  SET(QT_LOCATION C:/Qt/5.13.2/msvc2017_64)
else()
  SET(QT_LOCATION C:/Qt/5.13.2/msvc2017)
endif()
SET(CMAKE_PREFIX_PATH ${QT_LOCATION}/lib/cmake)
find_package(Qt5 5.13.2 COMPONENTS Core Network Widgets Xml Gui REQUIRED)

#wxwidgets
if(WMV_X64)
  # wxWidgets 3.2.x prebuilt x64 DLL build (vc14x). Point FindwxWidgets at its lib dir; it derives
  # the rest (setup.h under <libdir>/mswu, WXUSINGDLL) from there.
  SET(wxWidgets_ROOT_DIR ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets3)
  SET(wxWidgets_LIB_DIR ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets3/lib/vc14x_x64_dll)
  SET(wxWidgets_CONFIGURATION mswu)
  SET(CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets3)
else()
  SET(CMAKE_PREFIX_PATH ThirdParty/wxWidgets)
endif()
SET(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS net aui xml adv core base REQUIRED)
# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/ThirdParty/include)


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

# add external libraries compilation
add_subdirectory(ThirdParty/CxImage)

# configure CASC lib for our usage
# NOTE: these must be CACHE ... FORCE sets, not option(VAR ON) — option()'s 2nd arg
# is the help string, so option(CASC_UNICODE ON) silently leaves CASC_UNICODE OFF.
# With CascLib 3.0 that causes a UNICODE-mismatch #error since WMV builds _UNICODE.
set(CASC_UNICODE ON CACHE BOOL "Force CASC Unicode build to match WMV" FORCE)
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
set(STORM_UNICODE ON CACHE BOOL "Force StormLib Unicode build to match WMV" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "Do not build StormLib tests" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "Do not install StormLib" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build StormLib as a static library" FORCE)
add_subdirectory(ThirdParty/stormlib)
set_property(TARGET storm PROPERTY FOLDER "ThirdParty libraries")

# Main source files.
add_subdirectory(Source)

add_custom_target(release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target clean --config Release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
                  ${CMAKE_SOURCE_DIR}/Installers/NSIS/makensis.exe "${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
          )
