# RenderBox -- the GPU tower.
#
# Vulkan, and headless: no surface extension, no swapchain, no window. Rule 2 of
# the architecture spec keeps ONYX out of everything a test links, and it says
# nothing about Vulkan -- a tower may link the API without linking the toolkit.
# Rule 1 is untouched: IconComposerFoundation still has no GPU in it.
find_package(Vulkan REQUIRED)

add_library(RenderBox STATIC
    Device.cpp
    Buffer.cpp
    PathBuffer.cpp
)

target_include_directories(RenderBox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# CoreSVG, because a path buffer is built FROM a parsed path. Still no UI and
# no Onyx anywhere in this tower.
target_link_libraries(RenderBox PUBLIC Vulkan::Vulkan IconComposer::CoreSVG)

add_library(IconComposer::RenderBox ALIAS RenderBox)
