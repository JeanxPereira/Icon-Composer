add_executable(ic_tests
    main.cpp
    test_json.cpp
    test_corpus.cpp
    test_document.cpp
    test_values.cpp
    test_svg_path.cpp
    test_svg_corpus.cpp
    test_svg_xml.cpp
    test_svg_document.cpp
    test_svg_paint.cpp
    test_svg_css.cpp
    test_bundle.cpp
    test_cli.cpp
)

target_link_libraries(ic_tests PRIVATE IconComposer::Foundation IconComposer::CliLib IconComposer::CoreSVG)
target_include_directories(ic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME ic_tests COMMAND ic_tests)
