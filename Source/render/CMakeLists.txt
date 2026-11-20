# The viewport's renderer: Vulkan (MoltenVK on macOS).
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building the Vulkan viewport renderer")

set(shader_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${gen_dir})

set(shader_headers)
foreach(stage vert frag)
  set(src ${shader_dir}/model.${stage})
  set(spv ${gen_dir}/model.${stage}.spv)
  set(hdr ${gen_dir}/model_${stage}_spv.h)
  add_custom_command(
    OUTPUT ${hdr}
    COMMAND Vulkan::glslc --target-env=vulkan1.1 -O -I ${shader_dir} ${src} -o ${spv}
    COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${hdr} -DNAME=model_${stage}_spv -P ${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake
    DEPENDS ${src} ${shader_dir}/common.glsl ${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake
    COMMENT "Compiling viewport shader model.${stage}"
    VERBATIM)
  list(APPEND shader_headers ${hdr})
endforeach()

set(src VulkanViewport.cpp)
set(headers VulkanViewport.h PlatformSurface.h)
if(APPLE)
  list(APPEND src PlatformSurfaceMac.mm)
elseif(WIN32)
  list(APPEND src PlatformSurfaceWin.cpp)
else()
  list(APPEND src PlatformSurfaceLinux.cpp)
endif()

use_core()
use_wow()

add_library(wmvrender STATIC ${src} ${headers} ${shader_headers})
set_property(TARGET wmvrender PROPERTY FOLDER "libraries")
target_include_directories(wmvrender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/ThirdParty PRIVATE ${gen_dir} ${shader_dir})
target_link_libraries(wmvrender PUBLIC Vulkan::Vulkan Qt5::Core ${extralibs})

if(WIN32)
  # vulkan_win32.h brings windows.h, whose min/max macros would break std::min/std::max here.
  target_compile_definitions(wmvrender PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(APPLE)
  set_source_files_properties(PlatformSurfaceMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(wmvrender PUBLIC "-framework AppKit" "-framework QuartzCore" "-framework Metal")
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  find_package(X11 REQUIRED)
  target_link_libraries(wmvrender PUBLIC PkgConfig::GTK3 X11::X11)
endif()
