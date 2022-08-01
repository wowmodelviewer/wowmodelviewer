cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(wowmodelviewer)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

include_directories(.)

# search for required package (wxwidgets)
set(wxWidgets_USE_UNICODE ON)
set(wxWidgets_ROOT_DIR ${CMAKE_SOURCE_DIR}/wxWidgets)
find_package(wxWidgets REQUIRED net aui xml adv core base)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)

# find required Qt packages
find_package(Qt5 COMPONENTS Core Network Widgets REQUIRED)
find_package(Qt5 COMPONENTS Xml Gui)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# add wmv source files compilation
set(src  AnimationExportChoiceDialog.cpp
         animcontrol.cpp
         AnimExporter.cpp
         app.cpp
         AVIGenerator.cpp
         charcontrol.cpp
         CharDetailsCustomizationChoice.cpp
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
         OrbitCamera.cpp
         Quantize.cpp
         SettingsControl.cpp
         shaders.cpp
         UserSkins.cpp
         util.cpp)

set(headers AnimationExportChoiceDialog.h
            animcontrol.h
            AnimExporter.h
            app.h
            AVIGenerator.h
            charcontrol.h
            CharDetailsCustomizationChoice.h
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
            OrbitCamera.h
            Quantize.h
            resource1.h
            SettingsControl.h
            shaders.h
            UserSkins.h
            util.h)

source_group("Header Files" FILES ${headers})

# add external code usage
use_glew()
use_core()
use_wow()
use_cximage()
use_casclib()

set(RES_FILES "wmv.rc")

# extra useful files for visual studio project
set(cmake_files ${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake
                ${CMAKE_CURRENT_LIST_DIR}/../cmake/linux.cmake
                ${CMAKE_CURRENT_LIST_DIR}/../cmake/macos.cmake
                ${CMAKE_CURRENT_LIST_DIR}/../cmake/policies.cmake
                ${CMAKE_CURRENT_LIST_DIR}/../cmake/windows.cmake)

source_group("cmake" FILES ${cmake_files})

set(common_files wowmodelviewer.json
                 ${WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi
                 ${WMV_BASE_PATH}/appveyor.yml
                 ${WMV_BASE_PATH}/bin_support/Encryption/extraEncryptionKeys.csv)

set(wow9_0_files 
                 ${WMV_BASE_PATH}/bin_support/wow/9.0/listfile.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.0/database.xml
                 ${WMV_BASE_PATH}/bin_support/wow/9.0/AnimationData.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.0/ChrRacesCustomization.csv)

set(wow9_1_files 
                 ${WMV_BASE_PATH}/bin_support/wow/9.1/listfile.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.1/database.xml
                 ${WMV_BASE_PATH}/bin_support/wow/9.1/AnimationData.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.1/ChrRacesCustomization.csv)

set(wow9_2_files 
                 ${WMV_BASE_PATH}/bin_support/wow/9.2/listfile.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.2/database.xml
                 ${WMV_BASE_PATH}/bin_support/wow/9.2/AnimationData.csv
                 ${WMV_BASE_PATH}/bin_support/wow/9.2/ChrRacesCustomization.csv)

source_group("wow9.0" FILES ${wow9_0_files})
source_group("wow9.1" FILES ${wow9_1_files})
source_group("wow9.2" FILES ${wow9_2_files})

set(useful_files ${common_files} ${cmake_files} ${wow9_0_files} ${wow9_1_files} ${wow9_2_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(NAME wowmodelviewer)
add_executable(${NAME} WIN32 ${src} ${headers} ${RES_FILES} ${useful_files} )

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

set(BIN_DIR "${WMV_BASE_PATH}/bin/")

if (MSVC_IDE)
  # Enable Qt in Visual Studio
  set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
  set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:_CONSOLE>)
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

target_link_libraries(${NAME}
  ${extralibs}
  Qt5::Xml
  Qt5::Gui
  Qt5::Network
  Qt5::Widgets
  ${wxWidgets_LIBRARIES}
)

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
  
# additional files needed to let WMV correctly works
set(QT_BIN_DIR  ${CMAKE_SOURCE_DIR}/Dependencies/Qt/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt5Core.dll 
             ${QT_BIN_DIR}/Qt5Gui.dll
             ${QT_BIN_DIR}/Qt5Network.dll
             ${QT_BIN_DIR}/Qt5Widgets.dll
             ${QT_BIN_DIR}/Qt5Xml.dll)

set(QT_PLUGIN_DIR ${CMAKE_SOURCE_DIR}/Dependencies/Qt/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll)

set(QT_PLUGIN_IMAGE_FILES ${QT_PLUGIN_DIR}/imageformats/qjpeg.dll
                          ${QT_PLUGIN_DIR}/imageformats/qtga.dll)

set(EXTRA_FILES ${CMAKE_SOURCE_DIR}/bin_support/vcredist_x86.exe
                ${CMAKE_SOURCE_DIR}/lib/libeay32.dll
                ${CMAKE_SOURCE_DIR}/lib/ssleay32.dll
                ${WMV_BASE_PATH}/bin_support/Encryption/extraEncryptionKeys.csv)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${BIN_DIR})
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION ${BIN_DIR}platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION ${BIN_DIR}plugins/imageformats)
install(FILES ${wow9_0_files} DESTINATION ${WMV_BASE_PATH}/bin/games/wow/9.0)
install(FILES ${wow9_1_files} DESTINATION ${WMV_BASE_PATH}/bin/games/wow/9.1)
install(FILES ${wow9_2_files} DESTINATION ${WMV_BASE_PATH}/bin/games/wow/9.2)
