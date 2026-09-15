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
    test_fill_resolve.cpp
    test_system_fill.cpp
    test_blend_mode.cpp
    test_blend_formula.cpp
    test_stroke_geometry.cpp
    test_stroke_render.cpp
    test_automatic_fill.cpp
)

target_link_libraries(ic_tests PRIVATE IconComposer::Foundation IconComposer::CliLib IconComposer::CoreSVG IconComposer::RenderBox)
target_include_directories(ic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME ic_tests COMMAND ic_tests)
