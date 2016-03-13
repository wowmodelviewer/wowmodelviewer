project(wowmodelviewer)
include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

include_directories(.)

# search for required package (wxwidgets)
set(wxWidgets_CONFIGURATION msw)
find_package(wxWidgets REQUIRED net gl aui xml adv core base)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS})

# find required Qt packages
find_package(Qt5Core)
find_package(Qt5Xml)
find_package(Qt5Gui)
find_package(Qt5Network)

# add wmv source files compilation
set(src  AnimationExportChoiceDialog.cpp
         animcontrol.cpp
         AnimExporter.cpp
         app.cpp
         AVIGenerator.cpp
         camera.cpp
         charcontrol.cpp
         CharDetailsFrame.cpp
         DisplaySettings.cpp
         effects.cpp
         ExportSettings.cpp
         filecontrol.cpp
         GeneralSettings.cpp
         imagecontrol.cpp
         ItemImporterDialog.cpp
         itemselection.cpp
         lightcontrol.cpp
         liquid.cpp
         LogStackWalker.cpp
         maptile.cpp
         modelbankcontrol.cpp
         modelcanvas.cpp
         modelcontrol.cpp
         modelviewer.cpp
         NPCImporterDialog.cpp
         Quantize.cpp
         SettingsControl.cpp
         shaders.cpp
         UserSkins.cpp
         util.cpp)

# add external code usage
use_glew()
use_core()
use_wow()
use_cximage()
use_casclib()

set(RES_FILES "wmv.rc")

#add_definitions(-D_BETAVERSION) # comment if you are building a released version
#add_definitions(-D_ALPHAVERSION) # comment if you are building a released version

add_executable(wowmodelviewer WIN32 ${src} ${RES_FILES} )

target_link_libraries(wowmodelviewer
  ${wxWidgets_LIBRARIES}
  wow
  cximage
  CascLib
)

install(TARGETS wowmodelviewer 
        RUNTIME DESTINATION $ENV{WMV_BASE_PATH}/bin)
  
# additional files needed to let WMV correctly works
set(QT_BIN_DIR  $ENV{WMV_SDK_BASEDIR}/Qt/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt5Core.dll 
           ${QT_BIN_DIR}/Qt5Gui.dll
           ${QT_BIN_DIR}/Qt5Network.dll
           ${QT_BIN_DIR}/Qt5Widgets.dll
           ${QT_BIN_DIR}/Qt5Xml.dll)
           
set(QT_PLUGIN_DIR $ENV{WMV_SDK_BASEDIR}/Qt/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll)
                   
set(QT_PLUGIN_IMAGE_FILES ${QT_PLUGIN_DIR}/imageformats/qjpeg.dll
                          ${QT_PLUGIN_DIR}/imageformats/qtga.dll)
                        
set(EXTRA_FILES $ENV{WMV_BASE_PATH}/bin_support/listfile.txt
        		$ENV{WMV_BASE_PATH}/bin_support/wow6.xml
        		$ENV{WMV_SDK_BASEDIR}/bin_support/vcredist_x86.exe
        		$ENV{WMV_SDK_BASEDIR}/lib/libeay32.dll
        		$ENV{WMV_SDK_BASEDIR}/lib/ssleay32.dll)


set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION $ENV{WMV_BASE_PATH}/bin)
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION $ENV{WMV_BASE_PATH}/bin/platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION $ENV{WMV_BASE_PATH}/bin/plugins/imageformats)