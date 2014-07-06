project(armory)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building armory importer")

find_package(wxWidgets COMPONENTS net core base REQUIRED)

# Qt5 stuff
find_package(Qt5Core)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

file(GLOB src *.cpp )

add_library(armory SHARED ${src})

add_dependencies(armory core)

target_link_libraries(armory ${wxWidgets_LIBRARIES} Qt5::Core core)

if(WIN32)
  install(TARGETS armory RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/plugins")
endif(WIN32)


        