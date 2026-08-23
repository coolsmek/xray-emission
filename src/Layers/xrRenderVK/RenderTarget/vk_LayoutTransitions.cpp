#include "stdafx.h"
#include "vk_LayoutTransitions.h"

// ─── Internal helper ─────────────────────────────────────────────────────────
static void DoTransition(
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
    uint32_t              mipCount)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = (VkAccessFlags)srcAccess;
    barrier.dstAccessMask                   = (VkAccessFlags)dstAccess;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = aspect;
    barrier.subresourceRange.baseMipLevel   = baseMip;
    barrier.subresourceRange.levelCount     = mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    VkPipelineStageFlags vkSrcStage = (VkPipelineStageFlags)srcStage;
    if (vkSrcStage == 0) vkSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    
    VkPipelineStageFlags vkDstStage = (VkPipelineStageFlags)dstStage;
    if (vkDstStage == 0) vkDstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (cmd == VK_NULL_HANDLE) {
        Msg("! VK FATAL ERROR: cmd is VK_NULL_HANDLE in DoTransition!");
    }
    if (image == VK_NULL_HANDLE) {
        Msg("! VK FATAL ERROR: image is VK_NULL_HANDLE in DoTransition!");
    }

    //VK_DBG("DoTransition: cmd=%p, image=%p, oldLayout=%d, newLayout=%d", (void*)cmd, (void*)image, oldLayout, newLayout);

    vkCmdPipelineBarrier(
        cmd,
        vkSrcStage, vkDstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void vk_TransitionImages(
    VkCommandBuffer             cmd,
    const VkImageMemoryBarrier* barriers,
    uint32_t                    count,
    VkPipelineStageFlags2       srcStage,
    VkPipelineStageFlags2       dstStage)
{
    if (count == 0 || barriers == nullptr) return;
    if (cmd == VK_NULL_HANDLE) {
        Msg("! VK FATAL ERROR: cmd is VK_NULL_HANDLE in vk_TransitionImages!");
        return;
    }

    VkPipelineStageFlags vkSrcStage = (VkPipelineStageFlags)srcStage;
    if (vkSrcStage == 0) vkSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    
    VkPipelineStageFlags vkDstStage = (VkPipelineStageFlags)dstStage;
    if (vkDstStage == 0) vkDstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    vkCmdPipelineBarrier(
        cmd,
        vkSrcStage, vkDstStage,
        0,
        0, nullptr,
        0, nullptr,
        count, barriers
    );

    /* 
    // TODO (Future Upgrade): Switch back to vkCmdPipelineBarrier2 when a proper
    // dynamic Vulkan loader (like Volk) is integrated into the engine to prevent 
    // function pointer access violations across different graphics drivers.
    
    VkImageMemoryBarrier2 barrier2{};
    barrier2.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier2.srcStageMask                    = srcStage;
    barrier2.srcAccessMask                   = srcAccess;
    barrier2.dstStageMask                    = dstStage;
    barrier2.dstAccessMask                   = dstAccess;
    barrier2.oldLayout                       = oldLayout;
    barrier2.newLayout                       = newLayout;
    barrier2.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier2.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier2.image                           = image;
    barrier2.subresourceRange.aspectMask     = aspect;
    barrier2.subresourceRange.baseMipLevel   = baseMip;
    barrier2.subresourceRange.levelCount     = mipCount;
    barrier2.subresourceRange.baseArrayLayer = 0;
    barrier2.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier2;
    vkCmdPipelineBarrier2(cmd, &dep);
    */
}

// ─── Single-mip (original) ───────────────────────────────────────────────────
void vk_TransitionImageLayout(
    VkCommandBuffer       cmd,
    VkImage               image,
    VkImageAspectFlags    aspect,
    VkImageLayout         oldLayout,
    VkImageLayout         newLayout,
    VkPipelineStageFlags2 srcStage,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2        srcAccess,
    VkAccessFlags2        dstAccess)
{
    DoTransition(cmd, image, aspect, oldLayout, newLayout,
                 srcStage, dstStage, srcAccess, dstAccess, 0, 1);
}

// ─── Multi-mip overload ───────────────────────────────────────────────────────
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
    uint32_t              mipCount)
{
    DoTransition(cmd, image, aspect, oldLayout, newLayout,
                 srcStage, dstStage, srcAccess, dstAccess, baseMip, mipCount);
}

