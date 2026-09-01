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
add_executable(icrender render_main.cpp)
target_link_libraries(icrender PRIVATE IconComposer::RenderBox IconComposer::Foundation)
