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
    ShellIntegration.cpp
    Stage.cpp
    StageCompositor.cpp
    Symbols.cpp
    TexturePool.cpp
    TrafficLights.cpp
    Window.cpp
)
target_link_libraries(IconComposer PRIVATE IconComposer::Kit imgui_backends)

# OS SHADERS DA TELA (StageCompositor.h), compilados no build pela mesma regra
# da RenderBox: um shader que nao compila e uma falha de build, com o
# diagnostico do glslc. `stage.frag` inclui a transcricao da refracao da
# RenderBox (`Displacement.glsl`), entao a pasta dela entra no caminho.
find_package(Vulkan REQUIRED COMPONENTS glslc)
set(_stage_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_stage_shader_dir})
set(_rb_shaders ${CMAKE_CURRENT_SOURCE_DIR}/../RenderBox/shaders)
foreach(shader IN ITEMS stage.vert stage.frag)
    set(_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
    set(_out ${_stage_shader_dir}/${shader}.inc)
    add_custom_command(OUTPUT ${_out}
        COMMAND Vulkan::glslc --target-env=vulkan1.3 -I ${_rb_shaders} -mfmt=c -o ${_out} ${_src}
        DEPENDS ${_src} ${_rb_shaders}/Displacement.glsl
        COMMENT "glslc ${shader}" VERBATIM)
    target_sources(IconComposer PRIVATE ${_out} shaders/${shader})
    set_source_files_properties(shaders/${shader} PROPERTIES HEADER_FILE_ONLY TRUE)
endforeach()
target_include_directories(IconComposer PRIVATE ${_stage_shader_dir})

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
# como simbolos, o que deixa chama-los sem `__uuidof`. `windowscodecs` e o WIC,
# que le os JPEG de fundo do palco (Stage.cpp).
if(WIN32)
    target_link_libraries(IconComposer PRIVATE dwmapi ole32 uuid windowscodecs shell32 advapi32)
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
