string (REPLACE "\\" "/" WMV_SDK_BASEDIR $ENV{WMV_SDK_BASEDIR})

#put here full path to your makensis cmd
set(MAKENSISCMD ${WMV_SDK_BASEDIR}/NSIS/makensis.exe)

set(RES_FILES "wmv_mingw.rc")
#set(CMAKE_RC_COMPILER_INIT windres)
#ENABLE_LANGUAGE(RC)
#SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")

message(STATUS "Using Windows version")

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



include_directories(next-gen/games/wow)

add_definitions(-D_WINDOWS)
add_definitions(-D_MINGW)
add_definitions(-D_BETAVERSION) # comment if you are building a released version
#add_definitions(-D_ALPHAVERSION) # comment if you are building a released version
if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  message("Release build : Final exe will be stripped")
  set(CMAKE_EXE_LINKER_FLAGS "-s") # strip exe
else()
  message("${CMAKE_BUILD_TYPE} build : Final exe will NOT be stripped")
endif()
  
# disable some visual studio annoying warnings
# warning on stl class dll exporting
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
# precompiler secure warnings (too much as WMV code is old)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  
if(NOT ${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  add_definitions(-DKEEP_CONSOLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /INCREMENTAL:NO")
endif()
  
 add_executable(wowmodelviewer WIN32 ${WOWMV_SOURCES} ${RES_FILES} )

find_package(Qt5Network)

target_link_libraries(wowmodelviewer
  ${wxWidgets_LIBRARIES}
  wow
  cximage
  CascLib
)

add_custom_target(release nmake install
                  COMMAND ${MAKENSISCMD} "../Installers/Windows/NSIS/WMVInstallerMUI.nsi")             


install(TARGETS wowmodelviewer 
        RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
  
# additional files needed to let WMV correctly works
#set(QT_BIN_DIR D:/Programmes/QT_VS/5.3/msvc2013_opengl/bin)
set(QT_BIN_DIR  D:/Programmes/QT_VS_5.4.2/5.4/msvc2013_opengl/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt5Core.dll 
  		     ${QT_BIN_DIR}/Qt5Gui.dll
  		     ${QT_BIN_DIR}/Qt5Network.dll
  		     ${QT_BIN_DIR}/Qt5Widgets.dll
  		     ${QT_BIN_DIR}/Qt5Xml.dll)
  			 
set(QT_SYS_FILES ${QT_BIN_DIR}/icudt53.dll
  			     ${QT_BIN_DIR}/icuin53.dll
  			     ${QT_BIN_DIR}/icuuc53.dll
  			     D:/sdk_new/bin_support/msvcp120.dll
  			     D:/sdk_new/bin_support/msvcr120.dll)
  				 
set(QT_PLUGIN_DIR D:/Programmes/QT_VS_5.4.2/5.4/msvc2013_opengl/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll)

set(EXTRA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/listfile.txt
				${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/wow6.xml)


set(files ${QT_FILES} ${QT_SYS_FILES} ${EXTRA_FILES})
set(platform_files ${QT_PLUGIN_SYS_FILES})			 
  				 
install(FILES ${files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
install(FILES ${platform_files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin/platforms)
