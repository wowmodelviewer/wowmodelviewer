cmake_minimum_required(VERSION 2.6)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/common.cmake)

project(wow)

message(STATUS "Building wow library")

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Xml)
find_package(Qt5Gui)
find_package(Qt5Network)
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
        ddslib.cpp
        globalvars.cpp
        HardDriveFile.cpp
        ModelAttachment.cpp
        ModelCamera.cpp
        ModelColor.cpp
        ModelEvent.cpp
        ModelLight.cpp
        ModelManager.cpp
        ModelRenderPass.cpp
        ModelTransparency.cpp
        particle.cpp
        quaternion.cpp
        RaceInfos.cpp
        RenderTexture.cpp
        TabardDetails.cpp
		Texture.cpp
        TextureAnim.cpp
		TextureManager.cpp
        video.cpp
		wdb2file.cpp
		wdb5file.cpp
		wdb6file.cpp
		wdc1file.cpp
		wdc2file.cpp
        wmo.cpp
		WMOFog.cpp
		WMOGroup.cpp
		WMOLight.cpp
		WMOModelInstance.cpp
		WoWDatabase.cpp
		WoWFolder.cpp
        WoWItem.cpp
        WoWModel.cpp)

set(headers animated.h
			AnimManager.h
			Attachment.h
			BaseCanvas.h
			Bone.h
			CASCChunks.h
			CASCFile.h
			CASCFolder.h
			CharDetails.h
			CharDetailsEvent.h
			CharTexture.h
			database.h
			ddslib.h
			displayable.h
			FileTreeItem.h
			globalvars.h
			HardDriveFile.h
			manager.h
			matrix.h
			ModelAttachment.h
			ModelCamera.h
			ModelColor.h
			ModelEvent.h
			modelheaders.h
			ModelLight.h
			ModelManager.h
			ModelRenderPass.h
			ModelTransparency.h
			OpenGLHeaders.h
			particle.h
			quaternion.h
			RaceInfos.h
			RenderTexture.h
			TabardDetails.h
			TextureAnim.h
			types.h
			vec3d.h
			video.h
			wdb2file.h
			wdb5file.h
			wdb6file.h
			wdc1file.h
			wdc2file.h
			wmo.h
			WMOFog.h
			WMOGroup.h
			WMOLight.h
			WMOModelInstance.h
			wow_enums.h
			WoWDatabase.h
			WoWFolder.h
			WoWItem.h
			WoWModel.h)

source_group("Header Files" FILES ${headers})

use_core()
use_glew()
use_casclib()


add_definitions(-DBUILDING_WOW_DLL)

set(NAME wow)
add_library(${NAME} SHARED ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} core casc Qt5::Core Qt5::Xml Qt5::Gui)
add_dependencies(${NAME} casclib)

set(BIN_DIR ../../bin/)

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set(OUTPUT_DIR "${BIN_DIR}$(Configuration)/$(PlatformName)/")
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_DIR})
endif()


if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
endif(WIN32)