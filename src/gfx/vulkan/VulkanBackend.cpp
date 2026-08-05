// Vulkan backend.
//
// Device detection is real: it creates a throwaway instance and reports the
// physical devices it finds, so `--list-backends` tells you whether this
// machine could run it. The renderer itself is not written yet.
//
// To finish this backend, override initialize/render/present/renderToImage:
// swapchain + render pass, a descriptor set matching the mesh shader's uniform
// block, and SPIR-V compiled from the GLSL already in gfx/opengl/Shaders.h.

#include "gfx/UnimplementedBackend.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace tessera::gfx {
namespace {

class VulkanBackend final : public UnimplementedBackend {
public:
    VulkanBackend() : UnimplementedBackend(BackendId::Vulkan, "vulkan", "Vulkan 1.2") {}

protected:
    [[nodiscard]] std::string detectStatus() const override {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "tessera";
        application.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &application;
#if defined(__APPLE__)
        // MoltenVK is a portability driver and refuses to load without this.
        createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        const char* portability = "VK_KHR_portability_enumeration";
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &portability;
#endif

        VkInstance instance = VK_NULL_HANDLE;
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            return "no Vulkan loader or driver found";
        }

        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) {
            vkDestroyInstance(instance, nullptr);
            return "Vulkan loader present but no devices";
        }

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices.front(), &properties);
        std::string name = properties.deviceName;
        vkDestroyInstance(instance, nullptr);

        return name + " (detected, renderer not implemented)";
    }
};

}  // namespace

BackendPtr makeVulkanBackend() { return std::make_unique<VulkanBackend>(); }

}  // namespace tessera::gfx
