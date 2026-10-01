cmake_minimum_required(VERSION 3.28)

project(IconComposer
    VERSION 0.1.0
    DESCRIPTION "Apple's Icon Composer, decoded and reimplemented"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IC_BUILD_TESTS "Builds the test suite" ON)
option(IC_SANITIZE "Builds with ASan/UBSan" OFF)
option(IC_BUILD_UI "Builds IconComposerKit and the app (GLFW + Dear ImGui + Vulkan)" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(IC_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# The two executables a release ships stand alone: with MinGW the C++ runtime,
# libgcc and winpthreads are linked in, so no toolchain DLL has to travel beside
# them (measured 30/09: launched from another shell, the editor found another
# toolchain's `libstdc++-6.dll` first and stopped before the window, on
# `__codecvt_utf8_utf16`). `cmake --install` puts them at the prefix root.
function(ic_release_executable target)
    target_compile_definitions(${target} PRIVATE IC_VERSION="${PROJECT_VERSION}")
    # Not in Debug: at -O0 `std::type_info::operator==` is emitted out of line in
    # our objects AND lives in the static libstdc++, and the link stops on the
    # duplicate. A Debug executable is not what a release ships.
    if(MINGW)
        target_link_options(${target} PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-static-libgcc;-static-libstdc++;-static>)
    endif()
    install(TARGETS ${target} RUNTIME DESTINATION .)
endfunction()

# The towers, in topological order -- see Docs/Specs/2026-08-31-arquitetura-design.md.
# Each is named after the image of the target it answers for, and each is a clean
# reimplementation of what that image does, never a copy of it.
add_subdirectory(Source/IconComposerFoundation)
add_subdirectory(Source/CoreSVG)
add_subdirectory(Source/RenderBox)
add_subdirectory(Source/cli)

if(IC_BUILD_UI)
    # A janela e nossa: GLFW, Dear ImGui (docking) e os dois backends dela, e
    # nada mais. Ate 30/09 isto vinha do OnyxSDK inteiro -- tema, settings,
    # registrar, a barra de menus DELE por cima da nossa --, e o editor usava
    # uma fracao e brigava com o resto. Os pins sao os mesmos que o Onyx usava
    # (imgui 1.92.9 WIP docking, build 19282), so a GLFW subiu para 3.4, que
    # declara um cmake_minimum que o CMake 4 aceita sem o escape de politica.
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL SYSTEM)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        2af6dd9694288e6befe1edb7ce25510911693c22
        SOURCE_SUBDIR  _no_cmake_here
        EXCLUDE_FROM_ALL SYSTEM)
    # As fontes de terceiros nao respondem aos avisos desta arvore: medido,
    # glfw e imgui devolvem ~700 linhas a -Wall -Wextra -Wpedantic.
    get_directory_property(_ic_saved_opts COMPILE_OPTIONS)
    set(_ic_dep_opts "${_ic_saved_opts}")
    list(REMOVE_ITEM _ic_dep_opts -Wall -Wextra -Wpedantic /W4 /permissive-)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_ic_dep_opts}")
    # A GLFW so e a casca fora do Windows (Shell.h); no Windows a janela e
    # Win32 direta, para poder nascer com alfa por pixel.
    if(WIN32)
        FetchContent_MakeAvailable(imgui)
    else()
        FetchContent_MakeAvailable(glfw imgui)
    endif()

    # O nucleo do ImGui, sem janela: e o que o Kit e o `ic_tests` linkam.
    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp)
    target_include_directories(imgui_lib SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    # Os backends, so para o executavel. Sem volk: o loader do Vulkan direto,
    # o mesmo caminho que a RenderBox ja faz quando nao ha volk no build.
    find_package(Vulkan REQUIRED)
    if(WIN32)
        add_library(imgui_backends STATIC
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
        target_link_libraries(imgui_backends PUBLIC imgui_lib Vulkan::Vulkan dwmapi)
    else()
        add_library(imgui_backends STATIC
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
        target_link_libraries(imgui_backends PUBLIC imgui_lib glfw Vulkan::Vulkan)
    endif()
    target_include_directories(imgui_backends SYSTEM PUBLIC ${imgui_SOURCE_DIR}/backends)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_ic_saved_opts}")

    add_subdirectory(Source/IconComposerKit)
    add_subdirectory(Source/app)
endif()

if(IC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# `cpack` writes IconComposer-<version>.zip: the two executables at its root,
# nothing else.
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "IconComposer-${PROJECT_VERSION}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
