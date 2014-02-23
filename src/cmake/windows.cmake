 #put here full path to your makensis cmd
  set(MAKENSISCMD "C:\\Program Files\\NSIS\\makensis.exe")

  set(RES_FILES "wmv_mingw.rc")
  set(CMAKE_RC_COMPILER_INIT windres)
  ENABLE_LANGUAGE(RC)
  SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")

  message(STATUS "Using Windows MinGW version")
  set(JPEG_INCLUDE_DIR c:/MinGW/include)

  message(STATUS "Using glew")
  include(glew/glew.cmake)
  add_definitions(-DGLEW_STATIC)
  set(WOWMV_SOURCES ${WOWMV_SOURCES} ${GLEW_SRC})
  message("glew src : ${GLEW_SRC}")
  message("glew include : ${GLEW_INCLUDE_DIR}")

  include_directories(${CMAKE_SOURCE_DIR}
                      ${wxWidgets_INCLUDE_DIRS}
                      ${GLEW_INCLUDE_DIR}
                     )
  
  add_definitions(-D_WINDOWS)
  add_definitions(-D_MINGW)
  add_definitions(-D_BETAVERSION) # comment if you are building a released version
  if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
    message("Release build : Final exe will be stripped")
    set(CMAKE_EXE_LINKER_FLAGS "-s") # strip exe
  else()
    message("${CMAKE_BUILD_TYPE} build : Final exe will NOT be stripped")
  endif()
  
  if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
    add_executable(wowmodelviewer WIN32 ${WOWMV_SOURCES} ${RES_FILES} )
  else()
    # non min size release case ( = dev) => let a console attached to app
    add_executable(wowmodelviewer ${WOWMV_SOURCES} ${RES_FILES} )
  endif()
  
  add_dependencies(wowmodelviewer CxImage StormLib nextgen)   
  
  target_link_libraries(wowmodelviewer
    cximage
    ${wxWidgets_LIBRARIES}
    ${EXTRA_LIBS}
    jpeg
    png
    core
   )

  add_custom_target(release
                    COMMAND ${MAKENSISCMD} "../Installers/Windows/NSIS/WMVInstallerMUI.nsi"
                    DEPENDS wowmodelviewer)             


  install(TARGETS wowmodelviewer 
          RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
        
  file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/MinGW/*.dll")
  file(GLOB platform_files "${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/MinGW/platforms/*.dll")
  install(FILES ${files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
  install(FILES ${platform_files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin/platforms)