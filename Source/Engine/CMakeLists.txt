# core lib (Engine)
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building Engine (core)")

include_directories(Core)
include_directories(Core/Logger)
include_directories(Core/Events)
include_directories(Core/Containers)
include_directories(Core/Memory)
include_directories(IO)
include_directories(Database)
include_directories(Plugin)
include_directories(Settings)
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/stb)
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/include)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(src Core/CharInfos.cpp
    Core/Game.cpp
    Core/Model.cpp
    Core/NPCInfos.cpp
    Core/stb_impl.cpp
    Core/ZipExtract.cpp
    Core/Logger/Logger.cpp
    Core/Logger/LogOutputConsole.cpp
    Core/Logger/LogOutputFile.cpp
    Core/Events/Component.cpp
    Core/Events/Event.cpp
    Core/Events/Observable.cpp
    Core/Events/Observer.cpp
    Core/Memory/MemoryUtils.cpp
    IO/GameFile.cpp
    IO/GameFolder.cpp
    IO/HttpClient.cpp
    IO/SoftwareImage.cpp
    Database/GameDatabase.cpp
    Database/dbfile.cpp
    Database/DBDFile.cpp
    Plugin/ExporterPlugin.cpp
    Plugin/ImporterPlugin.cpp
    Settings/GlobalSettings.cpp)

set(headers Core/CharInfos.h
      Core/Game.h
      Core/IniFile.h
      Core/Model.h
      Core/NPCInfos.h
      Core/string_utils.h
      Core/ZipExtract.h
      Core/Logger/Logger.h
      Core/Logger/LogOutput.h
      Core/Logger/LogOutputConsole.h
      Core/Logger/LogOutputFile.h
      Core/Events/Component.h
      Core/Events/Event.h
      Core/Events/Observable.h
      Core/Events/Observer.h
      Core/Containers/BaseIterator.h
      Core/Containers/Container.h
      Core/Memory/MemoryUtils.h
      IO/GameFile.h
      IO/GameFolder.h
      IO/HttpClient.h
      IO/SoftwareImage.h
      Database/GameDatabase.h
      Database/dbfile.h
      Database/DBDFile.h
      Plugin/ExporterPlugin.h
      Plugin/ImporterPlugin.h
      Settings/GlobalSettings.h)

source_group("Header Files" FILES ${headers})

use_glad()

set(NAME core)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} pugixml-static nlohmann_json::nlohmann_json winhttp ${CMAKE_SOURCE_DIR}/ThirdParty/lib/x64/zlib.lib ${extralibs})
