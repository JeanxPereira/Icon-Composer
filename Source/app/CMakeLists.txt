# The executable, and the only target in this tree that opens a window.
#
# Rule 2 of the architecture spec (spec 13/09 §3): the window, the dock layout,
# the texture pool, the file dialogs and the job queue live here and nowhere
# else. The Kit below it is ImGui and nothing else, which is what keeps
# `ic_tests` runnable on a machine with no device.
#
# Desde 30/09 tudo isto e nosso (Shell, NativeWindow, TexturePool, JobQueue,
# Dialogs), no lugar do OnyxSDK: so GLFW e os dois backends do ImGui.
add_executable(IconComposer
    main.cpp
    AppPorts.cpp
    Dialogs.cpp
    JobQueue.cpp
    NativeWindow.cpp
    Symbols.cpp
    TexturePool.cpp
    TrafficLights.cpp
    Window.cpp
)
target_link_libraries(IconComposer PRIVATE IconComposer::Kit imgui_backends)

# A CASCA, uma por sistema (Shell.h): no Windows a janela Win32 com o
# DirectComposition (o canto e a sombra do macOS 27); nos outros a GLFW.
if(WIN32)
    target_sources(IconComposer PRIVATE ShellWin32.cpp)
    target_link_libraries(IconComposer PRIVATE d3d11 dxgi dcomp)
else()
    target_sources(IconComposer PRIVATE ShellGlfw.cpp)
    target_compile_definitions(IconComposer PRIVATE IC_SHELL_GLFW)
endif()

# `dwmapi` e a moldura (sombra, cantos, modo escuro); `ole32` e `uuid` sao o
# IFileDialog -- `uuid` carrega CLSID_FileOpenDialog e IID_IFileOpenDialog
# como simbolos, o que deixa chama-los sem `__uuidof`.
if(WIN32)
    target_link_libraries(IconComposer PRIVATE dwmapi ole32 uuid)
endif()
target_include_directories(IconComposer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# O RUNTIME DO MINGW DENTRO DO EXE, a versao e o `install`: os tres vem de
# `ic_release_executable` (CMakeLists.txt da raiz), que e o que faz o exe abrir
# com dois cliques de qualquer lugar.
ic_release_executable(IconComposer)

# O icone da janela e do executavel (`Resources/IconComposer.rc`).
if(WIN32)
    target_sources(IconComposer PRIVATE Resources/IconComposer.rc)
endif()
