project(nextgen)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Using Next Gen files")

# Qt dependency
#set(QT_USE_QTCORE 1)
#find_package(Qt4 REQUIRED)
#include(${QT_USE_FILE})

file(GLOB src *.cpp)

add_library(nextgen STATIC ${src})
#target_link_libraries(nextgen ${QT_LIBRARIES})
        
