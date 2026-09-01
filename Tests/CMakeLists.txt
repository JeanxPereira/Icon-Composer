add_executable(ic_tests
    main.cpp
    test_json.cpp
    test_corpus.cpp
    test_document.cpp
    test_values.cpp
    test_bundle.cpp
    test_cli.cpp
)

target_link_libraries(ic_tests PRIVATE IconComposer::Foundation IconComposer::CliLib)
target_include_directories(ic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME ic_tests COMMAND ic_tests)
