# wowmodelviewer exe
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# add wmv source files compilation
set(src  AnimationExportChoiceDialog.cpp
         AppPaths.cpp
         ArmoryImportDialog.cpp
         BlpInfo.cpp
         animcontrol.cpp
         AnimExporter.cpp
         app.cpp
         AVIGenerator.cpp
         charcontrol.cpp
         CharDetailsCustomizationChoice.cpp
         CharDetailsFrame.cpp
         ClientChoiceDialog.cpp
         ClientInstallations.cpp
         ColorPickerDialog.cpp
         CustomizationGeosetTest.cpp
         DisplaySettings.cpp
         effects.cpp
         ExportJobManager.cpp
         ExportProgressDialog.cpp
         ExportSettings.cpp
         filecontrol.cpp
         GeneralSettings.cpp
         imagecontrol.cpp
         ImageSequenceDialog.cpp
         ImageSequenceExporter.cpp
         MiniExr.cpp
         ItemImporterDialog.cpp
         ItemSetCatalog.cpp
         ItemSetChoiceDialog.cpp
         itemselection.cpp
         KeyboardShortcutsDialog.cpp
         lightcontrol.cpp
         ClientLoadingPanel.cpp
         LogStackWalker.cpp
         main.cpp
         modelcanvas.cpp
         modelcontrol.cpp
         ModelIdLookup.cpp
         ModelInspector.cpp
         modelviewer.cpp
         MountCard.cpp
         NPCImporterDialog.cpp
         OrbitCamera.cpp
         Quantize.cpp
         SettingsControl.cpp
         TextureBrowse.cpp
         TextureCatalog.cpp
         TextureDecode.cpp
         TextureExport.cpp
         TextureExportNotice.cpp
         TexturePreview.cpp
         TextureView.cpp
         UiArt.cpp
         UiControls.cpp
         UiIcons.cpp
         UiStyle.cpp
         ClientState.cpp
         UserSkins.cpp
         util.cpp
         BackgroundColorDialog.cpp
         ViewportBackground.cpp
         ViewportHost.cpp
         WinTheme.cpp)

set(headers AnimationExportChoiceDialog.h
            AppPaths.h
            ArmoryImportDialog.h
            BlpInfo.h
            animcontrol.h
            AnimExporter.h
            app.h
            AVIGenerator.h
            charcontrol.h
            CharDetailsCustomizationChoice.h
            CharDetailsFrame.h
            ClientChoiceDialog.h
            ClientInstallations.h
            ColorPickerDialog.h
            CustomizationGeosetTest.h
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
            ItemSetCatalog.h
            ItemSetChoiceDialog.h
            ItemSetCategory.h
            itemselection.h
            KeyboardShortcutsDialog.h
            lightcontrol.h
            ClientLoadingPanel.h
            ClientLoadProgress.h
            LogStackWalker.h
            modelcanvas.h
            modelcontrol.h
            ModelIdLookup.h
            ModelInspector.h
            modelviewer.h
            MountCard.h
            NPCImporterDialog.h
            OrbitCamera.h
            Quantize.h
            resource1.h
            SettingsControl.h
            TextureBrowse.h
            TextureCatalog.h
            TextureDecode.h
            TextureExport.h
            TextureExportNotice.h
            TexturePreview.h
            TextureView.h
            UiArt.h
            UiControls.h
            UiIcons.h
            UiStyle.h
            ClientState.h
            UserSkins.h
            util.h
            BackgroundColorDialog.h
            ViewerMode.h
            ViewportBackground.h
            WinTheme.h)

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
# The community listfile is not in git (the viewer downloads it), so list only what is there.
list(FILTER useful_files EXCLUDE REGEX "listfile\\.csv$")

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(NAME wowmodelviewer)

