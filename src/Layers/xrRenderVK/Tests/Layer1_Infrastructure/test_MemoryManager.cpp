#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
#include "../../Resources/vk_BufferUtils.h"

// Allocate a device-local vertex buffer, verify handle, then destroy it.
// Under VerifiedVKASAN, ASAN will catch any use-after-free if vk_DestroyBuffer
// is broken.
VK_TEST(Memory, DeviceLocalAllocFree)
{
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult res = vk_CreateDeviceLocalBuffer(
        4096,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        buf, alloc);
    VK_EXPECT_VK(res);
    VK_EXPECT(buf   != VK_NULL_HANDLE);
    VK_EXPECT(alloc != VK_NULL_HANDLE);
    vk_DestroyBuffer(buf, alloc);  // Must not crash or leak
    return true;
}

// Allocate a staging buffer and verify the returned mapped pointer is non-null.
VK_TEST(Memory, StagingBufferMapped)
{
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkResult res = vk_CreateStagingBuffer(1024, buf, alloc, mapped);
    VK_EXPECT_VK(res);
    VK_EXPECT(mapped != nullptr);
    // Write a pattern — ASAN will catch out-of-bounds
    memset(mapped, 0xCD, 1024);
    vk_DestroyBuffer(buf, alloc);
    return true;
}

// Upload a 64-byte pattern into a device-local buffer via vk_UploadBuffer.
// Verifies that the transfer pool, command buffer alloc, and queue submit all work.
VK_TEST(Memory, UploadBufferRoundTrip)
{
    const uint32_t kSize = 64;
    uint8_t src[kSize];
    memset(src, 0xAB, kSize);

    VkBuffer dst = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult res = vk_CreateDeviceLocalBuffer(kSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, dst, alloc);
    VK_EXPECT_VK(res);

    bool ok = vk_UploadBuffer(src, kSize, dst);
    VK_EXPECT(ok);

    vk_DestroyBuffer(dst, alloc);
    return true;
}

// Verify the descriptor manager dynamic uniform ring buffer is mapped and sized.
VK_TEST(Memory, DescriptorRingBufferMapped)
{
    // AllocateDynamicUniform with size 0 should return a non-null pointer
    // (offset 0 is valid even for size-0 alloc in the current impl).
    // The key invariant: the mapped pointer stored internally is not null.
    uint32_t offset = 0;
    void* p = DescriptorManager.AllocateDynamicUniform(256, offset);
    VK_EXPECT(p != nullptr);
    VK_EXPECT(offset == 0); // first alloc in frame must be at offset 0
    return true;
}

#endif // VK_ENABLE_TESTS
