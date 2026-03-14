# add internal libraries compilation 
add_subdirectory(core)
add_subdirectory(games)

# add exporter/importer static libraries (linked into the main executable)
add_subdirectory(exporters/obj)
add_subdirectory(exporters/fbx)
add_subdirectory(importers/armory)
add_subdirectory(importers/wowhead)

# then trigs executables compilation
add_subdirectory(wowmodelviewer)
