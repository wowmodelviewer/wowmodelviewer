include(ExternalProject)
# retrieve project form ladislav's github repo
# GIT_TAG is used to control update, and avoid pulling a casclib version that would break WoW Model Viewer
ExternalProject_Add(casclib
    GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
	GIT_TAG 1048b02
	CMAKE_ARGS -DCASC_BUILD_STATIC_LIB=ON -DCASC_BUILD_SHARED_LIB=OFF -DCASC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=${CASCLIB_INSTALL_LOCATION}
)

set_property(TARGET casclib PROPERTY FOLDER "external libraries")
