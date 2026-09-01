# RenderBox -- the GPU tower.
#
# Vulkan, and headless: no surface extension, no swapchain, no window. Rule 2 of
# the architecture spec keeps ONYX out of everything a test links, and it says
# nothing about Vulkan -- a tower may link the API without linking the toolkit.
# Rule 1 is untouched: IconComposerFoundation still has no GPU in it.
find_package(Vulkan REQUIRED COMPONENTS glslc)

add_library(RenderBox STATIC
    Device.cpp
    Buffer.cpp
    PathBuffer.cpp
    PathVertexOracle.cpp
    Compute.cpp
    PathCoverageOracle.cpp
    PathResolveOracle.cpp
    PathCompositeOracle.cpp
    Image.cpp
    CoveragePass.cpp
)

# Shaders are compiled at BUILD time, not loaded at run time. A shader that does
# not compile has to be a build failure -- as a runtime load it becomes a test
# that fails for a reason unrelated to what it was testing, and the message says
# "could not create module" rather than the glslc diagnostic that explains why.
set(_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_shader_dir})
file(GLOB _glsl_deps ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl)
set(_shader_incs)
foreach(shader IN ITEMS path_probe.comp path_coverage.comp path_exterior.vert path_exterior.frag)
    set(_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
    set(_out ${_shader_dir}/${shader}.inc)
    add_custom_command(OUTPUT ${_out}
        COMMAND Vulkan::glslc --target-env=vulkan1.3
                -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                -mfmt=c -o ${_out} ${_src}
        DEPENDS ${_src} ${_glsl_deps}
        COMMENT "glslc ${shader}" VERBATIM)
    list(APPEND _shader_incs ${_out})
    target_sources(RenderBox PRIVATE shaders/${shader})
    set_source_files_properties(shaders/${shader} PROPERTIES HEADER_FILE_ONLY TRUE)
endforeach()
target_sources(RenderBox PRIVATE ${_shader_incs} ${_glsl_deps})
set_source_files_properties(${_glsl_deps} PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(RenderBox PUBLIC ${_shader_dir})

target_include_directories(RenderBox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# CoreSVG, because a path buffer is built FROM a parsed path. Still no UI and
# no Onyx anywhere in this tower.
target_link_libraries(RenderBox PUBLIC Vulkan::Vulkan IconComposer::CoreSVG)

add_library(IconComposer::RenderBox ALIAS RenderBox)
