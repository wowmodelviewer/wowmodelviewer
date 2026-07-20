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
         ClientChoiceDialog.cpp
         ColorPickerDialog.cpp
         DisplaySettings.cpp
         effects.cpp
         ExportJobManager.cpp
         ExportProgressDialog.cpp
         ExportSettings.cpp
         filecontrol.cpp
         VoiceLinesDialog.cpp
         GeneralSettings.cpp
         imagecontrol.cpp
         ImageSequenceDialog.cpp
         ImageSequenceExporter.cpp
         MiniExr.cpp
         ItemImporterDialog.cpp
         itemselection.cpp
         lightcontrol.cpp
         LoadingDialog.cpp
         liquid.cpp
         LogStackWalker.cpp
         main.cpp
         maptile.cpp
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
            VoiceLinesDialog.h
            app.h
            AVIGenerator.h
            charcontrol.h
            CharDetailsCustomizationChoice.h
            CharDetailsFrame.h
            ClientChoiceDialog.h
            ColorPickerDialog.h
            DisplaySettings.h
            effects.h
            enums.h
            ExportJobManager.h
            ExportProgressDialog.h
            ExportSettings.h
            filecontrol.h
            GeneralSettings.h
            imagecontrol.h
            ImageSequenceDialog.h
            ImageSequenceExporter.h
            MiniExr.h
            ItemImporterDialog.h
            itemselection.h
            lightcontrol.h
            LoadingDialog.h
            liquid.h
            LogStackWalker.h
            maptile.h
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

set(wow10_1_files
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.1/database.xml
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.1/AnimationData.csv
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/10.1/ChrRacesCustomization.csv)

set(wow12_0_files
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/12.0/database.xml
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/12.0/AnimationData.csv
                 ${CMAKE_SOURCE_DIR}/bin_support/wow/12.0/ChrRacesCustomization.csv)

source_group("wow9.2" FILES ${wow9_2_files})
source_group("wow10.0" FILES ${wow10_0_files})
source_group("wow10.1" FILES ${wow10_1_files})
source_group("wow12.0" FILES ${wow12_0_files})

set(useful_files ${common_files} ${cmake_files} ${wow9_2_files} ${wow10_0_files} ${wow10_1_files} ${wow12_0_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(NAME wowmodelviewer)
add_executable(${NAME} WIN32 ${src} ${headers} ${RES_FILES} ${useful_files} )

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

# This is a 32-bit build; loading a modern WoW's ~2M-entry listfile plus the
# in-memory database easily exceeds the default 2 GB user address space. Allow
# the process to use up to 4 GB on 64-bit Windows.
if(MSVC)
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS "/LARGEADDRESSAWARE")
endif()

if(WMV_X64)
  # GLEW (vcpkg 2.x) errors if any <GL/gl.h> is seen before <GL/glew.h>; wx and Qt both pull gl.h
  # in, so force glew.h to the very top of every translation unit. WIN32_LEAN_AND_MEAN stops
  # <windows.h> (dragged in by wx) from including the legacy winsock.h, which otherwise collides
  # with the winsock2.h that Qt Network uses (fd_set/timeval redefinitions).
  target_compile_definitions(${NAME} PRIVATE WIN32_LEAN_AND_MEAN)
  target_compile_options(${NAME} PRIVATE "/FIGL/glew.h")
endif()

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
             ${QT_BIN_DIR}/Qt5Core.pdb
             ${QT_BIN_DIR}/Qt5Cored.dll
             ${QT_BIN_DIR}/Qt5Cored.pdb 
             ${QT_BIN_DIR}/Qt5Gui.dll
             ${QT_BIN_DIR}/Qt5Gui.pdb
             ${QT_BIN_DIR}/Qt5Guid.dll
             ${QT_BIN_DIR}/Qt5Guid.pdb
             ${QT_BIN_DIR}/Qt5Network.dll
             ${QT_BIN_DIR}/Qt5Network.pdb
             ${QT_BIN_DIR}/Qt5Networkd.dll
             ${QT_BIN_DIR}/Qt5Networkd.pdb
             ${QT_BIN_DIR}/Qt5Widgets.dll
             ${QT_BIN_DIR}/Qt5Widgets.pdb
             ${QT_BIN_DIR}/Qt5Widgetsd.dll
             ${QT_BIN_DIR}/Qt5Widgetsd.pdb
             ${QT_BIN_DIR}/Qt5Xml.dll
             ${QT_BIN_DIR}/Qt5Xml.pdb
             ${QT_BIN_DIR}/Qt5Xmld.dll
             ${QT_BIN_DIR}/Qt5Xmld.pdb)

set(QT_PLUGIN_DIR ${QT_LOCATION}/plugins)
set(QT_PLUGIN_SYS_FILES ${QT_PLUGIN_DIR}/platforms/qminimal.dll
                        ${QT_PLUGIN_DIR}/platforms/qminimal.pdb
                        ${QT_PLUGIN_DIR}/platforms/qminimald.dll
                        ${QT_PLUGIN_DIR}/platforms/qminimald.pdb
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreen.pdb
                        ${QT_PLUGIN_DIR}/platforms/qoffscreend.dll
                        ${QT_PLUGIN_DIR}/platforms/qoffscreend.pdb
                        ${QT_PLUGIN_DIR}/platforms/qwindows.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindows.pdb
                        ${QT_PLUGIN_DIR}/platforms/qwindowsd.dll
                        ${QT_PLUGIN_DIR}/platforms/qwindowsd.pdb)

set(QT_PLUGIN_IMAGE_FILES ${QT_PLUGIN_DIR}/imageformats/qjpeg.dll
                          ${QT_PLUGIN_DIR}/imageformats/qjpeg.pdb
                          ${QT_PLUGIN_DIR}/imageformats/qjpegd.dll
                          ${QT_PLUGIN_DIR}/imageformats/qjpegd.pdb
                          ${QT_PLUGIN_DIR}/imageformats/qtga.dll
                          ${QT_PLUGIN_DIR}/imageformats/qtga.pdb
                          ${QT_PLUGIN_DIR}/imageformats/qtgad.dll
                          ${QT_PLUGIN_DIR}/imageformats/qtgad.pdb)

set(EXTRA_FILES ${CMAKE_SOURCE_DIR}/bin_support/vcredist_x86.exe
                ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libssl-1_1.dll
                ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libcrypto-1_1.dll
                ${CMAKE_SOURCE_DIR}/bin_support/wow/listfile.csv
                ${CMAKE_SOURCE_DIR}/bin_support/Encryption/extraEncryptionKeys.csv)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${BIN_DIR})
install(FILES ${QT_PLUGIN_SYS_FILES} DESTINATION ${BIN_DIR}platforms)
install(FILES ${QT_PLUGIN_IMAGE_FILES} DESTINATION ${BIN_DIR}plugins/imageformats)
install(FILES ${wow9_2_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/9.2)
install(FILES ${wow10_0_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/10.0)
install(FILES ${wow10_1_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/10.1)
install(FILES ${wow12_0_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/12.0)
