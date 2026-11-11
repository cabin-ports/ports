// The implementation without the Vulkan loader: VMA reaches Vulkan only
// through VmaVulkanFunctions, and the virtual allocator below calls none.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// Where vk_mem_alloc.h:3227-3302 falls through to its asserting
// vma_aligned_alloc, supply the allocation it asks for (cabin.toml).
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) &&     \
    !(defined(__linux__) && defined(__GLIBCXX__) &&                         \
      !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC)) &&                             \
    !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
static void *posix_aligned_alloc(size_t alignment, size_t size) {
  void *pointer = nullptr;
  if (alignment < sizeof(void *)) {
    alignment = sizeof(void *);
  }
  return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
}
#define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment)                          \
  posix_aligned_alloc((alignment), (size))
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

static_assert(VMA_VERSION == VK_MAKE_VERSION(3, 5, 0), "VMA_VERSION");
static_assert(VMA_VULKAN_VERSION == 1004000, "Vulkan 1.4 headers");
static_assert(std::is_same<decltype(VmaVulkanFunctions::vkGetInstanceProcAddr),
                           PFN_vkGetInstanceProcAddr>::value,
              "VmaVulkanFunctions");
static_assert(VMA_MEMORY_USAGE_AUTO == 7 &&
                  VMA_ALLOCATION_CREATE_MAPPED_BIT == 0x4 &&
                  VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT == 0x1,
              "enumerants");

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct HostCounts {
  int allocations = 0;
  int frees = 0;
  bool aligned = true;
};

VKAPI_ATTR void *VKAPI_CALL counting_allocation(void *user, size_t size,
                                                size_t alignment,
                                                VkSystemAllocationScope) {
  HostCounts &counts = *static_cast<HostCounts *>(user);
  ++counts.allocations;
  void *pointer = std::malloc(size);
  if (alignment > alignof(std::max_align_t) ||
      reinterpret_cast<std::uintptr_t>(pointer) % alignment != 0) {
    counts.aligned = false;
  }
  return pointer;
}

VKAPI_ATTR void VKAPI_CALL counting_free(void *user, void *pointer) {
  if (pointer != nullptr) {
    ++static_cast<HostCounts *>(user)->frees;
  }
  std::free(pointer);
}

// The default (TLSF) algorithm on the default host allocation.
void tlsf_block() {
  VmaVirtualBlockCreateInfo block_info = {};
  block_info.size = 1 << 20;
  VmaVirtualBlock block = VK_NULL_HANDLE;
  check(vmaCreateVirtualBlock(&block_info, &block) == VK_SUCCESS &&
            block != VK_NULL_HANDLE,
        "vmaCreateVirtualBlock");

  const VkDeviceSize sizes[3] = {100, 1000, 4096};
  const VkDeviceSize alignments[3] = {256, 64, 4096};
  int tags[3] = {};
  VmaVirtualAllocation allocations[3] = {};
  VkDeviceSize offsets[3] = {};
  for (int i = 0; i < 3; ++i) {
    VmaVirtualAllocationCreateInfo info = {};
    info.size = sizes[i];
    info.alignment = alignments[i];
    info.pUserData = &tags[i];
    check(vmaVirtualAllocate(block, &info, &allocations[i], &offsets[i]) ==
                  VK_SUCCESS &&
              offsets[i] % alignments[i] == 0 &&
              offsets[i] + sizes[i] <= block_info.size,
          "vmaVirtualAllocate places an aligned range in the block");
  }
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      check(offsets[i] + sizes[i] <= offsets[j] ||
                offsets[j] + sizes[j] <= offsets[i],
            "allocations do not overlap");
    }
  }

  VmaVirtualAllocationInfo info = {};
  vmaGetVirtualAllocationInfo(block, allocations[1], &info);
  check(info.offset == offsets[1] && info.size == 1000 &&
            info.pUserData == &tags[1],
        "vmaGetVirtualAllocationInfo");

  VmaStatistics stats = {};
  vmaGetVirtualBlockStatistics(block, &stats);
  check(stats.blockCount == 1 && stats.allocationCount == 3 &&
            stats.blockBytes == block_info.size &&
            stats.allocationBytes == 100 + 1000 + 4096,
        "vmaGetVirtualBlockStatistics");

  VmaVirtualAllocationCreateInfo too_big = {};
  too_big.size = block_info.size;
  VmaVirtualAllocation none = {};
  VkDeviceSize none_offset = 0;
  check(vmaVirtualAllocate(block, &too_big, &none, &none_offset) ==
                VK_ERROR_OUT_OF_DEVICE_MEMORY &&
            none == VK_NULL_HANDLE && none_offset == UINT64_MAX,
        "an allocation that does not fit fails");

  vmaVirtualFree(block, allocations[0]);
  VmaDetailedStatistics detailed = {};
  vmaCalculateVirtualBlockStatistics(block, &detailed);
  check(detailed.statistics.allocationCount == 2 &&
            detailed.allocationSizeMin == 1000 &&
            detailed.allocationSizeMax == 4096,
        "vmaVirtualFree and vmaCalculateVirtualBlockStatistics");
  check(vmaIsVirtualBlockEmpty(block) == VK_FALSE, "a block in use");
  vmaClearVirtualBlock(block);
  check(vmaIsVirtualBlockEmpty(block) == VK_TRUE, "vmaClearVirtualBlock");
  vmaDestroyVirtualBlock(block);
}

