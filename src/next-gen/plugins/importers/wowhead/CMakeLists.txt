project(armory)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Using wowhead importer")

find_package(wxWidgets COMPONENTS net core base REQUIRED)

file(GLOB src *.cpp )

add_library(wowhead STATIC ${src})
target_link_libraries(wowhead ${wxWidgets_LIBRARIES})
