 message(STATUS "Using Linux port")
  add_definitions(-D_LINUX)
  
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "64-bit Linux detected")
    add_definitions(-D_LINUX64)
  else ()
    message(STATUS "32-bit Linux detected")
    add_definitions(-D_LINUX32)
  endif ()
  
  find_package(GLEW REQUIRED)
  find_package(BZip2 REQUIRED)
  find_package(ZLIB REQUIRED)
  find_package(JPEG REQUIRED)
  find_package(PNG REQUIRED)
  set (EXTRA_LIBS ${EXTRA_LIBS} GLU)

  include_directories(${CMAKE_SOURCE_DIR}
    ${wxWidgets_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${PNG_PNG_INCLUDE_DIR})

  add_executable(wowmodelviewer ${WOWMV_SOURCES})
  add_dependencies(wowmodelviewer CxImage StormLib)   

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