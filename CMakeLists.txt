cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Force x64 MSVC compiler.
# With the Ninja generator the architecture "strategy" must be "external", so
# CMake uses whatever cl.exe is first on PATH.  If the environment was not set
# up via 'vcvarsall x64' this can silently pick up the x86 compiler.  Detect
# that situation and locate the x64 cl.exe automatically via vswhere before
# project() runs.
# ---------------------------------------------------------------------------
if(CMAKE_HOST_WIN32 AND NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(_vswhere vswhere
    PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
    NO_DEFAULT_PATH
  )
  if(_vswhere)
    execute_process(
      COMMAND "${_vswhere}" -latest -products *
              -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
              -property installationPath
      OUTPUT_VARIABLE _vs_path
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_vs_path)
      set(_vc_ver_file "${_vs_path}/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt")
      if(EXISTS "${_vc_ver_file}")
        file(READ "${_vc_ver_file}" _vc_ver)
        string(STRIP "${_vc_ver}" _vc_ver)
        set(_cl_x64 "${_vs_path}/VC/Tools/MSVC/${_vc_ver}/bin/Hostx64/x64/cl.exe")
        if(EXISTS "${_cl_x64}")
          set(CMAKE_C_COMPILER "${_cl_x64}" CACHE FILEPATH "" FORCE)
          set(CMAKE_CXX_COMPILER "${_cl_x64}" CACHE FILEPATH "" FORCE)
          message(STATUS "Auto-selected x64 MSVC compiler: ${_cl_x64}")
        endif()
      endif()
    endif()
  endif()
endif()

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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use dynamic CRT (/MD release, /MDd debug) to match vcpkg x64-windows-static-md libraries.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
message(STATUS "=== Configuring WoWModelViewer ===")

# Auto-install third-party dependencies on a fresh clone.
# Uses Qt6Config.cmake as a sentinel – if absent, runs the full setup script.
set(_qt6_sentinel "${CMAKE_SOURCE_DIR}/ThirdParty/Qt/6.8.3/6.8.3/msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake")
if(WIN32 AND NOT EXISTS "${_qt6_sentinel}")
  message(STATUS "Dependencies not found – running scripts/setup_x64_deps.ps1 ...")
  execute_process(
    COMMAND powershell.exe -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/setup_x64_deps.ps1"
            -RepoRoot "${CMAKE_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _setup_rc
  )
  if(NOT _setup_rc EQUAL 0)
    message(FATAL_ERROR "setup_x64_deps.ps1 failed (exit code ${_setup_rc}). See output above.")
  endif()
endif()

# Dependencies.
# Qt6
message(STATUS "Finding Qt6...")
set(QT_LOCATION "${CMAKE_SOURCE_DIR}/ThirdParty/Qt/6.8.3/6.8.3/msvc2022_64")
list(PREPEND CMAKE_PREFIX_PATH "${QT_LOCATION}/lib/cmake")
find_package(Qt6 6.8.3 COMPONENTS Core Network Widgets Xml Gui REQUIRED)
message(STATUS "  Qt6 found: ${Qt6_VERSION}")

# WxWidgets
message(STATUS "Finding wxWidgets...")
set(wxWidgets_ROOT_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64")
set(wxWidgets_LIB_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64/lib/vc_x64_lib")
set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS gl net aui xml adv core base REQUIRED)
message(STATUS "  wxWidgets found: ${wxWidgets_LIBRARIES}")
# Include WxWidgets headers
include(${wxWidgets_USE_FILE})
# FindWxWidgets incorrectly injects /MT based on the vc_x64_lib directory name.
# Strip it so our CMAKE_MSVC_RUNTIME_LIBRARY setting (/MDd, /MD) is not overridden.
if(MSVC)
  foreach(_f CMAKE_CXX_FLAGS CMAKE_C_FLAGS)
    string(REGEX REPLACE "/M[Td]+" "" ${_f} "${${_f}}")
    string(STRIP "${${_f}}" ${_f})
  endforeach()
endif()
include_directories(${wxWidgets_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/ThirdParty/include")


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

# CascLib
message(STATUS "Adding CascLib...")
set(CASC_UNICODE ON CACHE BOOL "Use Unicode in CascLib" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building")
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "Compile dynamically linked library")
include_directories(ThirdParty/casclib/src)
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
set(CMAKE_INSTALL_PREFIX ${CASCLIB_INSTALL_LOCATION})
# Point CascLib's find_package(ZLIB) to our ThirdParty zlib 1.3.1 so it
# does not fall back to its bundled (older) copy.
set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/include" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY "${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib" CACHE FILEPATH "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.16)
add_subdirectory(ThirdParty/casclib)
set_property(TARGET casc_static PROPERTY FOLDER "ThirdParty libraries")

# Main source files.
message(STATUS "Adding Source...")
add_subdirectory(Source)

add_custom_target(release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target clean --config Release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
                  ${CMAKE_SOURCE_DIR}/Installers/NSIS/makensis.exe "${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
          )
