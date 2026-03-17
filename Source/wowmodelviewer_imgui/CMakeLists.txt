# wowmodelviewer_imgui - parallel migration target (GLFW + Dear ImGui)
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Include the wowmodelviewer directory for OrbitCamera.h
include_directories(${CMAKE_SOURCE_DIR}/Source/wowmodelviewer)

set(src main_imgui.cpp
        ${CMAKE_SOURCE_DIR}/Source/wowmodelviewer/OrbitCamera.cpp)

# External dependencies
use_glad()
use_imgui()
use_wow()   # brings in core, casclib, wow (video, Attachment, WoWModel, etc.)

set(NAME wowmodelviewer_imgui)
add_executable(${NAME} WIN32 ${src})

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

# Force-include glad/gl.h before all source files (same as the wxWidgets target)
if(MSVC)
  target_compile_options(${NAME} PRIVATE /FIglad/gl.h)
endif()

target_link_libraries(${NAME}
  ${extralibs}
  opengl32
  glu32
  winhttp
  pugixml-static
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/libpng.lib
)

# On MSVC, use main() instead of WinMain for the console-friendly entry point.
if(MSVC)
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")
install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})

# Install WoW version support files (database.xml, AnimationData.csv, etc.)
# so the game loading code can find them at games/wow/X.Y/ relative to bin/.
file(GLOB _wow_ver_dirs LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/bin_support/wow/*)
foreach(_dir ${_wow_ver_dirs})
  if(IS_DIRECTORY ${_dir})
    get_filename_component(_ver ${_dir} NAME)
    file(GLOB _ver_files ${_dir}/*)
    install(FILES ${_ver_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/${_ver})
  endif()
endforeach()
