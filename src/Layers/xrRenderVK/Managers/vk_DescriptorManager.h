// Descriptor Set Pooling and allocation
#pragma once
#include "stdafx.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include "vk_MemoryManager.h"   // FIX 2.1: use VMA for ring buffer allocation

// Maintain a ring buffer of pools matched to the frames in flight
constexpr uint32_t VK_MAX_FRAMES_IN_FLIGHT = CHW::MAX_FRAMES_IN_FLIGHT;

class vk_DescriptorManager
{
private:
    VkDevice m_device;
    std::mutex m_mutex;

    // A separate pool for each frame in flight
    VkDescriptorPool m_descriptorPools[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t m_currentFrameIndex;

    // FIX 2.1: Dynamic Uniform Buffer — now managed through VMA instead of raw vkAllocateMemory
    VkBuffer     m_dynamicUniformBuffer[VK_MAX_FRAMES_IN_FLIGHT];
    VmaAllocation m_dynamicUniformAlloc[VK_MAX_FRAMES_IN_FLIGHT];  // was VkDeviceMemory
    void*        m_dynamicUniformMapped[VK_MAX_FRAMES_IN_FLIGHT];

    size_t m_dynamicAlignment;
    size_t m_currentBufferOffset;
    size_t m_maxBufferSize;

    std::unordered_map<uint64_t, VkDescriptorSet> m_setCache[VK_MAX_FRAMES_IN_FLIGHT];

public:
    vk_DescriptorManager();
    ~vk_DescriptorManager();

    void Initialize(VkDevice device, VkPhysicalDevice physDevice);
    void Destroy();

    // Called at the start of each frame to rotate the ring buffer
    void BeginFrame(uint32_t frameIndex);

    // Allocates a fresh descriptor set from the current frame's pool
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

    // Allocates a chunk of memory from the current frame's Dynamic Uniform Buffer
    // Returns the byte offset within the buffer and a pointer to write data into
    void* AllocateDynamicUniform(size_t size, uint32_t& outOffset);

    // Returns the current dynamic uniform buffer
    VkBuffer GetCurrentDynamicBuffer() const { return m_dynamicUniformBuffer[m_currentFrameIndex]; }

    VkDescriptorSet FindCachedSet(uint64_t hash) const;
    void InsertCachedSet(uint64_t hash, VkDescriptorSet set);

private:
    void CreateDescriptorPools();
    void CreateDynamicUniformBuffers(VkPhysicalDevice physDevice);
};

extern vk_DescriptorManager DescriptorManager;
