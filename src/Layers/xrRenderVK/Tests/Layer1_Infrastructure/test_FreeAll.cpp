#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
#include "../../Resources/vk_BufferUtils.h"

static VmaBudget QueryBudget()
{
    VmaBudget budget[VK_MAX_MEMORY_HEAPS]{};
    // Use the public GetAllocator() accessor — m_allocator is private.
    vmaGetHeapBudgets(MemoryManager.GetAllocator(), budget);
    // Return the heap with the largest allocation (VRAM heap)
    VmaBudget best = budget[0];
    for (uint32_t i = 1; i < VK_MAX_MEMORY_HEAPS; ++i)
        if (budget[i].statistics.allocationBytes > best.statistics.allocationBytes)
            best = budget[i];
    return best;
}

// Allocate 16 small device-local buffers, record pre-alloc budget,
// allocate, free via vk_DestroyBuffer, then record post-free budget.
// Bytes allocated must return to within 4 KB of the pre-alloc level
// (VMA may retain pages internally; exact match is not required).
VK_TEST(FreeAll, NoLeakAfterSimulatedLevelUnload)
{
    VmaBudget before = QueryBudget();

    const uint32_t kCount = 16;
    VkBuffer    bufs[kCount];
    VmaAllocation allocs[kCount];

    for (uint32_t i = 0; i < kCount; ++i)
    {
        VkResult res = vk_CreateDeviceLocalBuffer(
            65536, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bufs[i], allocs[i]);
        VK_EXPECT_VK(res);
    }

    for (uint32_t i = 0; i < kCount; ++i)
        vk_DestroyBuffer(bufs[i], allocs[i]);

    // VMA may not return pages to the OS immediately; wait for idle and force trim.
    vkDeviceWaitIdle(HW.m_vkDevice);

    VmaBudget after = QueryBudget();
    int64_t delta = (int64_t)after.statistics.allocationBytes
                  - (int64_t)before.statistics.allocationBytes;
    Msg("* [VK_TEST FreeAll] alloc delta: %lld bytes (expected <= 4096)", delta);
    VK_EXPECT(delta <= 4096);  // No net leak larger than one page
    return true;
}

#endif // VK_ENABLE_TESTS
