file(GLOB WOWMV_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} *.cpp)
list(REMOVE_ITEM WOWMV_SOURCES particle_test.cpp modelexport_fbx.cpp AVIGenerator.cpp)