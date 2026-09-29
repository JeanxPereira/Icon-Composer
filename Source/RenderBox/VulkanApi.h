#pragma once
// How this tower reaches Vulkan -- and why it does not reach it by name.
//
// THE COLLISION
// -------------
// The app links `imgui_lib`, and `imgui_lib` links `volk::volk` from the Onyx
// SDK. Onyx compiles volk with `VK_NO_PROTOTYPES` as a PUBLIC definition, and in
// that mode `volk.c` defines every Vulkan entry point as a VARIABLE -- a function
// pointer in `.bss`. Measured on the Onyx checkout: 657 of them. This tower used
// to include `<vulkan/vulkan.h>` without that define, so it referenced the same
// names as FUNCTIONS and pulled them out of the loader's import library. Same
// name, incompatible kind of symbol: 62 `multiple definition` errors, and no
// executable.
//
// WHY "RENDERBOX SHOULD JUST USE VOLK TOO" IS THE WRONG FIX
// ---------------------------------------------------------
// Onyx calls `volkLoadDevice(m_device)` (Source/Rendering/VkContext.cpp:362).
// That rewrites volk's GLOBAL dispatch with pointers specialised to ONYX's
// VkDevice. This tower creates its own device -- spec 13/09 §6 says two
// VkInstances in one process on purpose -- so every device-level call made
// through the global table would be routed to Onyx's device instead of ours.
// That does not crash; it corrupts quietly, which is worse.
//
// WHAT THIS HEADER DOES INSTEAD
// -----------------------------
// A dispatch table PER DEVICE, which is volk's own documented answer to several
// devices in one process. `rb::Device` owns a `DeviceApi` loaded from ITS device,
// and the 56 device-level entry points this tower calls go through that table and
// never through a global symbol. The handful of global and instance-level calls
// (create an instance, enumerate adapters, create a device) stay on the loader's
// trampolines, which dispatch on the handle they are given and so are indifferent
// to which instance loaded them.
//
// AND WHEN VOLK IS NOT IN THE BUILD
// ---------------------------------
// `icrender` and `ic_tests` link this tower with no UI and no Onyx, so there is
// no volk. `DeviceApi` is then a struct with the same member names, filled from
// the loader's own prototypes. Every call site reads identically in both modes --
// `device.api().vkCreateBuffer(...)` -- so there is one body of code, not two.
#if defined(IC_RB_USE_VOLK)
#  include <volk.h>
#else
#  include <vulkan/vulkan.h>
#endif

namespace rb {

// The device-level entry points this tower calls, and the only list of them.
// The struct, the loader and (in volk's absence) the assignments are all
// generated from this one place, so a call site that needs a new function fails
// to compile until the name is added here -- which is the point.
#define IC_RB_DEVICE_FUNCTIONS(X)   \
    X(vkAllocateCommandBuffers)     \
    X(vkAllocateDescriptorSets)     \
    X(vkAllocateMemory)             \
    X(vkBeginCommandBuffer)         \
    X(vkBindBufferMemory)           \
    X(vkBindImageMemory)            \
    X(vkCmdBeginRenderPass)         \
    X(vkCmdBindDescriptorSets)      \
    X(vkCmdBindPipeline)            \
    X(vkCmdCopyBuffer)              \
    X(vkCmdCopyImageToBuffer)       \
    X(vkCmdDispatch)                \
    X(vkCmdDraw)                    \
    X(vkCmdEndRenderPass)           \
    X(vkCmdFillBuffer)              \
    X(vkCmdPipelineBarrier)         \
    X(vkCmdPushConstants)           \
    X(vkCmdSetScissor)              \
    X(vkCmdSetViewport)             \
    X(vkCreateBuffer)               \
    X(vkCreateCommandPool)          \
    X(vkCreateComputePipelines)     \
    X(vkCreateDescriptorPool)       \
    X(vkCreateDescriptorSetLayout)  \
    X(vkCreateFence)                \
    X(vkCreateFramebuffer)          \
    X(vkCreateGraphicsPipelines)    \
    X(vkCreateImage)                \
    X(vkCreateImageView)            \
    X(vkCreatePipelineLayout)       \
    X(vkCreateRenderPass)           \
    X(vkCreateShaderModule)         \
    X(vkDestroyBuffer)              \
    X(vkDestroyCommandPool)         \
    X(vkDestroyDescriptorPool)      \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyDevice)              \
    X(vkDestroyFence)               \
    X(vkDestroyFramebuffer)         \
    X(vkDestroyImage)               \
    X(vkDestroyImageView)           \
    X(vkDestroyPipeline)            \
    X(vkDestroyPipelineLayout)      \
    X(vkDestroyRenderPass)          \
    X(vkDestroyShaderModule)        \
    X(vkDeviceWaitIdle)             \
    X(vkEndCommandBuffer)           \
    X(vkFreeCommandBuffers)         \
    X(vkFreeMemory)                 \
    X(vkGetBufferMemoryRequirements)\
    X(vkGetDeviceQueue)             \
    X(vkGetImageMemoryRequirements) \
    X(vkMapMemory)                  \
    X(vkQueueSubmit)                \
    X(vkResetDescriptorPool)        \
    X(vkUnmapMemory)                \
    X(vkUpdateDescriptorSets)       \
    X(vkWaitForFences)

#if defined(IC_RB_USE_VOLK)
// volk's own per-device table. Its members are spelled exactly like the entry
// points, which is why the substitute below can be spelled the same way.
using DeviceApi = ::VolkDeviceTable;
#else
struct DeviceApi {
#  define IC_RB_DECLARE_PFN(name) PFN_##name name = nullptr;
    IC_RB_DEVICE_FUNCTIONS(IC_RB_DECLARE_PFN)
#  undef IC_RB_DECLARE_PFN
};
#endif

// Brings the loader up. Must run before ANY Vulkan call: with volk in the build
// even `vkEnumerateInstanceLayerProperties` is a null pointer until it does.
// Without volk there is nothing to do and the answer is VK_SUCCESS.
VkResult initialiseLoader();

// Loads the global and instance-level entry points for `instance`.
//
// `volkLoadInstanceOnly`, not `volkLoadInstance`: the latter also fills the
// global DEVICE-level slots, which would overwrite the ones Onyx specialised for
// its own device with generic trampolines. Slower for Onyx and no use to us --
// we read our device's functions out of our own table.
void loadInstanceApi(VkInstance instance);

// Fills `table` with the entry points of `device`, and of no other device.
void loadDeviceApi(DeviceApi& table, VkDevice device);

}  // namespace rb
