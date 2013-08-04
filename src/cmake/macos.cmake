message(STATUS "Using Mac OS X port")
  add_definitions(-D_MAC)
  add_definitions(-D_MAC_INTEL)
  find_package(GLEW REQUIRED)
  find_package(BZip2 REQUIRED)
  find_package(ZLIB REQUIRED)
  find_package(JPEG REQUIRED)
  find_package(PNG REQUIRED)

  set(CMAKE_OSX_ARCHITECTURES i386)
  set(MACOSX_BUNDLE_ICON_FILE wmv.icns)
  set(MACOSX_BUNDLE_INFO_STRING "World of Warcraft ModelViewer")
  
  set_source_files_properties("${MACOSX_BUNDLE_ICON_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS> ;<CMAKE_RANLIB> -c <TARGET> ")
  set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS> ;<CMAKE_RANLIB> -c <TARGET> ")

  include_directories(${CMAKE_SOURCE_DIR}
    ${wxWidgets_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${PNG_PNG_INCLUDE_DIR})

  target_link_libraries(wowmodelviewer
    ${EXTRA_LIBS}
    cximage
    ${wxWidgets_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    )

  add_executable(wowmodelviewer MACOSX_BUNDLE ${WOWMV_SOURCES} ../bin_support/Icons/${MACOSX_BUNDLE_ICON_FILE})
  add_dependencies(wowmodelviewer CxImage StormLib) 