// The linear algorithm, its upper-address stack, the JSON dump, and
// VkAllocationCallbacks.
void linear_block() {
  HostCounts counts;
  VkAllocationCallbacks callbacks = {};
  callbacks.pUserData = &counts;
  callbacks.pfnAllocation = counting_allocation;
  callbacks.pfnFree = counting_free;

  VmaVirtualBlockCreateInfo block_info = {};
  block_info.size = 4096;
  block_info.flags = VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT;
  block_info.pAllocationCallbacks = &callbacks;
  VmaVirtualBlock block = VK_NULL_HANDLE;
  check(vmaCreateVirtualBlock(&block_info, &block) == VK_SUCCESS,
        "vmaCreateVirtualBlock with VkAllocationCallbacks");

  // Non-null pUserData: the linear algorithm's statistics skip null ones
  // (cabin.toml).
  int tags[3] = {};
  VmaVirtualAllocationCreateInfo info = {};
  VmaVirtualAllocation allocations[3] = {};
  VkDeviceSize offsets[3] = {};
  info.size = 100;
  info.pUserData = &tags[0];
  vmaVirtualAllocate(block, &info, &allocations[0], &offsets[0]);
  info.size = 64;
  info.alignment = 64;
  info.pUserData = &tags[1];
  vmaVirtualAllocate(block, &info, &allocations[1], &offsets[1]);
  info.size = 256;
  info.alignment = 16;
  info.flags = VMA_VIRTUAL_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
  info.pUserData = &tags[2];
  vmaVirtualAllocate(block, &info, &allocations[2], &offsets[2]);
  check(offsets[0] == 0 && offsets[1] == 128 && offsets[2] == 4096 - 256,
        "linear placement: 0, then 100 aligned up to 64, then the top");

  VmaStatistics stats = {};
  vmaGetVirtualBlockStatistics(block, &stats);
  check(stats.allocationCount == 3 && stats.allocationBytes == 420,
        "linear block statistics");

  char *json = nullptr;
  vmaBuildVirtualBlockStatsString(block, &json, VK_TRUE);
  check(json != nullptr && std::strstr(json, "\"Allocations\": 3") &&
            std::strstr(json, "\"UnusedBytes\": 3676") &&
            std::strstr(json, "{\"Offset\": 3840, \"Size\": 256"),
        "vmaBuildVirtualBlockStatsString");
  vmaFreeVirtualBlockStatsString(block, json);

  vmaVirtualFree(block, allocations[1]);
  vmaVirtualFree(block, allocations[2]);
  vmaVirtualFree(block, allocations[0]);
  check(vmaIsVirtualBlockEmpty(block) == VK_TRUE, "linear free");
  vmaDestroyVirtualBlock(block);
  check(counts.allocations > 0 && counts.allocations == counts.frees &&
            counts.aligned,
        "VkAllocationCallbacks allocations are all freed");
}

} // namespace

int main() {
  tlsf_block();
  linear_block();
  if (failures != 0) {
    return 1;
  }
  std::printf("vulkan-memory-allocator %u.%u.%u: virtual blocks allocate, "
              "report and free\n",
              VK_VERSION_MAJOR(VMA_VERSION), VK_VERSION_MINOR(VMA_VERSION),
              VK_VERSION_PATCH(VMA_VERSION));
  return 0;
}
