/* The implementation is C++ (cabin.toml), so a C program uses the C API's
   declarations and links an implementation compiled elsewhere.  This checks
   that those declarations are valid C99 and carry the documented values and
   types; sizeof does not call a function, so nothing is linked. */
#include <vk_mem_alloc.h>

#include <stdio.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

/* Has the type of the loader's vkGetInstanceProcAddr, which a consumer passes
   with VMA_DYNAMIC_VULKAN_FUNCTIONS. */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
fake_get_instance_proc_addr(VkInstance instance, const char *name) {
  (void)instance;
  (void)name;
  return NULL;
}

int main(void) {
  VmaVulkanFunctions functions = {0};
  VmaAllocatorCreateInfo allocator_info = {0};
  VmaAllocationCreateInfo allocation_info = {0};
  VmaVirtualBlockCreateInfo block_info = {0};
  VmaVirtualAllocationCreateInfo virtual_info = {0};
  VmaAllocator allocator = VK_NULL_HANDLE;
  VmaVirtualBlock block = VK_NULL_HANDLE;
  VmaVirtualAllocation allocation = VK_NULL_HANDLE;
  uint32_t memory_type = 0;

  check(VMA_VERSION == VK_MAKE_VERSION(3, 5, 0), "VMA_VERSION");
  check(VMA_VULKAN_VERSION == 1004000, "VMA_VULKAN_VERSION from Vulkan 1.4");
  check(VMA_MEMORY_USAGE_AUTO == 7 && VMA_MEMORY_USAGE_AUTO_PREFER_HOST == 9 &&
            VMA_ALLOCATION_CREATE_MAPPED_BIT == 0x4 &&
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT == 0x400 &&
            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT == 0x8 &&
            VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT == 0x4 &&
            VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT == 0x1,
        "enumerants");

  /* The configuration a consumer without a linked loader fills in. */
  functions.vkGetInstanceProcAddr = fake_get_instance_proc_addr;
  allocator_info.vulkanApiVersion = VK_API_VERSION_1_4;
  allocator_info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  allocator_info.pVulkanFunctions = &functions;
  allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
  allocation_info.flags =
      VMA_ALLOCATION_CREATE_MAPPED_BIT |
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  block_info.size = 1 << 20;
  block_info.flags = VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT;
  virtual_info.size = 256;
  virtual_info.alignment = 16;

  /* The prototypes declare these return types. */
  check(sizeof(vmaCreateAllocator(&allocator_info, &allocator)) ==
                sizeof(VkResult) &&
            sizeof(vmaFindMemoryTypeIndex(allocator, UINT32_MAX,
                                          &allocation_info, &memory_type)) ==
                sizeof(VkResult) &&
            sizeof(vmaCreateVirtualBlock(&block_info, &block)) ==
                sizeof(VkResult) &&
            sizeof(vmaVirtualAllocate(block, &virtual_info, &allocation,
                                      NULL)) == sizeof(VkResult) &&
            sizeof(vmaIsVirtualBlockEmpty(block)) == sizeof(VkBool32),
        "VMA prototypes");

  if (failures != 0) {
    return 1;
  }
  printf("vulkan-memory-allocator %u.%u.%u: C API declarations\n",
         VK_VERSION_MAJOR(VMA_VERSION), VK_VERSION_MINOR(VMA_VERSION),
         VK_VERSION_PATCH(VMA_VERSION));
  return 0;
}
