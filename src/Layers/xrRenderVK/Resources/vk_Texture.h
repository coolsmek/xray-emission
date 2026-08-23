
// vk_Texture.h — Game texture creation/destruction for xrRenderVK.
// Handles DDS file parsing, VkImage upload, VkImageView and VkSampler creation.
// Render-target textures are managed separately by vk_SH_RT.cpp.
#pragma once
#include <vulkan/vulkan.h>
#include "../Managers/vk_MemoryManager.h"
#include "../../xrRender/xrD3DDefs.h"

// Create a 2-D game texture from raw DDS data in memory.
// On success fills all fields of outWrapper (image, alloc, format, imageView, sampler, w/h/mips)
// and returns VK_SUCCESS.  Caller owns the wrapper and must call vk_DestroyTexture2D.
// Returns VK_ERROR_FORMAT_NOT_SUPPORTED for non-DDS or unknown formats; logs details.
VkResult vk_CreateTexture2D(
    const void*          ddsData,
    size_t               ddsSize,
    VkTexture2DWrapper&  outWrapper,
    uint32_t             mip_skip = 0);

VkResult vk_CreateDummyTexture(VkTexture2DWrapper& outWrapper);
VkResult vk_CreateMaterialLUT(VkTexture2DWrapper& outWrapper);

void vk_Texture_Cleanup();

// Destroys all Vulkan objects created by vk_CreateTexture2D.
// Safe to call with a partially-filled wrapper (null handles are skipped).
void vk_DestroyTexture2D(VkTexture2DWrapper& wrapper);

