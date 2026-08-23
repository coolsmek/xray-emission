#include "stdafx.h"

// VMA_IMPLEMENTATION is compiled separately in vk_vma_impl.cpp (no-PCH TU).
// This file only contains the vk_MemoryManager class method bodies.
#include "vk_MemoryManager.h"

vk_MemoryManager MemoryManager;

vk_MemoryManager::vk_MemoryManager()
    : m_allocator(VK_NULL_HANDLE)
    , m_device(VK_NULL_HANDLE)
{
}

vk_MemoryManager::~vk_MemoryManager()
{
    Destroy();
}

void vk_MemoryManager::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
{
    m_device = device;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4; // API version tested in vk_TestHW (Vulkan 1.4)
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;

    VkResult res = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    R_ASSERT2(res == VK_SUCCESS, "vk_MemoryManager::Initialize — vmaCreateAllocator failed");
}

void vk_MemoryManager::Destroy()
{
    if (m_allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

VkResult vk_MemoryManager::CreateBuffer(
    const VkBufferCreateInfo* bufferInfo,
    const VmaAllocationCreateInfo* allocInfo,
    VkBuffer* buffer,
    VmaAllocation* allocation,
    VmaAllocationInfo* allocationInfo)
{
    return vmaCreateBuffer(m_allocator, bufferInfo, allocInfo, buffer, allocation, allocationInfo);
}

void vk_MemoryManager::DestroyBuffer(VkBuffer buffer, VmaAllocation allocation)
{
    // Guard against late global-destructor calls that arrive after Destroy().
    if (m_allocator == VK_NULL_HANDLE) return;
    vmaDestroyBuffer(m_allocator, buffer, allocation);
}

VkResult vk_MemoryManager::CreateImage(
    const VkImageCreateInfo* imageInfo,
    const VmaAllocationCreateInfo* allocInfo,
    VkImage* image,
    VmaAllocation* allocation,
    VmaAllocationInfo* allocationInfo)
{
    return vmaCreateImage(m_allocator, imageInfo, allocInfo, image, allocation, allocationInfo);
}

void vk_MemoryManager::DestroyImage(VkImage image, VmaAllocation allocation)
{
    if (m_allocator == VK_NULL_HANDLE) return;
    vmaDestroyImage(m_allocator, image, allocation);
}

VkResult vk_MemoryManager::MapMemory(VmaAllocation allocation, void** ppData)
{
    return vmaMapMemory(m_allocator, allocation, ppData);
}

void vk_MemoryManager::UnmapMemory(VmaAllocation allocation)
{
    vmaUnmapMemory(m_allocator, allocation);
}

VkResult vk_MemoryManager::FlushAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{
    return vmaFlushAllocation(m_allocator, allocation, offset, size);
}

VkResult vk_MemoryManager::InvalidateAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{
    return vmaInvalidateAllocation(m_allocator, allocation, offset, size);
}
