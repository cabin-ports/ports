#include <vulkan/layer/vk_layer_settings.hpp>
#include <vulkan/utility/vk_safe_struct.hpp>
#include <vulkan/utility/vk_struct_helper.hpp>

// vk_small_containers.hpp uses std::numeric_limits without including
// <limits>, as upstream's own test includes it first.
#include <limits>
#include <vulkan/utility/vk_small_containers.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

// A safe struct deep-copies the create info, its strings and its pNext
// chain, so later changes to the originals do not reach the copy.
static void check_safe_struct() {
  char app_name[] = "demo";
  char extension[] = "VK_EXT_debug_utils";
  const char* extensions[] = {extension};
  VkValidationFeatureEnableEXT enables[] = {
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};

  VkValidationFeaturesEXT features = vku::InitStructHelper();
  features.enabledValidationFeatureCount = 1;
  features.pEnabledValidationFeatures = enables;
  VkApplicationInfo app = vku::InitStructHelper();
  app.pApplicationName = app_name;
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo info = vku::InitStructHelper(&features);
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;
  CHECK(info.sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  CHECK(vku::FindStructInPNextChain<VkValidationFeaturesEXT>(info.pNext) ==
        &features);
  CHECK(vku::FindStructInPNextChain<VkLayerSettingsCreateInfoEXT>(
            info.pNext) == nullptr);

  vku::safe_VkInstanceCreateInfo copy(&info);
  app_name[0] = 'X';
  extension[3] = 'X';
  enables[0] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;

  CHECK(copy.pApplicationInfo != nullptr);
  CHECK(std::strcmp(copy.pApplicationInfo->pApplicationName, "demo") == 0);
  CHECK(copy.pApplicationInfo->apiVersion == VK_API_VERSION_1_3);
  CHECK(std::strcmp(copy.ppEnabledExtensionNames[0], "VK_EXT_debug_utils") ==
        0);
  // The copied chain holds safe structs (SafePnextCopy).
  const auto* copied =
      static_cast<const vku::safe_VkValidationFeaturesEXT*>(copy.pNext);
  CHECK(copied != nullptr && static_cast<const void*>(copied) != &features);
  CHECK(copied->sType == VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
  CHECK(copied->enabledValidationFeatureCount == 1);
  CHECK(copied->pEnabledValidationFeatures[0] ==
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);

  CHECK(vku::AddExtension(copy, "VK_KHR_surface"));
  CHECK(!vku::AddExtension(copy, "VK_KHR_surface"));
  CHECK(copy.enabledExtensionCount == 2);
  CHECK(vku::FindExtension(copy, "VK_KHR_surface") == 1);

  vku::safe_VkInstanceCreateInfo assigned;
  assigned = copy;
  CHECK(assigned.ppEnabledExtensionNames != copy.ppEnabledExtensionNames);
  CHECK(std::strcmp(assigned.ppEnabledExtensionNames[1], "VK_KHR_surface") ==
        0);
}

// A list setting from the environment splits on ',' or the platform's
// delimiter: ';' where WIN32 is defined, as the manifest does for MSVC,
// ':' elsewhere.  Other Windows compilers may or may not define WIN32, so
// they use ','.
static void check_env_list() {
#if defined(_MSC_VER) && !defined(__clang__)
  CHECK(_putenv_s("VK_LUNARG_TEST_MY_LIST", "red;green") == 0);
#elif defined(_WIN32)
  CHECK(_putenv_s("VK_LUNARG_TEST_MY_LIST", "red,green") == 0);
#else
  CHECK(setenv("VK_LUNARG_TEST_MY_LIST", "red:green", 1) == 0);
#endif

  VkuLayerSettingSet set = VK_NULL_HANDLE;
  CHECK(vkuCreateLayerSettingSet("VK_LAYER_LUNARG_test", nullptr, nullptr,
                                 nullptr, &set) == VK_SUCCESS);
  CHECK(vkuHasLayerSetting(set, "my_list"));
  std::vector<std::string> values;
  CHECK(vkuGetLayerSettingValues(set, "my_list", values) == VK_SUCCESS);
  CHECK(values.size() == 2 && values[0] == "red" && values[1] == "green");
  vkuDestroyLayerSettingSet(set, nullptr);
}

static void check_small_vector() {
  vku::small::vector<std::string, 2> v;
  v.emplace_back("a");
  v.emplace_back("b");
  v.emplace_back("c");  // Spills from the inline storage to the heap.
  CHECK(v.size() == 3 && v[0] == "a" && v[2] == "c");
}

int main() {
  check_safe_struct();
  check_env_list();
  check_small_vector();
  std::puts("vulkan-utility-libraries consumer ok");
  return 0;
}
