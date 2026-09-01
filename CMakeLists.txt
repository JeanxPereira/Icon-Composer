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
add_subdirectory(Source/cli)

if(IC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
