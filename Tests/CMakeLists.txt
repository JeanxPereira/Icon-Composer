# EXCLUDE_FROM_ALL: a suite nao entra no build default -- ela linka 40 MB e o loop
# de edicao paga isso por nada. Construa sob demanda:
#   cmake --build --preset mingw --target ic_tests
add_executable(ic_tests EXCLUDE_FROM_ALL
    main.cpp
    test_json.cpp
    test_json_mutate.cpp
    test_corpus.cpp
    test_document.cpp
    test_document_edit.cpp
    test_values.cpp
    test_values_tojson.cpp
    test_svg_path.cpp
    test_svg_corpus.cpp
    test_svg_xml.cpp
    test_svg_document.cpp
    test_svg_paint.cpp
    test_svg_css.cpp
    test_bundle.cpp
    test_bundle_save.cpp
    test_cli.cpp
    test_rb_device.cpp
    test_rb_pathbuffer.cpp
    test_rb_pathvertex.cpp
    test_rb_coverage.cpp
    test_rb_render.cpp
    test_render_svg.cpp
    test_svg_filter.cpp
    test_svg_pattern.cpp
    test_png.cpp
    test_icon_render.cpp
    test_viewport_render.cpp
    test_rb_gradient.cpp
    test_auto_gradient.cpp
    test_rb_glass.cpp
    test_rb_displacement.cpp
    test_rb_field.cpp
    test_rb_mip.cpp
    test_rb_glass_background.cpp
    test_rb_glass_foreground.cpp
    test_rb_sdf_displacement.cpp
    test_glass_material.cpp
    test_glass_layer.cpp
    test_glass_shadow.cpp
    test_blur_material.cpp
    test_fill_resolve.cpp
    test_system_fill.cpp
    test_blend_mode.cpp
    test_blend_formula.cpp
    test_stroke_geometry.cpp
    test_stroke_render.cpp
    test_automatic_fill.cpp
    test_chiclet_highlights.cpp
    test_chiclet_shape.cpp
    test_time_budget.cpp
)

target_link_libraries(ic_tests PRIVATE IconComposer::Foundation IconComposer::CliLib IconComposer::RenderCliLib IconComposer::CoreSVG IconComposer::RenderBox)
target_include_directories(ic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The UI's own cases. `-DIC_BUILD_UI=OFF` builds no Kit, and a suite that failed
# to configure there would be a worse gate than one that runs 26 cases fewer --
# so the Kit's cases are added only when the Kit exists, and the count printed at
# the end of a run is the honest tally for the build that produced it.
#
# THE 26 IS A HOMONYM AND IT HAS BURNED THIS PROJECT TWICE. `679` with the Kit
# and `653` without it differ by the 26 cases these four files carry. A run with
# no `IC_CORPUS_DIR` ALSO says 26 -- but as 26 FAILURES (`dir != nullptr`,
# `!files.empty()`), not 26 absences. Same number, opposite meaning: one is a
# build that has fewer cases, the other is a build whose cases cannot find the
# corpus. Read which of the two the line says before calling anything a
# regression.
if(TARGET IconComposerKit)
    target_sources(ic_tests PRIVATE test_kit_idiom.cpp test_kit_canvas.cpp test_kit_layers.cpp test_kit_inspector.cpp
                                    test_e2e_inspector.cpp test_e2e_layers.cpp
                                    test_e2e_renditions.cpp
                                    test_e2e_import_save_duplicate.cpp
                                    test_e2e_export.cpp)
    target_link_libraries(ic_tests PRIVATE IconComposer::Kit)
endif()

add_test(NAME ic_tests COMMAND ic_tests)
