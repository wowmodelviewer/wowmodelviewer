# ListFileGenerator ext
include(${WMV_BASE_PATH}/src/cmake/common.cmake)

message(STATUS "Building ListFileGenerator")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

set(src main.cpp)
        
use_wow()

set(NAME ListFileGenerator)
add_executable(${NAME} ${src})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME} wow Qt5::Core)

set(BIN_DIR "${WMV_BASE_PATH}/bin/")

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS _CONSOLE)

	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})