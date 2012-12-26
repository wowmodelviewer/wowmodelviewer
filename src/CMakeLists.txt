project(wowmodelviewer)
cmake_minimum_required(VERSION 2.6)

set(CMAKE_BUILD_TYPE MinSizeRel)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

if(WIN32)
	set(RES_FILES "wmv_mingw.rc")
	set(CMAKE_RC_COMPILER_INIT windres)
	ENABLE_LANGUAGE(RC)
	SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")
endif()


set(USE_STORM 1)
if (APPLE)
	message(STATUS "Using Mac OS X port")
	add_definitions(-D_MAC)
	add_definitions(-D_MAC_INTEL)
	find_package(GLEW REQUIRED)
	find_package(BZip2 REQUIRED)
	find_package(ZLIB REQUIRED)
	find_package(JPEG REQUIRED)
	find_package(PNG REQUIRED)

	set(CMAKE_OSX_ARCHITECTURES i386)
	set(MACOSX_BUNDLE_ICON_FILE wmv.icns)
	set(MACOSX_BUNDLE_INFO_STRING "World of Warcraft ModelViewer")
	
	set_source_files_properties("${MACOSX_BUNDLE_ICON_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

	set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS> ;<CMAKE_RANLIB> -c <TARGET> ")
	set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS> ;<CMAKE_RANLIB> -c <TARGET> ")
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL Linux)
	message(STATUS "Using Linux port")
	add_definitions(-D_LINUX)
	add_definitions(-D_LINUX32)
	find_package(GLEW REQUIRED)
	find_package(BZip2 REQUIRED)
	find_package(ZLIB REQUIRED)
	find_package(JPEG REQUIRED)
	find_package(PNG REQUIRED)
	set (EXTRA_LIBS ${EXTRA_LIBS} GLU)
endif()

if (WIN32)
	message(STATUS "Using Windows MinGW version")
	set(JPEG_INCLUDE_DIR c:/MinGW/include)
endif()

add_subdirectory(CxImage)

# adding compilation from next gen software
add_subdirectory(next-gen)
	
if (USE_STORM)
    	message(STATUS "Using Stormlib")
    	add_subdirectory(stormlib)
    	add_definitions(-DUSE_STORM)
    	set (EXTRA_LIBS ${EXTRA_LIBS} StormLib)
else ()
    	message(STATUS "Using libmpq")
    	add_library(mpq STATIC ${MPQ_SOURCES})
    	set (EXTRA_LIBS ${EXTRA_LIBS} mpq)
endif()

add_definitions(
    -DWotLK -Wall -Wno-unknown-pragmas -g
)

include(${wxWidgets_USE_FILE})
include(cmake/sources.cmake)

if(WIN32)
	message(STATUS "Using glew")
	include(glew/glew.cmake)
	add_definitions(-DGLEW_STATIC)
	set(WOWMV_SOURCES ${WOWMV_SOURCES} ${GLEW_SRC})
	message("glew src : ${GLEW_SRC}")
	message("glew include : ${GLEW_INCLUDE_DIR}")

	include_directories(${CMAKE_SOURCE_DIR}
		${wxWidgets_INCLUDE_DIRS}
		${GLEW_INCLUDE_DIR})
else()
	include_directories(${CMAKE_SOURCE_DIR}
		${wxWidgets_INCLUDE_DIRS}
		${BZIP2_INCLUDE_DIR}
		${ZLIB_INCLUDE_DIR}
		${GLEW_INCLUDE_DIR}
		${JPEG_INCLUDE_DIR}
		${PNG_PNG_INCLUDE_DIR})
endif()


if (APPLE)
	add_executable(wowmodelviewer MACOSX_BUNDLE ${WOWMV_SOURCES} ../bin_support/Icons/${MACOSX_BUNDLE_ICON_FILE})
    add_dependencies(wowmodelviewer CxImage StormLib)   
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL Linux)
	add_executable(wowmodelviewer ${WOWMV_SOURCES})
    add_dependencies(wowmodelviewer CxImage StormLib)   
endif()

if(WIN32)
    add_definitions(-D_WINDOWS)
	add_definitions(-D_MINGW)
	add_definitions(-D_BETAVERSION) # comment if you are building a released version
	set(CMAKE_EXE_LINKER_FLAGS "-s") # strip exe
	
    add_executable(wowmodelviewer WIN32 ${WOWMV_SOURCES} ${RES_FILES} )
    add_dependencies(wowmodelviewer CxImage StormLib nextgen)   
endif()

if(WIN32)
	target_link_libraries(wowmodelviewer
		cximage
		${wxWidgets_LIBRARIES}
		${EXTRA_LIBS}
		jpeg
		png
		nextgen
	)
else()
	target_link_libraries(wowmodelviewer
		${EXTRA_LIBS}
		cximage
		${wxWidgets_LIBRARIES}
		${BZIP2_LIBRARIES}
		${ZLIB_LIBRARIES}
		${GLEW_LIBRARIES}
		${JPEG_LIBRARIES}
		${PNG_LIBRARIES}
	)
endif()

if(WIN32)
	install(TARGETS wowmodelviewer 
        	RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
        
	file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/../bin_support/MinGW/*.dll")
	install(FILES ${files} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
endif()
        
