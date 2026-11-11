#include <vulkan/layer/vk_layer_settings.h>
#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vk_enum_string_helper.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,       \
              #cond);                                                        \
      exit(1);                                                               \
    }                                                                        \
  } while (0)

static int lookups;

static VKAPI_ATTR void VKAPI_CALL
fake_destroy_instance(VkInstance instance, const VkAllocationCallbacks* a) {
  (void)instance;
  (void)a;
}

/* Stands in for the loader: resolves only vkDestroyInstance. */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
fake_gipa(VkInstance instance, const char* name) {
  (void)instance;
  lookups++;
  if (strcmp(name, "vkDestroyInstance") == 0) {
    return (PFN_vkVoidFunction)fake_destroy_instance;
  }
  return NULL;
}

static void check_layer_settings(void) {
  const int32_t ints[2] = {7, -3};
  const char* names[1] = {"hello"};
  const VkLayerSettingEXT settings[2] = {
      {"VK_LAYER_LUNARG_test", "my_ints", VK_LAYER_SETTING_TYPE_INT32_EXT, 2,
       ints},
      {"VK_LAYER_LUNARG_test", "my_name", VK_LAYER_SETTING_TYPE_STRING_EXT, 1,
       names},
  };
  VkLayerSettingsCreateInfoEXT info = {
      VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, NULL, 2, settings};

  /* The settings are found in an instance create info's pNext chain. */
  VkInstanceCreateInfo instance_info;
  memset(&instance_info, 0, sizeof instance_info);
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pNext = &info;
  CHECK(vkuFindLayerSettingsCreateInfo(&instance_info) == &info);

  VkuLayerSettingSet set = VK_NULL_HANDLE;
  CHECK(vkuCreateLayerSettingSet("VK_LAYER_LUNARG_test", &info, NULL, NULL,
                                 &set) == VK_SUCCESS);
  CHECK(vkuHasLayerSetting(set, "my_ints"));
  CHECK(!vkuHasLayerSetting(set, "absent"));

  uint32_t count = 0;
  CHECK(vkuGetLayerSettingValues(set, "my_ints", VKU_LAYER_SETTING_TYPE_INT32,
                                 &count, NULL) == VK_SUCCESS);
  CHECK(count == 2);
  int32_t got[2] = {0, 0};
  count = 1;
  CHECK(vkuGetLayerSettingValues(set, "my_ints", VKU_LAYER_SETTING_TYPE_INT32,
                                 &count, got) == VK_INCOMPLETE);
  CHECK(count == 1 && got[0] == 7 && got[1] == 0);
  count = 2;
  CHECK(vkuGetLayerSettingValues(set, "my_ints", VKU_LAYER_SETTING_TYPE_INT32,
                                 &count, got) == VK_SUCCESS);
  CHECK(got[0] == 7 && got[1] == -3);

  const char* name = NULL;
  count = 1;
  CHECK(vkuGetLayerSettingValues(set, "my_name", VKU_LAYER_SETTING_TYPE_STRING,
                                 &count, &name) == VK_SUCCESS);
  CHECK(name != NULL && strcmp(name, "hello") == 0);

  vkuDestroyLayerSettingSet(set, NULL);
}

static void check_headers(void) {
  VkuInstanceDispatchTable table;
  vkuInitInstanceDispatchTable(VK_NULL_HANDLE, &table, fake_gipa);
  CHECK(table.GetInstanceProcAddr == fake_gipa);
  CHECK(table.DestroyInstance == fake_destroy_instance);
  CHECK(table.EnumeratePhysicalDevices == NULL);
  CHECK(lookups > 1);

  CHECK(vkuFormatIsDepthAndStencil(VK_FORMAT_D24_UNORM_S8_UINT));
  CHECK(!vkuFormatIsDepthAndStencil(VK_FORMAT_D32_SFLOAT));
  CHECK(vkuFormatIsSRGB(VK_FORMAT_R8G8B8A8_SRGB));
  CHECK(!vkuFormatIsSRGB(VK_FORMAT_R8G8B8A8_UNORM));
  CHECK(vkuFormatIsCompressed(VK_FORMAT_BC1_RGB_UNORM_BLOCK));
  CHECK(vkuFormatComponentCount(VK_FORMAT_R8G8B8A8_UNORM) == 4);
  CHECK(vkuFormatTexelBlockSize(VK_FORMAT_R16G16_SFLOAT) == 4);

  CHECK(strcmp(string_VkResult(VK_ERROR_DEVICE_LOST),
               "VK_ERROR_DEVICE_LOST") == 0);
  CHECK(strcmp(string_VkFormat(VK_FORMAT_B8G8R8A8_SRGB),
               "VK_FORMAT_B8G8R8A8_SRGB") == 0);
}

int main(void) {
  check_layer_settings();
  check_headers();
  puts("vulkan-utility-libraries consumer ok");
  return 0;
}
