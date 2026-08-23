#pragma once

// Configure VMA to use static Vulkan function pointers linked via vulkan-1.lib
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#endif

#include <vma/vk_mem_alloc.h>

class vk_MemoryManager
{
private:
    VmaAllocator m_allocator;
    VkDevice     m_device;

public:
    vk_MemoryManager();
    ~vk_MemoryManager();

    void Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    void Destroy();

    VkResult CreateBuffer(
        const VkBufferCreateInfo* bufferInfo,
        const VmaAllocationCreateInfo* allocInfo,
        VkBuffer* buffer,
        VmaAllocation* allocation,
        VmaAllocationInfo* allocationInfo = nullptr);

    void DestroyBuffer(VkBuffer buffer, VmaAllocation allocation);

    VkResult CreateImage(
        const VkImageCreateInfo* imageInfo,
        const VmaAllocationCreateInfo* allocInfo,
        VkImage* image,
        VmaAllocation* allocation,
        VmaAllocationInfo* allocationInfo = nullptr);

    void DestroyImage(VkImage image, VmaAllocation allocation);

    VkResult MapMemory(VmaAllocation allocation, void** ppData);
    void UnmapMemory(VmaAllocation allocation);

    VkResult FlushAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size);
    VkResult InvalidateAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size);

    VmaAllocator GetAllocator() const { return m_allocator; }
};

extern vk_MemoryManager MemoryManager;
