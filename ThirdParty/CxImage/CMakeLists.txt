# CxImage lib

set(SRC_FILES
    tif_xfile.cpp
    ximabmp.cpp
    ximadsp.cpp
    ximaenc.cpp
    ximaexif.cpp
    ximage.cpp
    ximagif.cpp
    ximahist.cpp
    ximaico.cpp
    ximainfo.cpp
    ximaint.cpp
    ximajas.cpp
    ximajbg.cpp
    ximajpg.cpp
    ximalpha.cpp
    ximalyr.cpp
    ximamng.cpp
    ximapal.cpp
    ximapcx.cpp
    ximapng.cpp
    ximaraw.cpp
    ximasel.cpp
    ximaska.cpp
    ximatga.cpp
    ximath.cpp
    ximatif.cpp
    ximatran.cpp
    ximawbmp.cpp
    ximawmf.cpp
    ximawnd.cpp
    xmemfile.cpp
)

message(STATUS "Using CxImage")

if (APPLE)
  add_definitions(-D_MAC)
  add_definitions(-D_MAC_INTEL)
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL Linux)
  add_definitions(-D_LINUX)
  add_definitions(-D_LINUX32)
endif()

if (WIN32)
  if(WMV_X64)
    # x64: vcpkg-provided png/jpeg/zlib headers (version-matched to the libs below)
    include_directories(${WMV_VCPKG_X64}/include)
  endif()
  add_definitions(-D_WINDOWS)
  add_definitions(-D_WIN32)
endif()

add_library(cximage STATIC ${SRC_FILES})
set_property(TARGET cximage PROPERTY FOLDER "ThirdParty libraries")
# CxImage predates C++17 ('register').
set_target_properties(cximage PROPERTIES CXX_STANDARD 14)
# libpng and libjpeg: the system's or a package manager's (vcpkg on Windows), else the legacy prebuilt libraries.
find_package(PNG QUIET)
find_package(JPEG QUIET)
if(PNG_FOUND AND JPEG_FOUND)
  target_link_libraries(cximage PUBLIC PNG::PNG JPEG::JPEG)
elseif(NOT WIN32)
  message(FATAL_ERROR "CxImage needs libpng and libjpeg")
elseif(WMV_X64)
  target_link_libraries(cximage ${WMV_VCPKG_X64}/lib/jpeg.lib ${WMV_VCPKG_X64}/lib/libpng16.lib)
else()
  target_include_directories(cximage PUBLIC ${CMAKE_SOURCE_DIR}/ThirdParty/include)
  target_link_libraries(cximage ${CMAKE_SOURCE_DIR}/ThirdParty/lib/jpeg.lib
    ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libpng.lib)
endif()
