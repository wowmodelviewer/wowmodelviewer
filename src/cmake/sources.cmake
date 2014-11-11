file(GLOB WOWMV_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} *.cpp)
list(APPEND WOWMV_SOURCES sqlite3.c)
list(REMOVE_ITEM WOWMV_SOURCES particle_test.cpp modelexport_fbx.cpp modelexport_lwo.cpp modelexport_obj.cpp AVIGenerator.cpp)