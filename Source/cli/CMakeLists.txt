# The report is a library; the tool is a thin main over it.
#
# ic_cli_lib is what the tests link: a pure function from a bundle to text, with
# no process, no argv and no terminal in the way. The executable adds those and
# nothing else. This is the sfsymview_lib split, and the reason is the same --
# what is worth testing should not require a process to exercise.
add_library(ic_cli_lib STATIC Report.cpp)
target_link_libraries(ic_cli_lib PUBLIC IconComposer::Foundation)
add_library(IconComposer::CliLib ALIAS ic_cli_lib)

add_executable(ictool main.cpp)
target_link_libraries(ictool PRIVATE IconComposer::CliLib)

# icrender is a SECOND executable on purpose: it links the GPU tower, and
# `ictool` must keep running on a machine that has no device. The target ships
# `ictool` and `icrtool` for the same reason.
#
# E O CAMINHO DE BUNDLE DELE E UMA BIBLIOTECA, pela mesma razao que `ic_cli_lib`
# e uma: o que decide os BYTES nao pode exigir um processo para ser exercido.
# `Tests/test_e2e_export.cpp` linka esta e compara a UI contra ela, que e o que
# faz a promessa de `Export.h` ("byte a byte contra a sequencia do `icrender`")
# ser verdade em vez de uma transcricao (revisao 19/09, I6).
add_library(ic_render_lib STATIC RenderBundle.cpp)
target_link_libraries(ic_render_lib PUBLIC IconComposer::RenderBox IconComposer::Foundation)
add_library(IconComposer::RenderCliLib ALIAS ic_render_lib)

add_executable(icrender render_main.cpp)
target_link_libraries(icrender PRIVATE IconComposer::RenderCliLib)

# icserver e o nucleo como processo persistente para a casca Tauri (ui/): abre
# o dispositivo uma vez e guarda o RenderCache entre quadros. Protocolo no topo
# de server_main.cpp.
add_executable(icserver server_main.cpp)
target_link_libraries(icserver PRIVATE IconComposer::RenderCliLib)
