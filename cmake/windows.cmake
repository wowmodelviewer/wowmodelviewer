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

    # Fix LNK4272: ensure bare system library names (kernel32, user32, etc. from wxWidgets)
    # resolve to x64 versions. Derive correct paths from the compiler and SDK tool locations
    # so /LIBPATH entries are searched before the LIB environment variable.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # MSVC x64 lib: derive from CMAKE_CXX_COMPILER (.../MSVC/<ver>/bin/Hostx64/x64/cl.exe)
        get_filename_component(_msvc_bin_arch "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_msvc_bin_host "${_msvc_bin_arch}" DIRECTORY)
        get_filename_component(_msvc_bin      "${_msvc_bin_host}" DIRECTORY)
        get_filename_component(_msvc_root     "${_msvc_bin}" DIRECTORY)
        set(_msvc_lib_x64 "${_msvc_root}/lib/x64")

        # Windows SDK x64 libs: derive from CMAKE_RC_COMPILER (.../Windows Kits/10/bin/<ver>/x64/rc.exe)
        get_filename_component(_sdk_bin_arch "${CMAKE_RC_COMPILER}" DIRECTORY)
        get_filename_component(_sdk_bin_ver  "${_sdk_bin_arch}" DIRECTORY)
        get_filename_component(_sdk_ver      "${_sdk_bin_ver}" NAME)
        get_filename_component(_sdk_bin      "${_sdk_bin_ver}" DIRECTORY)
        get_filename_component(_sdk_root     "${_sdk_bin}" DIRECTORY)
        set(_sdk_um_x64   "${_sdk_root}/lib/${_sdk_ver}/um/x64")
        set(_sdk_ucrt_x64 "${_sdk_root}/lib/${_sdk_ver}/ucrt/x64")

        if(EXISTS "${_msvc_lib_x64}")
            link_directories("${_msvc_lib_x64}")
        endif()
        if(EXISTS "${_sdk_um_x64}")
            link_directories("${_sdk_um_x64}")
        endif()
        if(EXISTS "${_sdk_ucrt_x64}")
            link_directories("${_sdk_ucrt_x64}")
        endif()
    endif()
endif()
