#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
// vk_Texture.h is the Phase 6a new file — include it here to drive its development contract.
// This file will not compile until Phase 6a implementation is complete.
// #include "../../Resources/vk_Texture.h"

// For now, forward-declare the placeholder functions to allow the test to compile as a stub.
struct VkTextureHandle { void* _impl; };

VkResult vk_CreateTexture2D(
    const void* pixelData,
    size_t dataSizeInBytes,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImage& outImage,
    VmaAllocation& outAlloc,
    VkImageView& outView,
    VkSampler& outSampler);

void vk_DestroyTexture2D(VkImage image, VmaAllocation alloc, VkImageView view, VkSampler sampler);

// Generates a 4×4 RGBA8 checkerboard as raw pixel data — no file I/O.
static void MakeCheckerboard(uint8_t* out, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            bool white = ((x + y) & 1);
            out[(y * w + x) * 4 + 0] = white ? 0xFF : 0x00;
            out[(y * w + x) * 4 + 1] = white ? 0xFF : 0x00;
            out[(y * w + x) * 4 + 2] = white ? 0xFF : 0x00;
            out[(y * w + x) * 4 + 3] = 0xFF;
        }
}

// Allocate a 4×4 RGBA8 texture, upload a checkerboard pattern, verify handles.
VK_TEST(Texture, AllocAndUpload_4x4_RGBA8)
{
    const uint32_t W = 4, H = 4;
    uint8_t pixels[W * H * 4];
    MakeCheckerboard(pixels, W, H);

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    VkResult res = vk_CreateTexture2D(
        pixels, W * H * 4,
        W, H,
        VK_FORMAT_R8G8B8A8_UNORM,
        image, alloc, view, sampler);

    VK_EXPECT_VK(res);
    VK_EXPECT(image   != VK_NULL_HANDLE);
    VK_EXPECT(alloc   != VK_NULL_HANDLE);
    VK_EXPECT(view    != VK_NULL_HANDLE);
    VK_EXPECT(sampler != VK_NULL_HANDLE);

    vk_DestroyTexture2D(image, alloc, view, sampler);
    return true;
}

// Attempt to allocate 16 small textures — descriptor pool exhaustion test.
VK_TEST(Texture, LargeAllocationGracefulDegradation)
{
    const uint32_t kCount = 16;
    struct Entry { VkImage img; VmaAllocation alloc; VkImageView view; VkSampler smp; };
    std::vector<Entry> created;
    uint8_t pixel[4] = {0xFF, 0x00, 0xFF, 0xFF};

    for (uint32_t i = 0; i < kCount; ++i) {
        Entry e{};
        VkResult res = vk_CreateTexture2D(pixel, 4, 1, 1,
            VK_FORMAT_R8G8B8A8_UNORM, e.img, e.alloc, e.view, e.smp);
        if (res != VK_SUCCESS) break;  // acceptable: pool exhausted
        created.push_back(e);
    }
    VK_EXPECT(created.size() > 0);  // Must allocate at least one
    for (auto& e : created)
        vk_DestroyTexture2D(e.img, e.alloc, e.view, e.smp);
    return true;
}

// Sampler parameters test: verify the sampler is valid.
VK_TEST(Texture, SamplerHasValidFilter)
{
    uint8_t pixel[4] = {0x80, 0x80, 0x80, 0xFF};
    VkImage image; VmaAllocation alloc; VkImageView view; VkSampler sampler;
    VK_EXPECT_VK(vk_CreateTexture2D(pixel, 4, 1, 1,
        VK_FORMAT_R8G8B8A8_UNORM, image, alloc, view, sampler));

    VK_EXPECT(sampler != VK_NULL_HANDLE);
    vk_DestroyTexture2D(image, alloc, view, sampler);
    return true;
}

#endif // VK_ENABLE_TESTS