# Windows-only parts: the AVI writer (Video for Windows) and the Windows theme probe.
if(NOT WIN32)
  list(REMOVE_ITEM src AVIGenerator.cpp WinTheme.cpp)
  list(REMOVE_ITEM headers AVIGenerator.h WinTheme.h)
  set(RES_FILES)
endif()

add_executable(${NAME} WIN32 MACOSX_BUNDLE ${src} ${headers} ${RES_FILES} ${useful_files} )

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

if(MSVC)
  # WIN32_LEAN_AND_MEAN stops <windows.h> (dragged in by wx) from including the legacy winsock.h, which otherwise
  # collides with the winsock2.h that Qt Network uses (fd_set/timeval redefinitions).
  target_compile_definitions(${NAME} PRIVATE WIN32_LEAN_AND_MEAN)
endif()

if(APPLE)
  # wxWidgets and Qt both forward-declare Cocoa classes (NSData, NSString, ...); in C++ their declarations collide,
  # in Objective-C++ both are @class. So the viewer's sources compile as Objective-C++ here.
  target_compile_options(${NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-x objective-c++ -fobjc-arc>)
  set_target_properties(${NAME} PROPERTIES
    OUTPUT_NAME "WoW Model Viewer"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/installer/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "net.wowmodelviewer.viewer"
    MACOSX_BUNDLE_BUNDLE_NAME "WoW Model Viewer"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.2"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.2")
endif()

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

target_link_libraries(${NAME}
  wmvrender
  ${extralibs}
  Qt5::Xml
  Qt5::Gui
  Qt5::Network
  Qt5::Widgets
  ${wxWidgets_LIBRARIES}
)



if(WIN32)
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
  # The WoWDBDefs table definitions: every table is checked against the loaded client's own layout through them
  # (WoWDatabase::refreshStructures), so they ship next to the executable (dbd/).
  file(GLOB wow_dbd_files ${CMAKE_SOURCE_DIR}/bin_support/dbd/*.dbd)
  install(FILES ${wow_dbd_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/dbd)
endif()

# THE RUNTIME DATA next to the program, so a build runs where it was built: the database schemas (games/wow/<ver>),
# the table definitions (dbd/), the encryption key list, and the icon. On macOS inside the bundle's Resources; the
# viewer links them into its per-user home folder at start (see app.cpp, the home folder).
if(APPLE)
  set(WMV_RUNTIME_DATA_DIR "$<TARGET_BUNDLE_CONTENT_DIR:${NAME}>/Resources")
elseif(UNIX)
  set(WMV_RUNTIME_DATA_DIR "$<TARGET_FILE_DIR:${NAME}>/share")
endif()
if(NOT WIN32)
  add_custom_command(TARGET ${NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WMV_RUNTIME_DATA_DIR}/games/wow" "${WMV_RUNTIME_DATA_DIR}/dbd"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin_support/wow/9.2 "${WMV_RUNTIME_DATA_DIR}/games/wow/9.2"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin_support/wow/10.0 "${WMV_RUNTIME_DATA_DIR}/games/wow/10.0"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin_support/wow/10.1 "${WMV_RUNTIME_DATA_DIR}/games/wow/10.1"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin_support/wow/12.0 "${WMV_RUNTIME_DATA_DIR}/games/wow/12.0"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin_support/dbd "${WMV_RUNTIME_DATA_DIR}/dbd"
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin_support/Encryption/extraEncryptionKeys.csv "${WMV_RUNTIME_DATA_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv.icns ${CMAKE_SOURCE_DIR}/bin_support/Icons/WMW-Midnight-26-256x256.png
            ${CMAKE_SOURCE_DIR}/bin_support/Icons/wmv.xpm ${CMAKE_SOURCE_DIR}/bin_support/Splash/Splash-006-Midnight.png "${WMV_RUNTIME_DATA_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt ${CMAKE_SOURCE_DIR}/LICENSE "${WMV_RUNTIME_DATA_DIR}/"
    COMMENT "Staging the viewer's runtime data"
    VERBATIM)
endif()
