cmake_minimum_required(VERSION 2.6)
include(cmake/common.cmake)

# add external libraries compilation
add_subdirectory(CxImage)
add_subdirectory(casclib)
        
# add internal libraries compilation 
add_subdirectory(core)
add_subdirectory(games)

# then trigs executables compilation
add_subdirectory(wowmodelviewer)
add_subdirectory(UpdateManager)

# add plugins compilation
add_subdirectory(plugins)

add_custom_target(release nmake clean install
                  COMMAND $ENV{WMV_SDK_BASEDIR}/NSIS/makensis.exe "$ENV{WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi")