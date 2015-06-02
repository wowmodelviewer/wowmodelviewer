project(nextgen)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Using Next Gen files")

include_directories(.)

# add core files compilation
add_subdirectory(core)

# add wow related files lib compilation
add_subdirectory(games)

# add plugins compilation
add_subdirectory(plugins)

# add update manager compilation
# add_subdirectory(UpdateManager)        
