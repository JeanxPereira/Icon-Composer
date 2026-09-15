# IconComposerKit -- the editor's UI, in Dear ImGui and nothing else.
#
# No window, no Vulkan surface, no Onyx: this is what `ic_tests` and the selftest
# link, and rule 2 of the architecture spec is what keeps it that way. The two
# things a window provides -- a texture upload and a render thread -- arrive
# through Ports.h, implemented in Source/app.
add_library(IconComposerKit STATIC
    Headless.cpp
    Ports.cpp
    PanelLayers.cpp
    RenderCoordinator.cpp
    MenuBar.cpp
    PanelCanvas.cpp
    Session.cpp
    ViewModel.cpp
)
target_link_libraries(IconComposerKit
    PUBLIC IconComposer::Foundation IconComposer::CoreSVG IconComposer::RenderBox imgui_lib)
target_include_directories(IconComposerKit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_library(IconComposer::Kit ALIAS IconComposerKit)
