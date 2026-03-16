# wowmodelviewer exe
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

include_directories(.)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# add wmv source files compilation
set(src  AnimationExportChoiceDialog.cpp
         animcontrol.cpp
         app.cpp
         charcontrol.cpp
         CharDetailsCustomizationChoice.cpp
         CharDetailsFrame.cpp
         DisplaySettings.cpp
         ExportSettings.cpp
         filecontrol.cpp
         GeneralSettings.cpp
         ItemImporterDialog.cpp
         itemselection.cpp
         lightcontrol.cpp
         LogStackWalker.cpp
         main.cpp
         modelbankcontrol.cpp
         modelcanvas.cpp
         modelcontrol.cpp
         modelviewer.cpp
         NPCImporterDialog.cpp
         OrbitCamera.cpp
         SettingsControl.cpp
         shaders.cpp
         UserSkins.cpp
         util.cpp)

set(headers AnimationExportChoiceDialog.h
            animcontrol.h
            app.h
            charcontrol.h
            CharDetailsCustomizationChoice.h
            CharDetailsFrame.h
            DisplaySettings.h
            enums.h
            ExportSettings.h
            filecontrol.h
            GeneralSettings.h
            ItemImporterDialog.h
            itemselection.h
            lightcontrol.h
            LogStackWalker.h
            modelbankcontrol.h
            modelcanvas.h
            modelcontrol.h
            modelviewer.h
            NPCImporterDialog.h
            OrbitCamera.h
             resource1.h
            SettingsControl.h
            shaders.h
            UserSkins.h
            util.h)

source_group("Header Files" FILES ${headers})

# add external code usage
use_glad()
use_core()
use_wow()
use_casclib()

# Exporter/importer include directories (static libraries linked directly into the executable)
include_directories(${CMAKE_SOURCE_DIR}/Source/exporters/obj)
include_directories(${CMAKE_SOURCE_DIR}/Source/exporters/fbx)
include_directories(${CMAKE_SOURCE_DIR}/Source/importers/armory)
include_directories(${CMAKE_SOURCE_DIR}/Source/importers/wowhead)

set(RES_FILES "wmv.rc")

# extra useful files for visual studio project
set(cmake_files ${CMAKE_SOURCE_DIR}/cmake/common.cmake
                ${CMAKE_SOURCE_DIR}/cmake/policies.cmake
                ${CMAKE_SOURCE_DIR}/cmake/windows.cmake)

source_group("cmake" FILES ${cmake_files})

set(common_files 
                 ${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi
                 ${CMAKE_SOURCE_DIR}/.github/workflows/build.yml)

# Auto-discover WoW version directories and their support files.
# To add a new version, just create a new folder under bin_support/wow/ with the data files.
file(GLOB wow_version_dirs LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/bin_support/wow/*)

set(all_wow_files "")
foreach(dir ${wow_version_dirs})
  if(IS_DIRECTORY ${dir})
    get_filename_component(ver ${dir} NAME)
    file(GLOB ver_files ${dir}/*)
    source_group("wow${ver}" FILES ${ver_files})
    list(APPEND all_wow_files ${ver_files})
  endif()
endforeach()

set(useful_files ${common_files} ${cmake_files} ${all_wow_files})

set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

set(NAME wowmodelviewer)
add_executable(${NAME} WIN32 ${src} ${headers} ${RES_FILES} ${useful_files} )

set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

# Force-include glad/gl.h before all source files to prevent gl.h ordering issues with wxWidgets 3.x
if(MSVC)
  target_compile_options(${NAME} PRIVATE /FIglad/gl.h)
endif()

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

target_link_libraries(${NAME}
  ${extralibs}
  objexporter
  fbxexporter
  armory
  wowhead
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/libfbxsdk.lib
  Qt6::Xml
  Qt6::Gui
  Qt6::Network
  Qt6::Widgets
  ${wxWidgets_LIBRARIES}
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib
  ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/libpng.lib
  ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64/lib/vc_x64_lib/nanosvg.lib
  ${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64/lib/vc_x64_lib/nanosvgrast.lib
)

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})

# additional files needed to let WMV correctly works
set(QT_BIN_DIR  ${QT_LOCATION}/bin)
set(QT_FILES ${QT_BIN_DIR}/Qt6Core.dll
             ${QT_BIN_DIR}/Qt6Cored.dll
             ${QT_BIN_DIR}/Qt6Gui.dll
             ${QT_BIN_DIR}/Qt6Guid.dll
             ${QT_BIN_DIR}/Qt6Network.dll
             ${QT_BIN_DIR}/Qt6Networkd.dll
             ${QT_BIN_DIR}/Qt6Widgets.dll
             ${QT_BIN_DIR}/Qt6Widgetsd.dll
             ${QT_BIN_DIR}/Qt6Xml.dll
             ${QT_BIN_DIR}/Qt6Xmld.dll)

set(QT_PLUGIN_DIR ${QT_LOCATION}/plugins)
set(QT_PLUGIN_TLS_FILES ${QT_PLUGIN_DIR}/tls/qschannelbackend.dll
                        ${QT_PLUGIN_DIR}/tls/qschannelbackendd.dll)

set(QT_PLUGIN_IMG_FILES ${QT_PLUGIN_DIR}/imageformats/qgif.dll
                        ${QT_PLUGIN_DIR}/imageformats/qgifd.dll
                        ${QT_PLUGIN_DIR}/imageformats/qico.dll
                        ${QT_PLUGIN_DIR}/imageformats/qicod.dll
                        ${QT_PLUGIN_DIR}/imageformats/qjpeg.dll
                        ${QT_PLUGIN_DIR}/imageformats/qjpegd.dll
                        ${QT_PLUGIN_DIR}/imageformats/qsvg.dll
                        ${QT_PLUGIN_DIR}/imageformats/qsvgd.dll)

set(EXTRA_FILES ${CMAKE_SOURCE_DIR}/bin_support/vcredist_x64.exe)

set(files ${QT_FILES} ${EXTRA_FILES})

install(FILES ${files} DESTINATION ${BIN_DIR})
install(FILES ${QT_PLUGIN_TLS_FILES} DESTINATION ${BIN_DIR}plugins/tls)
install(FILES ${QT_PLUGIN_IMG_FILES} DESTINATION ${BIN_DIR}plugins/imageformats)
foreach(dir ${wow_version_dirs})
  if(IS_DIRECTORY ${dir})
    get_filename_component(ver ${dir} NAME)
    file(GLOB ver_files ${dir}/*)
    install(FILES ${ver_files} DESTINATION ${CMAKE_SOURCE_DIR}/bin/games/wow/${ver})
  endif()
endforeach()
