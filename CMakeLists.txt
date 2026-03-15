cmake_minimum_required(VERSION 3.16)

project(WoWModelViewer)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use dynamic CRT (/MD release, /MDd debug) to match vcpkg x64-windows-static-md libraries.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
message(STATUS "=== Configuring WoWModelViewer ===")

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
