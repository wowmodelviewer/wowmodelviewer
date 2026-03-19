# cmake/dependencies.cmake
# Downloads and configures external dependencies that were previously handled
# by scripts/setup_x64_deps.ps1.  Runs automatically at configure time.
# Requires: vcpkg toolchain (set via CMAKE_TOOLCHAIN_FILE or VCPKG_ROOT),
#           7-Zip on PATH or at its default install location (for FBX SDK).

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Helper: find 7-Zip (needed to extract the FBX SDK NSIS installer)
# ---------------------------------------------------------------------------
function(_find_7zip out_var)
  find_program(_7z 7z PATHS "$ENV{ProgramFiles}/7-Zip" NO_CACHE)
  if(_7z)
    set(${out_var} "${_7z}" PARENT_SCOPE)
  else()
    message(FATAL_ERROR
      "7-Zip not found.  Install from https://7-zip.org or ensure 7z is on PATH.")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# 1. zlib + libpng  (via vcpkg – consumed through find_package)
# ---------------------------------------------------------------------------
message(STATUS "Finding zlib...")
find_package(ZLIB REQUIRED)

message(STATUS "Finding libpng...")
find_package(PNG REQUIRED)

# Expose imported targets under short names used by the rest of the build.
# Also write the static libs into ThirdParty/lib/x64 so CascLib's own
# find_package(ZLIB) and legacy absolute-path references keep working.
get_target_property(_zlib_lib ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
if(NOT _zlib_lib)
  get_target_property(_zlib_lib ZLIB::ZLIB IMPORTED_LOCATION)
endif()
get_target_property(_zlib_inc ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)

get_target_property(_png_lib PNG::PNG IMPORTED_LOCATION_RELEASE)
if(NOT _png_lib)
  get_target_property(_png_lib PNG::PNG IMPORTED_LOCATION)
endif()

set(_lib_dst "${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64")
file(MAKE_DIRECTORY "${_lib_dst}")

# Copy libs so hard-coded paths in sub-projects keep working.
if(_zlib_lib AND NOT EXISTS "${_lib_dst}/zlib.lib")
  file(COPY_FILE "${_zlib_lib}" "${_lib_dst}/zlib.lib")
endif()
if(_png_lib AND NOT EXISTS "${_lib_dst}/libpng.lib")
  file(COPY_FILE "${_png_lib}" "${_lib_dst}/libpng.lib")
endif()

# Point CascLib's find_package(ZLIB) at the vcpkg copy.
set(ZLIB_INCLUDE_DIR "${_zlib_inc}" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY     "${_lib_dst}/zlib.lib" CACHE FILEPATH "" FORCE)

# ---------------------------------------------------------------------------
# 2. FBX SDK 2020.3.9  (download + extract at configure time)
# ---------------------------------------------------------------------------
set(_fbx_inc_dst "${CMAKE_SOURCE_DIR}/ThirdParty/include")
set(_fbx_lib_dst "${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64")
set(_fbx_sentinel "${_fbx_inc_dst}/fbxsdk.h")

if(NOT EXISTS "${_fbx_sentinel}")
  message(STATUS "FBX SDK not found – downloading 2020.3.9 ...")

  _find_7zip(_7z_exe)

  set(_fbx_tmp "${CMAKE_BINARY_DIR}/_deps/fbx_download")
  file(MAKE_DIRECTORY "${_fbx_tmp}")

  set(_fbx_installer "${_fbx_tmp}/fbx202039_fbxsdk_vs2022_win.exe")
  if(NOT EXISTS "${_fbx_installer}")
    file(DOWNLOAD
      "https://damassets.autodesk.net/content/dam/autodesk/www/files/fbx202039_fbxsdk_vs2022_win.exe"
      "${_fbx_installer}"
      SHOW_PROGRESS
      STATUS _dl_status
    )
    list(GET _dl_status 0 _dl_rc)
    if(NOT _dl_rc EQUAL 0)
      list(GET _dl_status 1 _dl_msg)
      message(FATAL_ERROR "FBX SDK download failed: ${_dl_msg}")
    endif()
  endif()

  set(_fbx_extract "${_fbx_tmp}/extracted")
  if(NOT EXISTS "${_fbx_extract}")
    message(STATUS "Extracting FBX SDK ...")
    file(MAKE_DIRECTORY "${_fbx_extract}")
    execute_process(
      COMMAND "${_7z_exe}" x "${_fbx_installer}" -o${_fbx_extract} -aoa -bd
      RESULT_VARIABLE _7z_rc
      OUTPUT_QUIET
    )
    if(NOT _7z_rc EQUAL 0)
      message(FATAL_ERROR "7z extraction failed for FBX SDK installer (exit ${_7z_rc})")
    endif()
  endif()

  # Locate fbxsdk.h inside the extraction tree.
  file(GLOB_RECURSE _fbx_headers "${_fbx_extract}/*/fbxsdk.h")
  if(NOT _fbx_headers)
    # Also try flat layout (no intermediate directory).
    if(EXISTS "${_fbx_extract}/include/fbxsdk.h")
      set(_fbx_headers "${_fbx_extract}/include/fbxsdk.h")
    else()
      message(FATAL_ERROR "Could not find fbxsdk.h in extracted FBX SDK")
    endif()
  endif()
  list(GET _fbx_headers 0 _fbx_header_path)
  cmake_path(GET _fbx_header_path PARENT_PATH _fbx_inc_src)   # .../include
  cmake_path(GET _fbx_inc_src     PARENT_PATH _fbx_root)      # .../

  # Copy headers.
  file(MAKE_DIRECTORY "${_fbx_inc_dst}")
  file(COPY "${_fbx_inc_src}/fbxsdk.h" DESTINATION "${_fbx_inc_dst}")
  file(COPY "${_fbx_inc_src}/fbxsdk"   DESTINATION "${_fbx_inc_dst}")

  # Locate x64/release libs.
  file(GLOB_RECURSE _fbx_libs "${_fbx_root}/*/x64/release/libfbxsdk.lib")
  if(NOT _fbx_libs)
    message(FATAL_ERROR "Could not find libfbxsdk.lib under x64/release in extracted FBX SDK")
  endif()
  list(GET _fbx_libs 0 _fbx_lib_path)
  cmake_path(GET _fbx_lib_path PARENT_PATH _fbx_lib_src)

  file(MAKE_DIRECTORY "${_fbx_lib_dst}")
  file(COPY "${_fbx_lib_src}/libfbxsdk.lib" DESTINATION "${_fbx_lib_dst}")
  file(COPY "${_fbx_lib_src}/libfbxsdk.dll" DESTINATION "${_fbx_lib_dst}")
  message(STATUS "FBX SDK 2020.3.9 installed.")
else()
  message(STATUS "FBX SDK found at ${_fbx_sentinel}")
endif()

# ---------------------------------------------------------------------------
# 3. vcredist_x64.exe  (for the NSIS installer)
# ---------------------------------------------------------------------------
set(_vcredist_dst "${CMAKE_SOURCE_DIR}/bin_support/vcredist_x64.exe")
if(NOT EXISTS "${_vcredist_dst}")
  message(STATUS "Downloading vcredist_x64.exe ...")
  file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/bin_support")
  file(DOWNLOAD
    "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    "${_vcredist_dst}"
    SHOW_PROGRESS
    STATUS _vcr_status
  )
  list(GET _vcr_status 0 _vcr_rc)
  if(NOT _vcr_rc EQUAL 0)
    list(GET _vcr_status 1 _vcr_msg)
    message(WARNING "Failed to download vcredist_x64.exe: ${_vcr_msg}")
  endif()
else()
  message(STATUS "vcredist_x64.exe already present.")
endif()
