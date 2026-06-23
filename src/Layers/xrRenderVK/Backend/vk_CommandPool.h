// Command buffer lifecycles
/* Vulkan command pools are explicitly non-thread-safe. This file scopes and manages the lifespan of your
VkCommandPool, ensuring that when a frame boundary is hit, the pool can be cleared en masse without incurring micro-allocations. */

#pragma once
#include <vulkan/vulkan.h>

class vk_CommandPoolManager
{
private:
    VkCommandPool m_pool;
    VkDevice      m_device;

public:
    vk_CommandPoolManager() : m_pool(VK_NULL_HANDLE), m_device(VK_NULL_HANDLE) {}
    ~vk_CommandPoolManager() { Destroy(); }

    void Initialize(VkDevice device, uint32_t queueFamilyIndex)
    {
        m_device = device;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        // Allows individual command buffers allocated from this pool to be reset/re-recorded
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult res = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_pool);
        R_ASSERT2(res == VK_SUCCESS, "vk_CommandPoolManager — Failed to create command pool");
    }

    void Destroy()
    {
        if (m_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device, m_pool, nullptr);
            m_pool = VK_NULL_HANDLE;
        }
    }

    VkCommandBuffer AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_pool;
        allocInfo.level              = level;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        VkResult res = vkAllocateCommandBuffers(m_device, &allocInfo, &cmdBuffer);
        R_ASSERT2(res == VK_SUCCESS, "vk_CommandPoolManager — Failed to allocate command buffer");
        return cmdBuffer;
    }

    VkCommandPool GetPool() const { return m_pool; }
};
