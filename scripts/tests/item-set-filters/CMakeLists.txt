cmake_minimum_required(VERSION 3.20)
project(ItemSetFilters LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(WMV_ROOT "" CACHE PATH "WMV checkout")
set(wxWidgets_ROOT_DIR "${WMV_ROOT}/ThirdParty/wxWidgets3")
set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/vc14x_x64_dll")
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# Compile the production dialog implementation, substituting only application
# dependencies and the two unrelated network importer callbacks. No dialog,
# filtering, selection or category methods are replaced.
file(READ "${WMV_ROOT}/Source/wowmodelviewer/itemselection.cpp" dialog)
foreach(header QString globalvars.h Game.h charcontrol.h ItemImporterDialog.h modelviewer.h NPCImporterDialog.h)
  string(REGEX REPLACE "#include [<\"]${header}[>\"]" "" dialog "${dialog}")
endforeach()
string(FIND "${dialog}" "void FilteredChoiceDialog::OnImportNPC" begin)
string(FIND "${dialog}" "void FilteredChoiceDialog::DoFilter" end)
string(SUBSTRING "${dialog}" 0 ${begin} prefix)
string(SUBSTRING "${dialog}" ${end} -1 suffix)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dialog.cpp" "#include \"receiver.h\"\n${prefix}\nvoid FilteredChoiceDialog::OnImportNPC(wxCommandEvent&) {}\nvoid FilteredChoiceDialog::OnImportItem(wxCommandEvent&) {}\n${suffix}")
add_executable(item-set-filters regression.cpp "${CMAKE_CURRENT_BINARY_DIR}/dialog.cpp")
target_include_directories(item-set-filters PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${WMV_ROOT}/Source/wowmodelviewer" "${WMV_ROOT}/Source/games/wow")
target_link_libraries(item-set-filters PRIVATE ${wxWidgets_LIBRARIES})

# Read the real catalog and exercise its two-level panel without loading assets.
set(QT_ROOT "" CACHE PATH "Qt installation")
find_package(Qt5Core REQUIRED PATHS "${QT_ROOT}/lib/cmake/Qt5Core")
add_executable(item-set-integration integration.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/dialog.cpp"
  "${WMV_ROOT}/Source/wowmodelviewer/ItemSetCatalog.cpp"
  "${WMV_ROOT}/Source/wowmodelviewer/ItemSetChoiceDialog.cpp"
  "${WMV_ROOT}/Source/core/sqlite3.c")
target_include_directories(item-set-integration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${WMV_ROOT}/Source/wowmodelviewer" "${WMV_ROOT}/Source/games/wow" "${WMV_ROOT}/Source/core")
target_link_libraries(item-set-integration PRIVATE ${wxWidgets_LIBRARIES} Qt5::Core)

file(READ "${WMV_ROOT}/Source/games/wow/WoWItem.cpp" item)
string(FIND "${item}" "bool WoWItem::setAppearanceId" begin)
string(FIND "${item}" "void WoWItem::setModifierId" end)
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${item}" ${begin} ${length} appearance)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/appearance-method.inc" "${appearance}")
add_executable(item-set-appearance appearance.cpp)
target_include_directories(item-set-appearance PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(item-set-appearance PRIVATE Qt5::Core)

file(READ "${WMV_ROOT}/Source/wowmodelviewer/charcontrol.cpp" control)
string(FIND "${control}" "void CharControl::applySetPieces" begin)
string(FIND "${control}" "void CharControl::selectStart" end)
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${control}" ${begin} ${length} equipment)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/equipment-method.inc" "${equipment}")
target_include_directories(item-set-appearance PRIVATE "${WMV_ROOT}/Source/wowmodelviewer" "${WMV_ROOT}/Source/games/wow")

add_executable(item-set-catalog catalog.cpp "${WMV_ROOT}/Source/wowmodelviewer/ItemSetCatalog.cpp")
target_include_directories(item-set-catalog PRIVATE "${WMV_ROOT}/Source/wowmodelviewer" "${WMV_ROOT}/Source/games/wow")
target_link_libraries(item-set-catalog PRIVATE Qt5::Core)
