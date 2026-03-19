# core lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building nextgen Core")

include_directories(.)
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/stb)
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/include)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(src CharInfos.cpp
    CSVFile.cpp
    DBDFile.cpp
    dbfile.cpp
    ExporterPlugin.cpp
    HttpClient.cpp
    Game.cpp
    GameDatabase.cpp
    GameFile.cpp
    GameFolder.cpp
    GlobalSettings.cpp
    ImporterPlugin.cpp
    MemoryUtils.cpp
    Model.cpp
    NPCInfos.cpp
    metaclasses/Component.cpp
    metaclasses/Event.cpp
    metaclasses/Observable.cpp
    metaclasses/Observer.cpp
    SoftwareImage.cpp
    stb_impl.cpp
    ZipExtract.cpp
    logger/Logger.cpp
    logger/LogOutputConsole.cpp
    logger/LogOutputFile.cpp)

set(headers CharInfos.h
      CSVFile.h
      DBDFile.h
      dbfile.h
      ExporterPlugin.h
      HttpClient.h
      Game.h
      IniFile.h
      GameDatabase.h
      GameFile.h
      GameFolder.h
      GlobalSettings.h
      ImporterPlugin.h
      MemoryUtils.h
      Model.h
      NPCInfos.h
      SoftwareImage.h
      string_utils.h
      ZipExtract.h
      metaclasses/BaseIterator.h
      metaclasses/Component.h
      metaclasses/Container.h
      metaclasses/Event.h
      metaclasses/Observable.h
      metaclasses/Observer.h
      logger/Logger.h
      logger/LogOutput.h
      logger/LogOutputConsole.h
      logger/LogOutputFile.h)

source_group("Header Files" FILES ${headers})

use_glad()
use_sqlite()

set(NAME core)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} pugixml-static nlohmann_json::nlohmann_json winhttp ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib ${extralibs})
