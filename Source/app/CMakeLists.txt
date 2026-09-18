# The executable, and the only target in this tree that links Onyx.
#
# Rule 2 of the architecture spec (spec 13/09 §3): the window, the registrar,
# the dock layout, the TexturePool, the file dialogs and the JobQueue live
# here and nowhere else. The Kit below it is ImGui and nothing else, which is
# what keeps `ic_tests` runnable on a machine with no device.
add_executable(iconcomposer
    main.cpp
    OnyxPorts.cpp
    Window.cpp
)
target_link_libraries(iconcomposer PRIVATE IconComposer::Kit Onyx::Onyx)

# The open dialog is IFileOpenDialog, because only a folder picker can name a
# `.icon` (OnyxPorts.h). `ole32` is CoCreateInstance and `uuid` carries
# CLSID_FileOpenDialog and IID_IFileOpenDialog as symbols, which is what lets
# the call site name them without `__uuidof`.
if(WIN32)
    target_link_libraries(iconcomposer PRIVATE ole32 uuid)
endif()
target_include_directories(iconcomposer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Onyx resolves its bundled fonts against the EXECUTABLE's directory
# (Onyx/Services/PathUtils.h `getExecutableDir`, read on the dddce38 checkout),
# not against the working directory, so they have to sit next to this binary --
# the same POST_BUILD copy Onyx's own MinimalViewer example does. Without it
# the window still opens, but with the ImGui default font and no SF Symbols.
if(ONYX_FONTS_DIR AND EXISTS "${ONYX_FONTS_DIR}")
    add_custom_command(TARGET iconcomposer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ONYX_FONTS_DIR}" "$<TARGET_FILE_DIR:iconcomposer>/third_party/fonts"
        COMMENT "Copying Onyx's bundled fonts next to iconcomposer")
endif()
