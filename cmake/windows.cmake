message(STATUS "Using Windows version")

add_definitions(-D_WINDOWS)

# disable some visual studio annoying warnings
# warning on stl class dll exporting
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
# precompiler secure warnings (too much as WMV code is old)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  
# force Unicode compilation
add_definitions(-DUNICODE -D_UNICODE)

# Fix LNK4098 warning by ignoring conflicting CRT libraries
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
    # Debug: also suppress dynamic release CRT pulled in by release-only third-party libs
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:MSVCRT")
endif()
