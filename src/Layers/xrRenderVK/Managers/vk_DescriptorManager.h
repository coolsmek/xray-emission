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
    static constexpr uint32_t kMaxConcurrentContexts = 16;

    // Context 0 must independently survive a FULL serial frame — either
    // -vk_mt_record is off, or any single frame's scene-size fallback
    // (totalPackets < kMinPacketsPerWorker*2 in rVK.cpp) dips below the MT
    // threshold, both of which funnel 100% of static-geometry recording
    // through context 0 alone. 64MB matches the proven pre-MT single-context
    // budget (m_maxBufferSize before this split existed).
    static constexpr size_t kPrimaryReserveBytes   = 64ull * 1024 * 1024;
    // Proven sufficient per-worker MT share — validated via -vk_mt_diag with
    // no "slice exhausted" warnings across Steps 1-3 testing (4MB/worker at
    // up to ~3300 uniform calls/frame).
    static constexpr size_t kPerWorkerReserveBytes = 4ull  * 1024 * 1024;

    VkDevice m_device;

    // A separate pool for each frame in flight, per context
    VkDescriptorPool m_descriptorPools[VK_MAX_FRAMES_IN_FLIGHT][kMaxConcurrentContexts];
    uint32_t m_currentFrameIndex;

    // FIX 2.1: Dynamic Uniform Buffer — now managed through VMA instead of raw vkAllocateMemory
    VkBuffer     m_dynamicUniformBuffer[VK_MAX_FRAMES_IN_FLIGHT];
    VmaAllocation m_dynamicUniformAlloc[VK_MAX_FRAMES_IN_FLIGHT];  // was VkDeviceMemory
    void*        m_dynamicUniformMapped[VK_MAX_FRAMES_IN_FLIGHT];

    size_t m_dynamicAlignment;
    size_t m_maxBufferSize;

    // FIX 5.x: one exclusive ring-slice per g_vkWorkerId value, so AllocateDynamicUniform
    // never needs m_mutex. Must be >= dx10ConstantBuffer::VK_CB_MAX_WORKERS (16) so every
    // valid g_vkWorkerId maps to a slice.
    size_t m_sliceBase[kMaxConcurrentContexts];
    size_t m_sliceCapacity[kMaxConcurrentContexts];
    size_t m_sliceOffset[kMaxConcurrentContexts];

    bool     m_contextLayoutReady  = false;
    bool     m_mtRecordConfigured  = false;
    uint32_t m_activeWorkerCap     = 0;   // 0 == MT unavailable/disabled this session

    static constexpr uint32_t kCacheShards = 16;
    mutable std::mutex m_cacheMutex[kCacheShards];
    std::unordered_map<uint64_t, VkDescriptorSet> m_setCache[VK_MAX_FRAMES_IN_FLIGHT][kCacheShards];

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
    void EnsureContextLayout();   // one-time: detect config, compute slice base/capacity
    void CreateDescriptorPools();
    void CreateDynamicUniformBuffers(VkPhysicalDevice physDevice);
};

extern vk_DescriptorManager DescriptorManager;
