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
option(IC_BUILD_UI "Builds IconComposerKit and the app, which pull the OnyxSDK" ON)
set(IC_ONYX_SOURCE_DIR "D:/CodingProjects/OnyxSDK"
    CACHE PATH "Local OnyxSDK checkout; empty to fetch the pinned SHA from GitHub")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(IC_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# The towers, in topological order -- see Docs/Specs/2026-08-31-arquitetura-design.md.
# Each is named after the image of the target it answers for, and each is a clean
# reimplementation of what that image does, never a copy of it.
add_subdirectory(Source/IconComposerFoundation)
add_subdirectory(Source/CoreSVG)
add_subdirectory(Source/RenderBox)
add_subdirectory(Source/cli)

if(IC_BUILD_UI)
    # The toolkit, the way sfsymview takes it: source consumption, EXCLUDE_FROM_ALL
    # so a plain build only compiles what the app links, SYSTEM so Onyx's headers
    # are not held to this tree's warnings. Local checkout first; the SHA when the
    # path is empty -- an exact SHA, never a branch.
    include(FetchContent)
    set(ONYX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ONYX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    # The video player is the one switch whose name does not follow the others:
    # ONYX_COMPONENT_MEDIA, not ONYX_BUILD_MEDIA. Measured on dddce38 -- with the
    # wrong name the option is simply never read, and configure spends its time
    # downloading a prebuilt FFmpeg this editor will never link.
    set(ONYX_COMPONENT_MEDIA OFF CACHE BOOL "" FORCE)
    if(IC_ONYX_SOURCE_DIR AND EXISTS "${IC_ONYX_SOURCE_DIR}/CMakeLists.txt")
        FetchContent_Declare(OnyxSDK SOURCE_DIR "${IC_ONYX_SOURCE_DIR}" EXCLUDE_FROM_ALL SYSTEM)
    else()
        FetchContent_Declare(OnyxSDK
            GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
            GIT_TAG        dddce38
            EXCLUDE_FROM_ALL SYSTEM)
    endif()
    # Onyx's own pins (GLFW 3.3.9, GLM, lz4) declare cmake_minimum_required below
    # 3.5, which CMake 4 refuses; sfsymview records the same escape hatch.
    set(_ic_saved_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    # SYSTEM spares Onyx's HEADERS from this tree's warnings, but its own sources
    # still inherit the directory's compile options: measured, glfw and imgui
    # answer -Wall -Wextra -Wpedantic with ~700 lines of noise that no one here
    # can act on. Strip the warning flags for the duration of the subdirectory --
    # and only those, so IC_SANITIZE keeps reaching Onyx's code.
    get_directory_property(_ic_saved_opts COMPILE_OPTIONS)
    set(_ic_onyx_opts "${_ic_saved_opts}")
    list(REMOVE_ITEM _ic_onyx_opts -Wall -Wextra -Wpedantic /W4 /permissive-)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_ic_onyx_opts}")
    FetchContent_MakeAvailable(OnyxSDK)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_ic_saved_opts}")
    if(_ic_saved_policy_min)
        set(CMAKE_POLICY_VERSION_MINIMUM "${_ic_saved_policy_min}" CACHE STRING "" FORCE)
    else()
        unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
    endif()
    add_subdirectory(Source/IconComposerKit)
    add_subdirectory(Source/app)
endif()

if(IC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
