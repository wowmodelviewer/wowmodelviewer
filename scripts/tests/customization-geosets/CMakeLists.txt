cmake_minimum_required(VERSION 3.20)
project(CustomizationGeosets LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(WMV_ROOT "" CACHE PATH "WMV checkout")
set(QT_ROOT "" CACHE PATH "Qt installation")
find_package(Qt5Core REQUIRED PATHS "${QT_ROOT}/lib/cmake/Qt5Core")

function(slice text begin end out)
  string(FIND "${text}" "${begin}" first)
  string(FIND "${text}" "${end}" last)
  if(first LESS 0 OR last LESS first)
    message(FATAL_ERROR "Cannot extract ${begin}")
  endif()
  math(EXPR length "${last}-${first}")
  string(SUBSTRING "${text}" ${first} ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# The production geoset code, unchanged: CharDetails::applyCustomizationGeosets(), WoWModel::showGeoset() and
# setGeosetDisplayById(), and the default visibility rule of WoWModel::refresh() (run here as applyDefaultGeosets(),
# the step refresh() takes right before the customization).
file(READ "${WMV_ROOT}/Source/games/wow/CharDetails.cpp" details)
file(READ "${WMV_ROOT}/Source/games/wow/WoWModel.cpp" model)
slice("${details}" "void CharDetails::applyCustomizationGeosets()" "void CharDetails::refreshTextures()" apply)
slice("${model}" "void WoWModel::showGeoset(uint geosetindex, bool value)" "bool WoWModel::isGeosetDisplayed" show)
slice("${model}" "void WoWModel::setGeosetDisplayById(int geosetId, bool display)" "void WoWModel::setCreatureGeosetData" byId)
slice("${model}" "  // Default geoset visibility (per the character customization rules):" "  // Apply customization-choice geosets" defaults)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production.cpp"
  "#include \"receiver.h\"\n${show}\n${byId}\nvoid WoWModel::applyDefaultGeosets()\n{\n${defaults}}\n\n${apply}")

add_executable(customization-geosets regression.cpp "${CMAKE_CURRENT_BINARY_DIR}/production.cpp")
target_include_directories(customization-geosets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(customization-geosets PRIVATE Qt5::Core)
