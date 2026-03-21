# ---------------------------------------------------------------------------
# cmake/docs.cmake — Optional Doxygen documentation target
# ---------------------------------------------------------------------------
# Adds a 'docs' build target that generates API documentation from Source/.
# Requires Doxygen to be installed; skipped gracefully if not found.
#
# Usage:
#   cmake --build <build-dir> --target docs
# ---------------------------------------------------------------------------

find_package(Doxygen QUIET)

if(NOT DOXYGEN_FOUND)
  message(STATUS "Doxygen not found — 'docs' target will not be available.")
  return()
endif()

message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")

# Check for Graphviz dot (used for class/call graphs).
if(DOXYGEN_DOT_FOUND)
  set(DOXYGEN_HAVE_DOT "YES")
  message(STATUS "Graphviz dot found — graphs will be generated.")
else()
  set(DOXYGEN_HAVE_DOT "NO")
  message(STATUS "Graphviz dot not found — graphs will be skipped.")
endif()

# Paths used by Doxyfile.in
set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
set(DOXYGEN_AWESOME_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/doxygen-awesome-css")

# Configure the Doxyfile template (substitute @VARIABLES@).
set(DOXYFILE_IN  "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in")
set(DOXYFILE_OUT "${CMAKE_BINARY_DIR}/Doxyfile")
configure_file("${DOXYFILE_IN}" "${DOXYFILE_OUT}" @ONLY)

add_custom_target(docs
  COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYFILE_OUT}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Generating API documentation with Doxygen..."
  VERBATIM
)

set_property(TARGET docs PROPERTY FOLDER "Documentation")
