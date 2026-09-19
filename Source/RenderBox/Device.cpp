#include "Source/RenderBox/Device.h"

#include <cstring>
#include <vector>

namespace rb {
namespace {

std::string fail(const char* what, VkResult r) {
    return std::string(what) + ": " + describe(r);
}

bool hasLayer(const char* wanted) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, wanted) == 0) return true;
    }
    return false;
}

// Prefers a discrete GPU, then anything else. Not a performance choice: on a
// machine with both, the integrated adapter and the discrete one round differently
// in the last bit, and a render that changes depending on which one CMake happened
// to enumerate first is a render nobody can diff.
int score(const VkPhysicalDeviceProperties& p) {
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
        default: return 0;
    }
}

}  // namespace

const char* describe(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        default: return "VkResult(unlisted)";
    }
}

Result<Device> Device::create(DeviceOptions options) {
    // Before ANY Vulkan call, and `hasLayer` below is one: with volk in the build
    // every entry point -- `vkEnumerateInstanceLayerProperties` included -- is a
    // null function pointer until the loader has been brought up.
    if (VkResult r = initialiseLoader(); r != VK_SUCCESS) {
        return std::unexpected(fail("the Vulkan loader could not be initialised", r));
    }

    Device d;
    d.api_ = std::make_unique<DeviceApi>();

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "IconStudio";
    app.apiVersion = VK_API_VERSION_1_3;

    const char* layer = "VK_LAYER_KHRONOS_validation";
    const bool useValidation = options.validation && hasLayer(layer);

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    // No extensions at all: headless needs no surface, and asking for one that a
    // driver lacks turns "no window" into "no device".
    if (useValidation) {
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &layer;
    }
    if (VkResult r = vkCreateInstance(&ici, nullptr, &d.instance_); r != VK_SUCCESS) {
        return std::unexpected(fail("vkCreateInstance", r));
    }
    // Instance-level entry points, for the enumeration below. These land in the
    // loader's global slots and that is safe: they are trampolines that dispatch
    // on the handle they are passed, so Onyx reading them and this tower reading
    // them are the same read.
    loadInstanceApi(d.instance_);

    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(d.instance_, &count, nullptr);
    if (count == 0) {
        return std::unexpected(std::string("no Vulkan physical device is present"));
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(d.instance_, &count, devices.data());

    int best = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);

        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> qf(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, qf.data());

        // Graphics implies transfer, and this tower needs both: it rasterises and
        // it reads back.
        for (std::uint32_t i = 0; i < families; ++i) {
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            if (score(props) > best) {
                best = score(props);
                d.physical_ = candidate;
                d.queueFamily_ = i;
                d.name_ = props.deviceName;
                d.limits_ = props.limits;   // imutaveis: lidas aqui, nunca de novo
            }
            break;
        }
    }
    if (d.physical_ == VK_NULL_HANDLE) {
        return std::unexpected(std::string("no physical device has a graphics queue family"));
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = d.queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (VkResult r = vkCreateDevice(d.physical_, &dci, nullptr, &d.device_); r != VK_SUCCESS) {
        return std::unexpected(fail("vkCreateDevice", r));
    }
    // From here on nothing in this tower touches a global Vulkan symbol: the
    // device's own dispatch is loaded once, and every device-level call goes
    // through it.
    loadDeviceApi(*d.api_, d.device_);
    d.api_->vkGetDeviceQueue(d.device_, d.queueFamily_, 0, &d.queue_);

    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = d.queueFamily_;
    if (VkResult r = d.api_->vkCreateCommandPool(d.device_, &pci, nullptr, &d.pool_);
        r != VK_SUCCESS) {
        return std::unexpected(fail("vkCreateCommandPool", r));
    }
    return d;
}

void Device::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        api_->vkDeviceWaitIdle(device_);
        if (pool_ != VK_NULL_HANDLE) api_->vkDestroyCommandPool(device_, pool_, nullptr);
        api_->vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
}

Device::~Device() { destroy(); }

Device::Device(Device&& other) noexcept
    : api_(std::move(other.api_)),
      instance_(other.instance_), physical_(other.physical_), device_(other.device_),
      queue_(other.queue_), pool_(other.pool_), queueFamily_(other.queueFamily_),
      name_(std::move(other.name_)), limits_(other.limits_) {
    other.instance_ = VK_NULL_HANDLE;
    other.physical_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.queue_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        destroy();
        api_ = std::move(other.api_);
        instance_ = other.instance_;
        physical_ = other.physical_;
        device_ = other.device_;
        queue_ = other.queue_;
        pool_ = other.pool_;
        queueFamily_ = other.queueFamily_;
        name_ = std::move(other.name_);
        limits_ = other.limits_;
        other.instance_ = VK_NULL_HANDLE;
        other.physical_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.queue_ = VK_NULL_HANDLE;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

Result<std::uint32_t> Device::memoryTypeIndex(std::uint32_t typeBits,
                                              VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
    for (std::uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) == 0) continue;
        if ((mem.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    return std::unexpected("no memory type on " + name_ + " has properties 0x" +
                           [properties] {
                               char buf[16];
                               std::snprintf(buf, sizeof buf, "%x", properties);
                               return std::string(buf);
                           }());
}

Result<void> Device::submitAndWait(const std::function<void(VkCommandBuffer)>& record) {
    if (!valid()) return std::unexpected(std::string("submitAndWait on an invalid device"));

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const DeviceApi& api = *api_;
    if (VkResult r = api.vkAllocateCommandBuffers(device_, &ai, &cmd); r != VK_SUCCESS) {
        return std::unexpected(fail("vkAllocateCommandBuffers", r));
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = api.vkBeginCommandBuffer(cmd, &bi); r != VK_SUCCESS) {
        api.vkFreeCommandBuffers(device_, pool_, 1, &cmd);
        return std::unexpected(fail("vkBeginCommandBuffer", r));
    }
    record(cmd);
    if (VkResult r = api.vkEndCommandBuffer(cmd); r != VK_SUCCESS) {
        api.vkFreeCommandBuffers(device_, pool_, 1, &cmd);
        return std::unexpected(fail("vkEndCommandBuffer", r));
    }

    // A fence, not vkQueueWaitIdle: the wait has to be able to TIME OUT. A hung
    // submit that blocks forever reports nothing, and a suite that never returns
    // is indistinguishable from one that is slow.
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (VkResult r = api.vkCreateFence(device_, &fi, nullptr, &fence); r != VK_SUCCESS) {
        api.vkFreeCommandBuffers(device_, pool_, 1, &cmd);
        return std::unexpected(fail("vkCreateFence", r));
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkResult submitted = api.vkQueueSubmit(queue_, 1, &si, fence);
    if (submitted == VK_SUCCESS) {
        constexpr std::uint64_t kTenSeconds = 10ull * 1000 * 1000 * 1000;
        submitted = api.vkWaitForFences(device_, 1, &fence, VK_TRUE, kTenSeconds);
    }
    api.vkDestroyFence(device_, fence, nullptr);
    api.vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    if (submitted == VK_TIMEOUT) {
        return std::unexpected(std::string("the submission did not complete in 10 s on ") + name_);
    }
    if (submitted != VK_SUCCESS) return std::unexpected(fail("vkQueueSubmit", submitted));
    return {};
}

}  // namespace rb
