# wowmodelviewer exe
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)

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
set(cmake_files ${CMAKE_SOURCE_DIR}/cmake/common.cmake
                ${CMAKE_SOURCE_DIR}/cmake/linux.cmake
                ${CMAKE_SOURCE_DIR}/cmake/macos.cmake
                ${CMAKE_SOURCE_DIR}/cmake/policies.cmake
                ${CMAKE_SOURCE_DIR}/cmake/windows.cmake)

source_group("cmake" FILES ${cmake_files})

set(common_files 
                 wowmodelviewer.json
                 ${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi
                 ${CMAKE_SOURCE_DIR}/appveyor.yml
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/listfile.csv
                 ${CMAKE_SOURCE_DIR}/bin_support/Encryption/extraEncryptionKeys.csv)

set(wow9_2_files 
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/9.2/database.xml
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/9.2/AnimationData.csv
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/9.2/ChrRacesCustomization.csv)

set(wow10_0_files
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.0/database.xml
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.0/AnimationData.csv
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.0/ChrRacesCustomization.csv)

source_group("wow9.2" FILES ${wow9_2_files})
source_group("wow10.0" FILES ${wow10_0_files})

set(useful_files ${common_files} ${cmake_files} ${wow9_2_files} ${wow10_0_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(NAME wowmodelviewer)
add_executable(${NAME} WIN32 ${src} ${headers} ${RES_FILES} ${useful_files} )

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

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
set(QT_BIN_DIR  ${QT_LOCATION}/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt5Core.dll 
             ${QT_BIN_DIR}/Qt5Gui.dll
             ${QT_BIN_DIR}/Qt5Network.dll
             ${QT_BIN_DIR}/Qt5Widgets.dll
             ${QT_BIN_DIR}/Qt5Xml.dll)

set(QT_PLUGIN_DIR ${QT_LOCATION}/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll)

set(QT_PLUGIN_IMAGE_FILES ${QT_PLUGIN_DIR}/imageformats/qjpeg.dll
                          ${QT_PLUGIN_DIR}/imageformats/qtga.dll)

set(EXTRA_FILES ${CMAKE_SOURCE_DIR}/bin_support/vcredist_x86.exe
                ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libeay32.dll
                ${CMAKE_SOURCE_DIR}/ThirdParty/lib/ssleay32.dll
                ${CMAKE_SOURCE_DIR}/bin_support/wow/listfile.csv
                ${CMAKE_SOURCE_DIR}/bin_support/Encryption/extraEncryptionKeys.csv)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${BIN_DIR})
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION ${BIN_DIR}platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION ${BIN_DIR}plugins/imageformats)
install(FILES ${wow9_2_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/9.2)
install(FILES ${wow10_0_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/10.0)
