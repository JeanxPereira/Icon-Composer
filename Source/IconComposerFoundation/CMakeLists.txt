# IconComposerFoundation -- the .icon document model.
#
# This tower links NO GPU and NO UI, and it must build without Vulkan. It is the
# layer the tests hammer; anything that drags a window or a device into it puts
# that on the critical path of every run. See the architecture spec, rule 1.
add_library(IconComposerFoundation STATIC
    Json.cpp
    IconDocument.cpp
    Coverage.cpp
    Values.cpp
    IconBundle.cpp
    Png.cpp
)

target_include_directories(IconComposerFoundation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

add_library(IconComposer::Foundation ALIAS IconComposerFoundation)
