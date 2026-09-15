#include "Source/RenderBox/VulkanApi.h"

namespace rb {

#if defined(IC_RB_USE_VOLK)

VkResult initialiseLoader() {
    // Idempotent: Onyx calls it too, and a second call re-resolves the same
    // module handle rather than loading a second loader.
    return volkInitialize();
}

void loadInstanceApi(VkInstance instance) { volkLoadInstanceOnly(instance); }

void loadDeviceApi(DeviceApi& table, VkDevice device) { volkLoadDeviceTable(&table, device); }

#else

VkResult initialiseLoader() { return VK_SUCCESS; }

void loadInstanceApi(VkInstance) {}

void loadDeviceApi(DeviceApi& table, VkDevice) {
    // No volk, so there is exactly one device's worth of dispatch in the process
    // and the loader's own prototypes are it. The table still exists so that the
    // call sites do not have to know which build they are in.
#  define IC_RB_ASSIGN_PFN(name) table.name = ::name;
    IC_RB_DEVICE_FUNCTIONS(IC_RB_ASSIGN_PFN)
#  undef IC_RB_ASSIGN_PFN
}

#endif

}  // namespace rb
