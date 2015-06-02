project(core)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building nextgen Core")

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

find_package(wxWidgets COMPONENTS core base REQUIRED)

include_directories(.)
include_directories(../)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp ../metaclasses/*.cpp ../logger/*.cpp)

add_definitions(-DBUILDING_CORE_DLL)

add_library(core SHARED ${src})

target_link_libraries(core ${wxWidgets_LIBRARIES} Qt5::Core Qt5::Network)

if(WIN32)
  install(TARGETS core RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/")
endif(WIN32)
