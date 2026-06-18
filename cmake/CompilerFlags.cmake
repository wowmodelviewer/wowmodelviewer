# CompilerFlags.cmake -- per-target warning settings for WoW Model Viewer's own targets.
#
# Mirrors WowViewer's wv_set_compiler_flags: a high warning level WITHOUT warnings-as-errors
# (the WMV codebase is old, so /WX is deliberately NOT applied). Call it only on first-party
# targets so the third-party libraries (imgui, glfw, casclib, pugixml, json, glad) stay quiet.
#
#   wmv_set_compiler_flags(<target>)

include_guard(GLOBAL)

function(wmv_set_compiler_flags target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
            /EHsc
        )
    elseif(MSVC)
        # Clang-CL: MSVC-compatible flags minus unsupported /Zc:preprocessor
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
        )
    endif()
endfunction()
