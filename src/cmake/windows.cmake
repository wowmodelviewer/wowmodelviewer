message(STATUS "Using Windows version")

add_definitions(-D_WINDOWS)

if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  message(STATUS "Release build : Final exe will be stripped")
  set(CMAKE_EXE_LINKER_FLAGS "-s") # strip exe
else()
  message(STATUS "${CMAKE_BUILD_TYPE} build : Final exe will NOT be stripped")
endif()
  
# disable some visual studio annoying warnings
# warning on stl class dll exporting
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
# precompiler secure warnings (too much as WMV code is old)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  
if(NOT ${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /INCREMENTAL:NO")
endif()
