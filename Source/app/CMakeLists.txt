# The executable, and the only target in this tree that opens a window.
#
# Rule 2 of the architecture spec (spec 13/09 §3): the window, the dock layout,
# the texture pool, the file dialogs and the job queue live here and nowhere
# else. The Kit below it is ImGui and nothing else, which is what keeps
# `ic_tests` runnable on a machine with no device.
#
# Desde 30/09 tudo isto e nosso (Shell, NativeWindow, TexturePool, JobQueue,
# Dialogs), no lugar do OnyxSDK: so GLFW e os dois backends do ImGui.
add_executable(iconcomposer
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
target_link_libraries(iconcomposer PRIVATE IconComposer::Kit imgui_backends)

# A CASCA, uma por sistema (Shell.h): no Windows a janela Win32 com o
# DirectComposition (o canto e a sombra do macOS 27); nos outros a GLFW.
if(WIN32)
    target_sources(iconcomposer PRIVATE ShellWin32.cpp)
    target_link_libraries(iconcomposer PRIVATE d3d11 dxgi dcomp)
else()
    target_sources(iconcomposer PRIVATE ShellGlfw.cpp)
    target_compile_definitions(iconcomposer PRIVATE IC_SHELL_GLFW)
endif()

# `dwmapi` e a moldura (sombra, cantos, modo escuro); `ole32` e `uuid` sao o
# IFileDialog -- `uuid` carrega CLSID_FileOpenDialog e IID_IFileOpenDialog
# como simbolos, o que deixa chama-los sem `__uuidof`.
if(WIN32)
    target_link_libraries(iconcomposer PRIVATE dwmapi ole32 uuid)
endif()
target_include_directories(iconcomposer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# O RUNTIME DO MINGW DENTRO DO EXE. Sem isto o `iconcomposer.exe` carrega a
# `libstdc++-6.dll` que estiver primeiro no PATH, e medido 30/09: lancado de
# outro shell ele achou uma de outra toolchain e parou antes da janela, num
# "ponto de entrada nao encontrado" (`__codecvt_utf8_utf16`). Estatico, o exe
# abre com dois cliques de qualquer lugar.
if(MINGW)
    target_link_options(iconcomposer PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
