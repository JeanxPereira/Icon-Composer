# RenderBox -- the GPU tower.
#
# Vulkan, and headless: no surface extension, no swapchain, no window. Rule 2 of
# the architecture spec keeps ONYX out of everything a test links, and it says
# nothing about Vulkan -- a tower may link the API without linking the toolkit.
# Rule 1 is untouched: IconComposerFoundation still has no GPU in it.
#
# One nuance rule 2 now has to state out loud: in a build with the UI this tower
# links `volk::volk`, and volk arrives through Onyx's FetchContent. volk is a
# 500-line loader, not the toolkit -- `ic_tests` still links no window, no GLFW
# and no Onyx target, which is what rule 2 is actually protecting. See the block
# at the foot of this file for why it has to be volk there and not the loader's
# own prototypes.
find_package(Vulkan REQUIRED COMPONENTS glslc)

add_library(RenderBox STATIC
    VulkanApi.cpp
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
    SvgRenderer.cpp
    SvgFilter.cpp
    SvgPattern.cpp
    BlurKernel.cpp
    ChicletHighlights.cpp
    ChicletShape.cpp
    IconRenderer.cpp
    Mono.cpp
    SimulatedGlass.cpp
    IconRendererGpu.cpp
    GpuResident.cpp
    GpuGlass.cpp
    RenderCache.cpp
    RenderingParameters.cpp
    ViewportPlan.cpp
    GradientOracle.cpp
    AutomaticGradient.cpp
    ColorSpace.cpp
    GlassOracle.cpp
    DisplacementOracle.cpp
    DistanceField.cpp
    MipPyramid.cpp
    GlassBackground.cpp
    GlassForeground.cpp
    SdfDisplacement.cpp
    GlassMaterial.cpp
    GlassTranslucency.cpp
    GlassShadow.cpp
    GlassSpecular.cpp
    GlassGlow.cpp
    GlassLayer.cpp
    FillResolve.cpp
    SystemFill.cpp
    BlendMode.cpp
    BlendFormula.cpp
    StrokeGeometry.cpp
    StrokeRender.cpp
)

# Shaders are compiled at BUILD time, not loaded at run time. A shader that does
# not compile has to be a build failure -- as a runtime load it becomes a test
# that fails for a reason unrelated to what it was testing, and the message says
# "could not create module" rather than the glslc diagnostic that explains why.
set(_shader_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${_shader_dir})
file(GLOB _glsl_deps ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl)
set(_shader_incs)
foreach(shader IN ITEMS path_probe.comp path_coverage.comp path_exterior.vert path_exterior.frag gradient_probe.comp glass_probe.comp displacement_probe.comp field_probe.comp mip_reduce.comp glass_background_probe.comp glass_foreground_probe.comp sdf_displacement_probe.comp icon_paint.comp icon_chiclet.comp icon_blend.comp icon_svg.comp icon_finish.comp icon_raster.comp icon_field.comp icon_ring.comp icon_shadow.comp icon_blur.comp icon_glass_mask.comp icon_displace.comp icon_refract.comp icon_highlight.comp icon_stroke.comp icon_group.comp icon_field_stack.comp icon_glow.comp icon_tint.comp)
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
# The document model too, now that the compositor walks groups and layers.
target_link_libraries(RenderBox PUBLIC IconComposer::CoreSVG IconComposer::Foundation)

# HOW THIS TOWER GETS ITS VULKAN, and why the answer has two shapes.
#
# When the UI is in the build, so is Onyx, and so is its volk -- compiled with
# VK_NO_PROTOTYPES PUBLIC, which turns every Vulkan entry point into a VARIABLE
# in volk's own object file. A tower that also referenced those names as
# FUNCTIONS, out of the loader's import library, gave the linker the same 62
# names twice in two incompatible kinds: measured, and the reason `iconcomposer`
# had no executable. So in that build this tower goes through volk as well --
# but through a table of its OWN device (VulkanApi.h says why the global one
# would send our work to Onyx's device).
#
# When the UI is out -- `icrender`, and `ic_tests` on a machine with no toolkit
# -- there is no volk to collide with and the loader's own prototypes are the
# shortest path. Both shapes compile the same call sites.
#
# A GENERATOR EXPRESSION, and `if(TARGET volk::volk)` would be a silent bug.
# The root adds Onyx AFTER this directory, so at the moment this file is read the
# target does not exist and a plain `if(TARGET ...)` is false every time -- it
# would compile the no-volk path into a build that has volk, with no warning and
# a link that fails 62 times. `$<TARGET_EXISTS:...>` is resolved at GENERATE
# time, once the whole tree has configured, so it answers the question that was
# actually asked. It also keeps the root free of a reordering.
set(_rb_volk $<TARGET_EXISTS:volk::volk>)
target_compile_definitions(RenderBox PUBLIC $<${_rb_volk}:IC_RB_USE_VOLK>)
target_link_libraries(RenderBox PUBLIC
    $<${_rb_volk}:volk::volk>
    $<$<NOT:${_rb_volk}>:Vulkan::Vulkan>)

add_library(IconComposer::RenderBox ALIAS RenderBox)
