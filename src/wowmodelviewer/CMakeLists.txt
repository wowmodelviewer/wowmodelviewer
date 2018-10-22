cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)

set(PROJECTNAME wowmodelviewer)

project(${PROJECTNAME})

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

include_directories(.)

# search for required package (wxwidgets)
set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets REQUIRED net gl aui xml adv core base)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS})

# find required Qt packages
find_package(Qt5 COMPONENTS Core Network Widgets REQUIRED)
find_package(Qt5 COMPONENTS Xml Gui)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(AUTORCC ON)

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
		 main.cpp
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

set (windowFiles WoWModelViewer.h
				WoWModelViewer.cpp
				dockWidget_Animation.h
				dockWidget_Animation.cpp
				dockWidget_CharacterDesigner.h
				dockWidget_CharacterDesigner.cpp
				dockWidget_Equipment.h
				dockWidget_Equipment.cpp
				dockWidget_FileList.h
				dockWidget_FileList.cpp
				dockWidget_Lighting.h
				dockWidget_Lighting.cpp
				dockWidget_ModelBank.h
				dockWidget_ModelBank.cpp
				dockWidget_ModelControls.h
				dockWidget_ModelControls.cpp)
				
source_group("Windows" FILES ${windowFiles})

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

set(wow7_3_files ${WMV_BASE_PATH}/bin_support/wow/7.3/listfile.txt
				 ${WMV_BASE_PATH}/bin_support/wow/7.3/database.xml
				 ${WMV_BASE_PATH}/bin_support/wow/7.3/AnimationData.csv
				 ${WMV_BASE_PATH}/bin_support/wow/7.3/ChrRacesCustomization.csv)

source_group("wowData\\wow7.3" FILES ${wow7_3_files})

set(wow8_0_files ${WMV_BASE_PATH}/bin_support/wow/8.0/listfile.txt
				 ${WMV_BASE_PATH}/bin_support/wow/8.0/database.xml
				 ${WMV_BASE_PATH}/bin_support/wow/8.0/AnimationData.csv
				 ${WMV_BASE_PATH}/bin_support/wow/8.0/ChrRacesCustomization.csv)

source_group("wowData\\wow8.0" FILES ${wow8_0_files})

set(wow8_1_files ${WMV_BASE_PATH}/bin_support/wow/8.1/listfile.txt
				 ${WMV_BASE_PATH}/bin_support/wow/8.1/database.xml
				 ${WMV_BASE_PATH}/bin_support/wow/8.1/AnimationData.csv
				 ${WMV_BASE_PATH}/bin_support/wow/8.1/ChrRacesCustomization.csv)

source_group("wowData\\wow8.1" FILES ${wow8_1_files})

set(TRANSLATION_PATH ${WMV_BASE_PATH}/src/Translations/)

# Translation files for the various languages
set(translationfiles ${TRANSLATION_PATH}wowmodelviewer_en_US.ts
					 ${TRANSLATION_PATH}wowmodelviewer_fr_FR.ts
					 ${TRANSLATION_PATH}wowmodelviewer_de_DE.ts
					 ${TRANSLATION_PATH}wowmodelviewer_zh_CN.ts
					 ${TRANSLATION_PATH}wowmodelviewer_zh_TW.ts)

source_group("Translation Files" FILES ${translationfiles})

set(uifiles ../Forms/wmvMainWindow.ui
			../Forms/dockWidget_Animation.ui
			../Forms/dockWidget_CharacterDesigner.ui
			../Forms/dockWidget_Equipment.ui
			../Forms/dockWidget_FileList.ui
			../Forms/dockWidget_Lighting.ui
			../Forms/dockWidget_ModelBank.ui
			../Forms/dockWidget_ModelControls.ui)
QT5_WRAP_UI(UI_HEADERS ${uifiles})

source_group("Forms" FILES ${uifiles})

set (MOC_HEADERS WoWModelViewer.h
				dockWidget_Animation.h
				dockWidget_CharacterDesigner.h
				dockWidget_Equipment.h
				dockWidget_FileList.h
				dockWidget_Lighting.h
				dockWidget_ModelBank.h
				dockWidget_ModelControls.h)
QT5_WRAP_CPP(MOC_SRCS ${MOC_HEADERS})

set (resources ../../wmv.qrc
				wmv.rc)
