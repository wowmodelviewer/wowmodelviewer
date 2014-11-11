string (REPLACE "\\" "/" WMV_SDK_BASEDIR $ENV{WMV_SDK_BASEDIR})

#put here full path to your makensis cmd
set(MAKENSISCMD ${WMV_SDK_BASEDIR}/NSIS/makensis.exe)

set(RES_FILES "wmv_mingw.rc")
set(CMAKE_RC_COMPILER_INIT windres)
ENABLE_LANGUAGE(RC)
SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")

message(STATUS "Using Windows MinGW version")

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
#add_definitions(-D_BETAVERSION) # comment if you are building a released version
add_definitions(-D_ALPHAVERSION) # comment if you are building a released version
if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  message("Release build : Final exe will be stripped")
  set(CMAKE_EXE_LINKER_FLAGS "-s") # strip exe
else()
  message("${CMAKE_BUILD_TYPE} build : Final exe will NOT be stripped")
endif()
  
if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  add_executable(wowmodelviewer ${WOWMV_SOURCES} ${RES_FILES} )
else()
  # non min size release case ( = dev) => let a console attached to app
  add_executable(wowmodelviewer ${WOWMV_SOURCES} ${RES_FILES} )
endif()

find_package(Qt5Network)

add_dependencies(wowmodelviewer cximage casc_static core)   
  
target_link_libraries(wowmodelviewer
  cximage
  ${wxWidgets_LIBRARIES}
  ${EXTRA_LIBS}
  jpeg
  png
  core
  Qt5::Xml
)

add_custom_target(release make install
                  COMMAND ${MAKENSISCMD} "../Installers/Windows/NSIS/WMVInstallerMUI.nsi")             


install(TARGETS wowmodelviewer 
        RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
  
# additional files needed to let WMV correctly works
set(QT_MINGW_DIR ${WMV_SDK_BASEDIR}/Qt/5.3/mingw482_32)
set(QT_BIN_DIR ${QT_MINGW_DIR}/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt5Core.dll 
  		     ${QT_BIN_DIR}/Qt5Gui.dll
  		     ${QT_BIN_DIR}/Qt5Network.dll
  		     ${QT_BIN_DIR}/Qt5Widgets.dll
  		     ${QT_BIN_DIR}/Qt5Xml.dll)
  			 
set(QT_SYS_FILES ${QT_BIN_DIR}/icudt52.dll
  			     ${QT_BIN_DIR}/icuin52.dll
  			     ${QT_BIN_DIR}/icuuc52.dll
  			     ${QT_BIN_DIR}/libgcc_s_dw2-1.dll
  			     ${QT_BIN_DIR}/libstdc++-6.dll
  			     ${QT_BIN_DIR}/libwinpthread-1.dll)
  				 
set(QT_PLUGIN_DIR ${QT_MINGW_DIR}/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll)

set(EXTRA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/listfile.txt
				${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/wow6.xml)

set(MINGW_BIN_DIR ${WMV_SDK_BASEDIR}/MinGW/bin)
set(MINGW_SYS_FILES ${MINGW_BIN_DIR}/jpeg62.dll)

set(files ${QT_FILES} ${QT_SYS_FILES} ${MINGW_SYS_FILES} ${EXTRA_FILES})
set(platform_files ${QT_PLUGIN_SYS_FILES})			 
  				 
install(FILES ${files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
install(FILES ${platform_files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin/platforms)