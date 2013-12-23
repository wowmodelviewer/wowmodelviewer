project(nextgen)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Using Next Gen files")

include_directories(.)
include_directories(../)

file(GLOB src core/*.cpp)

# add plugins compilation (static compilation for now)
add_subdirectory(plugins)

add_library(nextgen STATIC ${src})

add_dependencies(nextgen wowhead)
add_dependencies(nextgen armory)

target_link_libraries(nextgen wowhead armory)
        
