cmake_minimum_required(VERSION 2.6)
project(WoWModelViewer)
include(cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# add external libraries compilation
add_subdirectory(CxImage)

# configure CASC lib for our usage
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
add_subdirectory(casclib)

# add internal libraries compilation 
add_subdirectory(core)
add_subdirectory(games)

# then trigs executables compilation
add_subdirectory(wowmodelviewer)
add_subdirectory(UpdateManager)
add_subdirectory(ListFileGenerator)

# add plugins compilation
add_subdirectory(plugins)

add_custom_target(release
				  COMMAND
					"${CMAKE_COMMAND}" --build . --target clean --config Release
				  COMMAND
					"${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
					$ENV{WMV_SDK_BASEDIR}/NSIS/makensis.exe "$ENV{WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
				  )
