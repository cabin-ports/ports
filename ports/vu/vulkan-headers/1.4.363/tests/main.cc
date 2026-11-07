// Consumer test from C++: Vulkan-Hpp's wrapper structs and their layout
// compatibility with the C structs, Flags arithmetic, to_string,
// StructureChain, the format traits and video enums, and vk::raii::Context
// dispatching through a stand-in vkGetInstanceProcAddr, including the
// mapping of an error result to its exception.  Nothing here needs the
// Vulkan loader.

// Without the dynamic loader tool, vk::raii::Context takes the
// vkGetInstanceProcAddr to dispatch through instead of loading the Vulkan
// library itself (vulkan_raii.hpp, Context's constructors).
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_video.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

VkResult version_result = VK_SUCCESS;

VKAPI_ATTR VkResult VKAPI_CALL fake_enumerate_instance_version(uint32_t* api_version) {
  *api_version = VK_HEADER_VERSION_COMPLETE;
  return version_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_enumerate_instance_layer_properties(
    uint32_t* count, VkLayerProperties* properties) {
  static const char* const names[] = {"VK_LAYER_cabin_first", "VK_LAYER_cabin_second"};
  if (properties == nullptr) {
    *count = 2;
    return VK_SUCCESS;
  }
  uint32_t n = *count < 2 ? *count : 2;
  for (uint32_t i = 0; i < n; ++i) {
    std::memset(&properties[i], 0, sizeof properties[i]);
    std::memcpy(properties[i].layerName, names[i], std::strlen(names[i]) + 1);
    properties[i].specVersion = VK_API_VERSION_1_4;
    properties[i].implementationVersion = i + 1;
  }
  *count = n;
  return n < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}

// A stand-in for the loader's vkGetInstanceProcAddr.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fake_get_instance_proc_addr(VkInstance instance,
                                                                    const char* name) {
  if (instance != VK_NULL_HANDLE) return nullptr;
  if (std::strcmp(name, "vkEnumerateInstanceVersion") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(fake_enumerate_instance_version);
  if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(fake_enumerate_instance_layer_properties);
  return nullptr;
}

}  // namespace

int main() {
  CHECK(VK_HEADER_VERSION == 363);
  CHECK(vk::makeApiVersion(0, 1, 4, 0) == VK_API_VERSION_1_4);

  vk::ApplicationInfo app("cabin-probe", 1, "none", 0, VK_API_VERSION_1_4);
  CHECK(app.sType == vk::StructureType::eApplicationInfo);
  const VkApplicationInfo& c_app = app;
  CHECK(c_app.sType == VK_STRUCTURE_TYPE_APPLICATION_INFO);
  CHECK(c_app.apiVersion == VK_API_VERSION_1_4);
  CHECK(std::string(c_app.pApplicationName) == "cabin-probe");
  CHECK(sizeof(vk::ApplicationInfo) == sizeof(VkApplicationInfo));

  vk::BufferUsageFlags usage =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eVertexBuffer;
  CHECK(static_cast<VkBufferUsageFlags>(usage) == 0x81);
  CHECK(static_cast<bool>(usage & vk::BufferUsageFlagBits::eVertexBuffer));
  CHECK(!(usage & vk::BufferUsageFlagBits::eIndexBuffer));

  CHECK(vk::to_string(vk::Result::eErrorOutOfHostMemory) == "ErrorOutOfHostMemory");
  CHECK(vk::to_string(vk::Format::eR8G8B8A8Unorm) == "R8G8B8A8Unorm");

  vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                     vk::PhysicalDeviceVulkan12Features>
      chain;
  auto& features2 = chain.get<vk::PhysicalDeviceFeatures2>();
  auto& vulkan11 = chain.get<vk::PhysicalDeviceVulkan11Features>();
  auto& vulkan12 = chain.get<vk::PhysicalDeviceVulkan12Features>();
  CHECK(features2.pNext == &vulkan11);
  CHECK(vulkan11.pNext == &vulkan12);
  CHECK(vulkan12.pNext == nullptr);
  chain.unlink<vk::PhysicalDeviceVulkan11Features>();
  CHECK(features2.pNext == &vulkan12);

  CHECK(vk::blockSize(vk::Format::eR8G8B8A8Unorm) == 4);
  CHECK(vk::componentCount(vk::Format::eR8G8B8A8Unorm) == 4);
  CHECK(vk::blockSize(vk::Format::eBc1RgbUnormBlock) == 8);

  CHECK(static_cast<int>(vk::video::H264ProfileIdc::eHigh) == 100);

  vk::raii::Context context(fake_get_instance_proc_addr);
  CHECK(context.enumerateInstanceVersion() == VK_HEADER_VERSION_COMPLETE);
  std::vector<vk::LayerProperties> layers = context.enumerateInstanceLayerProperties();
  CHECK(layers.size() == 2);
  if (layers.size() == 2) {
    CHECK(std::string(layers[0].layerName.data()) == "VK_LAYER_cabin_first");
    CHECK(std::string(layers[1].layerName.data()) == "VK_LAYER_cabin_second");
    CHECK(layers[1].implementationVersion == 2);
  }

  version_result = VK_ERROR_OUT_OF_HOST_MEMORY;
  bool threw = false;
  try {
    (void)context.enumerateInstanceVersion();
  } catch (const vk::OutOfHostMemoryError& e) {
    const std::system_error& base = e;
    threw = e.code() == vk::Result::eErrorOutOfHostMemory &&
            std::string(base.code().category().name()) == "vk::Result";
  }
  CHECK(threw);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("vulkan-headers C++ consumer test passed");
  return 0;
}
