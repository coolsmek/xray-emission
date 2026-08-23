// Per-frame ring buffer synchronizations
/* This file replaces the traditional D3D implicit synchronization. It groups all resources that are
altered on a per-frame basis (like command buffers, dynamic uniform buffer memory, and swapchain image
availability semaphores) into a clean, trackable structure. */

#pragma once
#include <vulkan/vulkan.h>

// Tracks resources tied to a specific in-flight frame boundary.
// Prevents the CPU from overwriting memory the GPU is currently reading.
struct vk_FrameContext
{
    VkCommandBuffer     CommandBuffer;
    VkFence             InFlightFence;
    VkSemaphore         ImageAvailableSemaphore;
    VkSemaphore         RenderFinishedSemaphore;

    // Optional extension paths for structural safety:
    // VkDescriptorPool DynamicDescriptorPool; // For per-frame uniform bindings
    // vk_RingBuffer     DynamicConstantMemory; // Dynamic uniform allocations
};

// Global frame context ring array
extern vk_FrameContext GFrameContexts[CHW::MAX_FRAMES_IN_FLIGHT];
