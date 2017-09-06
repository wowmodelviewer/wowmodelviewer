cmake_minimum_required(VERSION 2.6)
include(cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# add external libraries compilation
add_subdirectory(CxImage)

# configure CASC lib for our usage
set(WITH_STATIC ON CACHE BOOL "Disable CASC dynamic library compilation")
set(WITH_DYNAMIC OFF CACHE BOOL "Enable CASC static library compilation")
set(WITH_INSTALL OFF CACHE BOOL "Disable CASC library installation")
add_subdirectory(casclib)
set_property(TARGET casc_static PROPERTY FOLDER "external libraries")
        
# add internal libraries compilation 
add_subdirectory(core)
add_subdirectory(games)

# then trigs executables compilation
add_subdirectory(wowmodelviewer)
add_subdirectory(UpdateManager)
add_subdirectory(ListFileGenerator)

# add plugins compilation
add_subdirectory(plugins)

add_custom_target(release nmake clean install
                  COMMAND $ENV{WMV_SDK_BASEDIR}/NSIS/makensis.exe "$ENV{WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi")