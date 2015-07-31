project(wow)
cmake_minimum_required(VERSION 2.6)

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

message(STATUS "Building wow library")

# search for required package (wxwidgets)
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Xml)
find_package(Qt5Gui)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(src animated.cpp
        AnimManager.cpp
        Attachment.cpp
        Bone.cpp
        CASCFile.cpp
        CASCFolder.cpp
        CharDetails.cpp
        CharTexture.cpp
        database.cpp
        dbcfile.cpp
        ddslib.cpp
        GameDatabase.cpp
        GameFile.cpp
        globalvars.cpp
        ModelAttachment.cpp
        ModelCamera.cpp
        ModelColor.cpp
        ModelEvent.cpp
        ModelLight.cpp
        ModelManager.cpp
        ModelRenderPass.cpp
        ModelTransparency.cpp
        particle.cpp
        RaceInfos.cpp
        RenderTexture.cpp
        TabardDetails.cpp
        TextureAnim.cpp
        video.cpp
        wmo.cpp
        WoWItem.cpp
        WoWModel.cpp)

list(APPEND src sqlite3.c)
list(APPEND src ${CMAKE_SOURCE_DIR}/glew/src/glew.c)

add_definitions(-DGLEW_STATIC)

include_directories(${CMAKE_SOURCE_DIR}/glew/include)

add_definitions(-DBUILDING_WOW_DLL)

add_library(wow SHARED ${src})

target_link_libraries(wow ${wxWidgets_LIBRARIES} core cximage CascLib Qt5::Core Qt5::Xml Qt5::Gui)

if(WIN32)
  install(TARGETS wow RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/")
endif(WIN32)