# ListFileGenerator ext
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building ListFileGenerator")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

set(src main.cpp)
        
use_wow()

set(NAME ListFileGenerator)
add_executable(${NAME} ${src})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME} wow Qt5::Core)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
