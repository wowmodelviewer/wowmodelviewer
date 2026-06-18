# MSVC x64 toolchain for WoW Model Viewer.
#
# Resolves the MSVC compiler, the Windows SDK, and all required INCLUDE / LIB / PATH and
# resource-compiler settings via vswhere -- so the build is fully self-contained and does
# NOT require running from a Visual Studio developer prompt (no vcvars64 needed). Modelled
# on the WowViewer reference toolchain (cmake/toolchains/msvc-x64.cmake). The presets
# chainload this file through vcpkg via VCPKG_CHAINLOAD_TOOLCHAIN_FILE, so vcpkg port
# builds use the same self-contained environment.

# --- Find Visual Studio via vswhere ---
set(_vswhere_path "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
execute_process(
    COMMAND "${_vswhere_path}" -latest -products *
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath
    OUTPUT_VARIABLE _vs_install_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\\" "/" _vs_install_path "${_vs_install_path}")

file(GLOB _msvc_versions "${_vs_install_path}/VC/Tools/MSVC/*")
list(SORT _msvc_versions COMPARE NATURAL ORDER DESCENDING)
list(GET _msvc_versions 0 _msvc_toolset_path)

# --- Find Windows SDK (newest installed) ---
set(_sdk_root "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
string(REPLACE "\\" "/" _sdk_root "${_sdk_root}")
file(GLOB _sdk_versions "${_sdk_root}/Include/*")
list(SORT _sdk_versions COMPARE NATURAL ORDER DESCENDING)
list(GET _sdk_versions 0 _sdk_include_best)
get_filename_component(_sdk_version "${_sdk_include_best}" NAME)

# --- Compilers and tools ---
set(CMAKE_C_COMPILER   "${_msvc_toolset_path}/bin/Hostx64/x64/cl.exe")
set(CMAKE_CXX_COMPILER "${_msvc_toolset_path}/bin/Hostx64/x64/cl.exe")
set(CMAKE_LINKER       "${_msvc_toolset_path}/bin/Hostx64/x64/link.exe")
set(CMAKE_AR           "${_msvc_toolset_path}/bin/Hostx64/x64/lib.exe")
set(CMAKE_RC_COMPILER  "${_sdk_root}/bin/${_sdk_version}/x64/rc.exe")
set(CMAKE_MT           "${_sdk_root}/bin/${_sdk_version}/x64/mt.exe")

# --- Set env vars for configure-time compiler tests (and vcpkg port builds) ---
set(ENV{INCLUDE}
    "${_msvc_toolset_path}/include;${_sdk_root}/Include/${_sdk_version}/ucrt;${_sdk_root}/Include/${_sdk_version}/um;${_sdk_root}/Include/${_sdk_version}/shared;${_sdk_root}/Include/${_sdk_version}/winrt;${_sdk_root}/Include/${_sdk_version}/cppwinrt"
)
set(ENV{LIB}
    "${_msvc_toolset_path}/lib/x64;${_sdk_root}/Lib/${_sdk_version}/ucrt/x64;${_sdk_root}/Lib/${_sdk_version}/um/x64"
)
set(ENV{PATH}
    "${_msvc_toolset_path}/bin/Hostx64/x64;${_sdk_root}/bin/${_sdk_version}/x64;$ENV{PATH}"
)

# --- Bake include dirs into every compile command (persists to build step) ---
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${_msvc_toolset_path}/include"
    "${_sdk_root}/Include/${_sdk_version}/ucrt"
    "${_sdk_root}/Include/${_sdk_version}/um"
    "${_sdk_root}/Include/${_sdk_version}/shared"
    "${_sdk_root}/Include/${_sdk_version}/winrt"
    "${_sdk_root}/Include/${_sdk_version}/cppwinrt"
)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES})

# --- Bake lib dirs into every link command (persists to build step) ---
set(_linker_flags "")
foreach(_dir IN ITEMS
    "${_msvc_toolset_path}/lib/x64"
    "${_sdk_root}/Lib/${_sdk_version}/ucrt/x64"
    "${_sdk_root}/Lib/${_sdk_version}/um/x64"
)
    string(APPEND _linker_flags " \"/LIBPATH:${_dir}\"")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_linker_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_linker_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_linker_flags}")

# --- Resource compiler include paths ---
set(CMAKE_RC_FLAGS_INIT
    "/I \"${_msvc_toolset_path}/include\" /I \"${_sdk_root}/Include/${_sdk_version}/ucrt\" /I \"${_sdk_root}/Include/${_sdk_version}/um\" /I \"${_sdk_root}/Include/${_sdk_version}/shared\""
)

# --- Dynamic CRT (/MD release, /MDd debug) to match vcpkg x64-windows-static-md ---
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
