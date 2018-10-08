project(ListFileGenerator)
include(${WMV_BASE_PATH}/src/cmake/common.cmake)

cmake_minimum_required(VERSION 2.6)
message(STATUS "Building ListFileGenerator")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

# Qt5 stuff
set(CMAKE_PREFIX_PATH $ENV{WMV_SDK_BASEDIR}/Qt/lib/cmake)
find_package(Qt5Core)

set(src main.cpp)
        
use_wow()

set(NAME ListFileGenerator)
add_executable(${NAME} ${src})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME} wow Qt5::Core)

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:"_CONSOLE")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:RELWITHDEBINFO>:"_CONSOLE")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

	set(OUTPUT_DIR "../bin/$(Configuration)/$(PlatformName)/")
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_DIR})
endif()

install(TARGETS ListFileGenerator RUNTIME DESTINATION ${WMV_BASE_PATH}/bin)