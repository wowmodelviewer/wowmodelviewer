# wowmodelviewer_imgui - parallel migration target (GLFW + Dear ImGui)
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(src main_imgui.cpp)

# External dependencies
use_glad()
use_imgui()

set(NAME wowmodelviewer_imgui)
add_executable(${NAME} WIN32 ${src})

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME}
  ${extralibs}
  opengl32
)

# On MSVC, use main() instead of WinMain for the console-friendly entry point.
if(MSVC)
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")
install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
