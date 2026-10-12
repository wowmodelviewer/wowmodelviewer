cmake_minimum_required(VERSION 3.20)
project(CascWholeRead LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(WMV_SOURCE_DIR "" CACHE PATH "WMV source tree whose CASCFolder.cpp is tested")
set(WMV_BUILD_DIR "" CACHE PATH "Release build of that tree (core.lib and core.dll)")
if(NOT WMV_SOURCE_DIR OR NOT WMV_BUILD_DIR)
  message(FATAL_ERROR "Pass -DWMV_SOURCE_DIR=<checkout> and -DWMV_BUILD_DIR=<its Release build>")
endif()

# Qt: the installation the WMV build was configured with, unless one is given.
if(NOT QT_ROOT)
  load_cache("${WMV_BUILD_DIR}" READ_WITH_PREFIX WMV_ Qt5Core_DIR)
  get_filename_component(qt "${WMV_Qt5Core_DIR}/../../.." ABSOLUTE)
  set(QT_ROOT "${qt}" CACHE PATH "Qt 5 installation")
endif()
find_package(Qt5Core REQUIRED PATHS "${QT_ROOT}/lib/cmake/Qt5Core" NO_DEFAULT_PATH)

# Compile the production CASCFolder.cpp itself. Its CascLib calls are answered by
# the scripted stubs in regression.cpp, so no game data and no CascLib are used.
# CASCFile.h is included but not used there; it is left out so the test does not
# need the CASCFile class.
set(folder "${WMV_SOURCE_DIR}/Source/games/wow/CASCFolder.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${folder}")
file(READ "${folder}" source)
string(REPLACE "#include \"CASCFile.h\"" "" source "${source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CASCFolder.cpp" "${source}")

add_executable(casc-whole-read regression.cpp "${CMAKE_CURRENT_BINARY_DIR}/CASCFolder.cpp")
# The wow library's own settings: CascLib in its Unicode form, CASCFolder defined
# (exported) here rather than imported from wow.dll.
target_compile_definitions(casc-whole-read PRIVATE BUILDING_WOW_DLL UNICODE _UNICODE CASCLIB_UNICODE
  CASCLIB_DETECT_UNICODE_MISMATCHES CASCLIB_NO_AUTO_LINK_LIBRARY _CRT_SECURE_NO_WARNINGS)
target_include_directories(casc-whole-read PRIVATE "${WMV_SOURCE_DIR}/Source/games/wow"
  "${WMV_SOURCE_DIR}/Source/core" "${WMV_SOURCE_DIR}/ThirdParty/casclib/src")
target_link_libraries(casc-whole-read PRIVATE "${WMV_BUILD_DIR}/Source/core/Release/core.lib" Qt5::Core)

# The runtime it needs beside it: the core.dll that matches core.lib, and Qt.
add_custom_command(TARGET casc-whole-read POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${WMV_BUILD_DIR}/Source/core/Release/core.dll" "${QT_ROOT}/bin/Qt5Core.dll" "${QT_ROOT}/bin/Qt5Gui.dll"
  "${QT_ROOT}/bin/Qt5Network.dll" "${QT_ROOT}/bin/Qt5Xml.dll" "$<TARGET_FILE_DIR:casc-whole-read>")
