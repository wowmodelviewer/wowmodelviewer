# wowmodelviewer_imgui - parallel migration target (GLFW + Dear ImGui)
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Exporter headers (OBJ / FBX)
include_directories(${CMAKE_SOURCE_DIR}/Source/exporters/obj)
include_directories(${CMAKE_SOURCE_DIR}/Source/exporters/fbx)
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/include)  # fbxsdk.h

# Importer headers (Armory / Wowhead)
include_directories(${CMAKE_SOURCE_DIR}/Source/importers/armory)
include_directories(${CMAKE_SOURCE_DIR}/Source/importers/wowhead)

set(src main.cpp
        OrbitCamera.cpp)

# Generate the resource file that embeds the application icon.
set(WMV_ICO_PATH "${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv.ico")
configure_file(wowmodelviewer.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wowmodelviewer.rc" @ONLY)

# External dependencies
use_glad()
use_imgui()
use_wow()   # brings in core, casclib, wow (video, Attachment, WoWModel, etc.)

set(NAME wowmodelviewer)
add_executable(${NAME} ${src} "${CMAKE_CURRENT_BINARY_DIR}/wowmodelviewer.rc")

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

# Pass the absolute path to the window icon PNG so the code works from any working directory.
target_compile_definitions(${NAME} PRIVATE
  WMV_ICON_PATH="${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv_16.png")

# Force-include glad/gl.h before all source files
if(MSVC)
  target_compile_options(${NAME} PRIVATE /FIglad/gl.h)
endif()

target_link_libraries(${NAME}
  ${extralibs}
  opengl32
  glu32
  winhttp
  pugixml-static
  objexporter
  fbxexporter
  armory
  wowhead
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/libpng.lib
)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")
install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})

# Install icon files next to the exe so the NSIS installer and runtime can find them.
install(FILES
  ${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv.ico
  ${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv_16.png
  DESTINATION ${BIN_DIR}
)

# Install shared WoW support files (CSV data files, tables.txt).
file(GLOB _wow_shared_files ${CMAKE_SOURCE_DIR}/bin_support/wow/*.csv ${CMAKE_SOURCE_DIR}/bin_support/wow/*.txt)
if(_wow_shared_files)
  install(FILES ${_wow_shared_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow)
endif()

# Install DBD definitions directory if present.
if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_support/wow/dbdefs)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/bin_support/wow/dbdefs
    DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow)
endif()
