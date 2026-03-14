message(STATUS "Using Windows version")

add_definitions(-D_WINDOWS)
# Prevent windows.h from including winsock.h (conflicts with winsock2.h used by wxWidgets 3.x)
add_definitions(-DWIN32_LEAN_AND_MEAN)
# Prevent windows.h from defining min/max macros (conflicts with glm and STL)
add_definitions(-DNOMINMAX)
# Suppress C++17 std::byte injection into global namespace; conflicts with Windows SDK byte typedef
add_definitions(-D_HAS_STD_BYTE=0)

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
    # wxWidgets 3.x static lib debug builds can produce duplicate symbols for template instantiations
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /FORCE:MULTIPLE")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /FORCE:MULTIPLE")
endif()