source_group("Resources" FILES ${resources} )

QT5_ADD_RESOURCES(QTRESOURCES ../../wmv.qrc)

find_package(Qt5LinguistTools REQUIRED)
set(LUPDATE_OPTIONS "-ts ${translationfiles}" CACHE STRING "specifies options passed to lupdate")
# adds the translations and a target for it
qt5_create_translation(QM_FILES
	${windowFiles} ${uifiles}
	OPTIONS ${LUPDATE_OPTIONS}
)

source_group("GeneratedFiles" FILES ${UI_HEADERS} ${MOC_SRCS} ${QTRESOURCES})

set(useful_files ${common_files} ${wow7_3_files} ${wow8_0_files} ${wow8_1_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(${PROJECTNAME} WIN32 ${src} ${headers} ${RES_FILES} ${translationfiles} ${windowFiles} ${uifiles} ${useful_files} ${resources} ${UI_HEADERS} ${MOC_SRCS} ${QTRESOURCES})

set_property(TARGET ${PROJECTNAME} PROPERTY FOLDER "executables")

set(BIN_DIR "${WMV_BASE_PATH}/bin/")

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${PROJECTNAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set_target_properties(${PROJECTNAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${PROJECTNAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:_CONSOLE>)
	set_target_properties(${PROJECTNAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
	set_target_properties(${PROJECTNAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

	set_target_properties(${PROJECTNAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
	set_target_properties(${PROJECTNAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
	set_target_properties(${PROJECTNAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
	set_target_properties(${PROJECTNAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

file(MAKE_DIRECTORY ${WoWModelViewer_BINARY_DIR}/GeneratedFiles)
set_property(TARGET ${PROJECTNAME} PROPERTY AUTOGEN_TARGETS_FOLDER ${WoWModelViewer_BINARY_DIR}/GeneratedFiles/)
include_directories(${WoWModelViewer_BINARY_DIR}/GeneratedFiles)

target_link_libraries(${PROJECTNAME}
  ${wxWidgets_LIBRARIES}
  wow
  cximage
  Qt5::Xml
  Qt5::Gui
  Qt5::Network
  Qt5::Widgets
)

install(TARGETS ${PROJECTNAME} RUNTIME DESTINATION ${BIN_DIR})

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
                        
set(EXTRA_FILES $ENV{WMV_SDK_BASEDIR}/bin_support/vc_redist.x64.exe
        		$ENV{WMV_SDK_BASEDIR}/lib/libeay32.dll
        		$ENV{WMV_SDK_BASEDIR}/lib/ssleay32.dll)

set(GAMEFILES_WOW7_3 ${WMV_BASE_PATH}/bin_support/wow/7.3/listfile.txt
        		     ${WMV_BASE_PATH}/bin_support/wow/7.3/database.xml
					 ${WMV_BASE_PATH}/bin_support/wow/7.3/AnimationData.csv
					 ${WMV_BASE_PATH}/bin_support/wow/7.3/ChrRacesCustomization.csv)

set(GAMEFILES_WOW8_0 ${WMV_BASE_PATH}/bin_support/wow/8.0/listfile.txt
        		     ${WMV_BASE_PATH}/bin_support/wow/8.0/database.xml
					 ${WMV_BASE_PATH}/bin_support/wow/8.0/AnimationData.csv
					 ${WMV_BASE_PATH}/bin_support/wow/8.0/ChrRacesCustomization.csv)

set(GAMEFILES_WOW8_1 ${WMV_BASE_PATH}/bin_support/wow/8.1/listfile.txt
        		     ${WMV_BASE_PATH}/bin_support/wow/8.1/database.xml
					 ${WMV_BASE_PATH}/bin_support/wow/8.1/AnimationData.csv
					 ${WMV_BASE_PATH}/bin_support/wow/8.1/ChrRacesCustomization.csv)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${BIN_DIR})
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION ${BIN_DIR}platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION ${BIN_DIR}plugins/imageformats)

install(FILES ${GAMEFILES_WOW7_3} DESTINATION ${BIN_DIR}games/wow/7.3)
install(FILES ${GAMEFILES_WOW8_0} DESTINATION ${BIN_DIR}games/wow/8.0)
install(FILES ${GAMEFILES_WOW8_1} DESTINATION ${BIN_DIR}games/wow/8.1)
