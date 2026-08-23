// vk_LayoutTransitions.h — Vulkan Image Barrier Helpers
#pragma once
#include <vulkan/vulkan.h>

// Single-mip pipeline barrier (baseMip=0, levelCount=1).
void vk_TransitionImageLayout(
    VkCommandBuffer       cmd,
    VkImage               image,
    VkImageAspectFlags    aspect,
    VkImageLayout         oldLayout,
    VkImageLayout         newLayout,
    VkPipelineStageFlags2 srcStage,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2        srcAccess,
    VkAccessFlags2        dstAccess);

// Multi-mip overload — transitions baseMip..baseMip+mipCount-1 in one barrier.
// Used by the texture loader after uploading all mip slices.
void vk_TransitionImageLayout(
    VkCommandBuffer       cmd,
    VkImage               image,
    VkImageAspectFlags    aspect,
    VkImageLayout         oldLayout,
    VkImageLayout         newLayout,
    VkPipelineStageFlags2 srcStage,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2        srcAccess,
    VkAccessFlags2        dstAccess,
    uint32_t              baseMip,
    uint32_t              mipCount);

// Batch multiple image memory barriers into a single vkCmdPipelineBarrier call
void vk_TransitionImages(
    VkCommandBuffer             cmd,
    const VkImageMemoryBarrier* barriers,
    uint32_t                    count,
    VkPipelineStageFlags2       srcStage,
    VkPipelineStageFlags2       dstStage);

