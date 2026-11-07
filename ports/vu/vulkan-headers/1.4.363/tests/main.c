/* Consumer test from C: the header version and version-packing macros,
 * enum and struct values the Vulkan specification fixes, a command called
 * through its PFN_ type, the ICD and layer interface headers, and the video
 * codec headers vulkan_core.h pulls in.  Nothing here needs the Vulkan
 * loader. */
#include <vulkan/vk_icd.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static VKAPI_ATTR VkResult VKAPI_CALL
fake_enumerate_instance_version(uint32_t *api_version) {
  *api_version = VK_HEADER_VERSION_COMPLETE;
  return VK_SUCCESS;
}

/* A stand-in for the loader's vkGetInstanceProcAddr. */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
fake_get_instance_proc_addr(VkInstance instance, const char *name) {
  if (instance == VK_NULL_HANDLE &&
      strcmp(name, "vkEnumerateInstanceVersion") == 0) {
    return (PFN_vkVoidFunction)fake_enumerate_instance_version;
  }
  return NULL;
}

int main(void) {
  uint32_t v = VK_MAKE_API_VERSION(0, 1, 4, 363);
  uint32_t reported = 0;
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
  VkInstanceCreateInfo create = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  PFN_vkGetInstanceProcAddr get_proc = fake_get_instance_proc_addr;
  PFN_vkEnumerateInstanceVersion enumerate_version;
  VK_LOADER_DATA loader_data;
  VkNegotiateLayerInterface negotiate = {
      .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
      .loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION};

  CHECK(VK_HEADER_VERSION == 363);
  CHECK(VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE) == 1);
  CHECK(VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE) == 4);
  CHECK(VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE) == 363);
  CHECK(v == ((1u << 22) | (4u << 12) | 363u));
  CHECK(VK_API_VERSION_VARIANT(v) == 0);
  CHECK(VK_API_VERSION_1_4 == VK_MAKE_API_VERSION(0, 1, 4, 0));

  CHECK(VK_STRUCTURE_TYPE_APPLICATION_INFO == 0);
  CHECK(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO == 1);
  CHECK(VK_SUCCESS == 0);
  CHECK(VK_ERROR_OUT_OF_HOST_MEMORY == -1);
  CHECK(VK_FORMAT_R8G8B8A8_UNORM == 37);
  CHECK((VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) == 0x81);
  CHECK(VK_MAX_EXTENSION_NAME_SIZE == 256);
  CHECK(sizeof(VkExtent2D) == 8);
  CHECK(offsetof(VkExtent3D, depth) == 8);

  app.apiVersion = VK_API_VERSION_1_4;
  create.pApplicationInfo = &app;
  CHECK(create.pApplicationInfo->sType == VK_STRUCTURE_TYPE_APPLICATION_INFO);
  CHECK(create.pApplicationInfo->apiVersion == VK_API_VERSION_1_4);

  /* The loader's lookup pattern: fetch a command by name, call it. */
  enumerate_version = (PFN_vkEnumerateInstanceVersion)get_proc(
      VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
  CHECK(enumerate_version != NULL);
  if (enumerate_version != NULL) {
    CHECK(enumerate_version(&reported) == VK_SUCCESS);
    CHECK(reported == VK_HEADER_VERSION_COMPLETE);
  }
  CHECK(get_proc(VK_NULL_HANDLE, "vkCreateInstance") == NULL);

  /* vk_icd.h: the dispatch-pointer magic an ICD stamps on its objects. */
  CHECK(CURRENT_LOADER_ICD_INTERFACE_VERSION == 7);
  loader_data.loaderMagic = 0;
  CHECK(!valid_loader_magic_value(&loader_data));
  set_loader_magic_value(&loader_data);
  CHECK(loader_data.loaderMagic == ICD_LOADER_MAGIC);
  CHECK(valid_loader_magic_value(&loader_data));

  /* vk_layer.h: the layer negotiation struct and loader create-info types. */
  CHECK(negotiate.loaderLayerInterfaceVersion == 2);
  CHECK(negotiate.pNext == NULL);
  CHECK(VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO == 47);
  CHECK(VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO == 48);

  /* vk_video/vulkan_video_codec_h264std.h: profile_idc from H.264. */
  CHECK(STD_VIDEO_H264_PROFILE_IDC_HIGH == 100);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("vulkan-headers C consumer test passed");
  return 0;
}
