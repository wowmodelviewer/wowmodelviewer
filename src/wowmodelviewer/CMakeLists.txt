project(wowmodelviewer)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

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
		 ArcBallCamera.cpp
		 ArcBallCameraControl.cpp
         AVIGenerator.cpp
		 camera.cpp
         charcontrol.cpp
		 CharDetailsCustomizationSpin.cpp
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

set(headers AnimationExportChoiceDialog.h
			animcontrol.h
			AnimExporter.h
			app.h
			ArcBallCamera.h
			ArcBallCameraControl.h
			AVIGenerator.h
			camera.h
			charcontrol.h
			CharDetailsCustomizationSpin.h
			CharDetailsFrame.h
			DisplaySettings.h
			effects.h
			enums.h
			ExportSettings.h
			filecontrol.h
			GeneralSettings.h
			imagecontrol.h
			ItemImporterDialog.h
			itemselection.h
			lightcontrol.h
			liquid.h
			LogStackWalker.h
			maptile.h
			modelbankcontrol.h
			modelcanvas.h
			modelcontrol.h
			modelviewer.h
			NPCImporterDialog.h
			Quantize.h
			resource1.h
			SettingsControl.h
			shaders.h
			UserSkins.h
			util.h)

source_group("Header Files" FILES ${headers})

# add external code usage
use_glew()
# reset extralibs to clean up wxwidgets (use_glew actually links with wx gl lib)
set(extralibs "")
use_core()
use_wow()
use_cximage()
use_casclib()
use_wxwidgets()

set(RES_FILES "wmv.rc")

# extra useful files for visual studio project
set(common_files wowmodelviewer.json
				 ${WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi
				 ${WMV_BASE_PATH}/appveyor.yml)

set(wow7_2_files ${WMV_BASE_PATH}/bin_support/wow/7.2/listfile.txt
				 ${WMV_BASE_PATH}/bin_support/wow/7.2/database.xml)
source_group("wow7.2" FILES ${wow7_2_files})

set(wow7_3_files ${WMV_BASE_PATH}/bin_support/wow/7.3/listfile.txt
				 ${WMV_BASE_PATH}/bin_support/wow/7.3/database.xml
				 ${WMV_BASE_PATH}/bin_support/wow/7.3/AnimationData.csv)

source_group("wow7.3" FILES ${wow7_3_files})

set(useful_files ${common_files} ${wow7_2_files} ${wow7_3_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)


add_executable(wowmodelviewer WIN32 ${src} ${headers} ${RES_FILES} ${useful_files})

set_property(TARGET wowmodelviewer PROPERTY FOLDER "executables")

target_link_libraries(wowmodelviewer
  ${wxWidgets_LIBRARIES}
  wow
  cximage
)

install(TARGETS wowmodelviewer 
        RUNTIME DESTINATION ${WMV_BASE_PATH}/bin)
  
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
                        
set(EXTRA_FILES $ENV{WMV_SDK_BASEDIR}/bin_support/vcredist_x86.exe
        		$ENV{WMV_SDK_BASEDIR}/lib/libeay32.dll
        		$ENV{WMV_SDK_BASEDIR}/lib/ssleay32.dll)

set(GAMEFILES_WOW7_2 ${WMV_BASE_PATH}/bin_support/wow/7.2/listfile.txt
        		     ${WMV_BASE_PATH}/bin_support/wow/7.2/database.xml)

set(GAMEFILES_WOW7_3 ${WMV_BASE_PATH}/bin_support/wow/7.3/listfile.txt
        		     ${WMV_BASE_PATH}/bin_support/wow/7.3/database.xml
					 ${WMV_BASE_PATH}/bin_support/wow/7.3/AnimationData.csv)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${WMV_BASE_PATH}/bin)
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION ${WMV_BASE_PATH}/bin/platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION ${WMV_BASE_PATH}/bin/plugins/imageformats)

install(FILES ${GAMEFILES_WOW7_2} DESTINATION ${WMV_BASE_PATH}/bin/games/wow/7.2)
install(FILES ${GAMEFILES_WOW7_3} DESTINATION ${WMV_BASE_PATH}/bin/games/wow/7.3)
