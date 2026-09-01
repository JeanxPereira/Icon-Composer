# CoreSVG -- reading the SVG a layer points at.
#
# Like IconComposerFoundation: no GPU, no UI, stdlib only. See the architecture
# spec, rule 1.
add_library(CoreSVG STATIC
    Path.cpp
    Xml.cpp
    Document.cpp
    Paint.cpp
)

target_include_directories(CoreSVG PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(IconComposer::CoreSVG ALIAS CoreSVG)
