cmake_minimum_required(VERSION 3.16)
project(Wdc3Strings LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(WMV_SOURCE_DIR "" CACHE PATH "WMV source tree whose headers match WMV_BUILD_DIR")
set(WMV_BUILD_DIR "" CACHE PATH "WMV build holding Release core.lib and wow.lib")
if(NOT WMV_SOURCE_DIR OR NOT WMV_BUILD_DIR)
  message(FATAL_ERROR "Pass -DWMV_SOURCE_DIR=<checkout> and -DWMV_BUILD_DIR=<its Release build>")
endif()

# Use the Qt the WMV build was configured with, unless one is given.
if(NOT Qt5Core_DIR)
  load_cache("${WMV_BUILD_DIR}" READ_WITH_PREFIX WMV_ Qt5Core_DIR)
  set(Qt5Core_DIR "${WMV_Qt5Core_DIR}" CACHE PATH "Qt5Core package of the WMV build" FORCE)
endif()
find_package(Qt5Core REQUIRED)

# The production WDC3File::open() and get() from wow.dll; nothing of the reader is compiled here.
add_executable(wdc3-strings regression.cpp)
# types.h picks its fixed-width types by _WINDOWS, as the WMV build defines it.
target_compile_definitions(wdc3-strings PRIVATE _WINDOWS)
target_include_directories(wdc3-strings PRIVATE "${WMV_SOURCE_DIR}/Source/core" "${WMV_SOURCE_DIR}/Source/games/wow")
target_link_libraries(wdc3-strings PRIVATE "${WMV_BUILD_DIR}/Source/core/Release/core.lib"
  "${WMV_BUILD_DIR}/Source/games/wow/Release/wow.lib" Qt5::Core)

# The reader under test and its runtime beside the executable: the wow.dll and core.dll of the same build (a wow.dll
# with another WDC3File layout than the headers compiled against would corrupt the test's own members), and Qt.
get_filename_component(qt_bin "${Qt5Core_DIR}/../../../bin" ABSOLUTE)
add_custom_command(TARGET wdc3-strings POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${WMV_BUILD_DIR}/Source/games/wow/Release/wow.dll" "${WMV_BUILD_DIR}/Source/core/Release/core.dll"
  "${qt_bin}/Qt5Core.dll" "${qt_bin}/Qt5Gui.dll" "${qt_bin}/Qt5Network.dll" "${qt_bin}/Qt5Xml.dll"
  "$<TARGET_FILE_DIR:wdc3-strings>")
