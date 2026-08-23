#include "stdafx.h"
#include "vk_DescriptorManager.h"
#include "vk_MemoryManager.h"

vk_DescriptorManager DescriptorManager;

vk_DescriptorManager::vk_DescriptorManager()
    : m_device(VK_NULL_HANDLE)
    , m_currentFrameIndex(0)
    , m_dynamicAlignment(256)
    , m_currentBufferOffset(0)
    , m_maxBufferSize(1024 * 1024 * 16) // 16 MB per frame for constants
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        m_descriptorPools[i]       = VK_NULL_HANDLE;
        m_dynamicUniformBuffer[i]  = VK_NULL_HANDLE;
        m_dynamicUniformAlloc[i]   = VK_NULL_HANDLE;  // FIX 2.1: VmaAllocation
        m_dynamicUniformMapped[i]  = nullptr;
    }
}

vk_DescriptorManager::~vk_DescriptorManager()
{
    Destroy();
}

void vk_DescriptorManager::Initialize(VkDevice device, VkPhysicalDevice physDevice)
{
    m_device = device;
    CreateDescriptorPools();
    CreateDynamicUniformBuffers(physDevice);
}

void vk_DescriptorManager::Destroy()
{
    if (m_device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_descriptorPools[i] != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_device, m_descriptorPools[i], nullptr);
            m_descriptorPools[i] = VK_NULL_HANDLE;
        }

        // FIX 2.1: Destroy via VMA instead of vkDestroyBuffer + vkFreeMemory
        if (m_dynamicUniformBuffer[i] != VK_NULL_HANDLE)
        {
            MemoryManager.DestroyBuffer(m_dynamicUniformBuffer[i], m_dynamicUniformAlloc[i]);
            m_dynamicUniformBuffer[i] = VK_NULL_HANDLE;
            m_dynamicUniformAlloc[i]  = VK_NULL_HANDLE;
            m_dynamicUniformMapped[i] = nullptr;
        }
    }
    m_device = VK_NULL_HANDLE;
}

void vk_DescriptorManager::CreateDescriptorPools()
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         65000  },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 65000  }, // set-0 binding 0 is now _DYNAMIC (Phase 13.C)
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65000  }, // kept for shaders that emit OpTypeSampledImage
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          65000  }, // separate image  (OpTypeImage in UniformConstant)
        { VK_DESCRIPTOR_TYPE_SAMPLER,                65000  }, // separate sampler (OpTypeSampler)
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = 65000;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
        vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPools[i]);
}

void vk_DescriptorManager::CreateDynamicUniformBuffers(VkPhysicalDevice physDevice)
{
    // Query alignment requirement from physical device
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDevice, &props);
    m_dynamicAlignment = props.limits.minUniformBufferOffsetAlignment;

    m_dynamicAlignment = std::max(
        props.limits.minUniformBufferOffsetAlignment,
        (VkDeviceSize)256);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = m_maxBufferSize;
    bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VmaAllocationInfo allocInfo{};
        VkResult res = MemoryManager.CreateBuffer(
            &bufferInfo, &allocCI,
            &m_dynamicUniformBuffer[i],
            &m_dynamicUniformAlloc[i],
            &allocInfo);

        if (res == VK_SUCCESS)
            m_dynamicUniformMapped[i] = allocInfo.pMappedData;
        else
            Msg("! vk_DescriptorManager: Failed to create dynamic uniform buffer[%u] (%d)", i, (int)res);
    }
}

void vk_DescriptorManager::BeginFrame(uint32_t frameIndex)
{
    m_currentFrameIndex   = frameIndex;
    vkResetDescriptorPool(m_device, m_descriptorPools[m_currentFrameIndex], 0);
    m_setCache[m_currentFrameIndex].clear();
    m_currentBufferOffset = 0;
}

VkDescriptorSet vk_DescriptorManager::AllocateDescriptorSet(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPools[m_currentFrameIndex];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
    return descriptorSet;
}

void* vk_DescriptorManager::AllocateDynamicUniform(size_t size, uint32_t& outOffset)
{
    size_t alignedSize = (size + m_dynamicAlignment - 1) & ~(m_dynamicAlignment - 1);
    if (m_currentBufferOffset + alignedSize > m_maxBufferSize)
    {
        Msg("! vk_DescriptorManager: Dynamic uniform ring buffer exhausted this frame");
        return nullptr;
    }

    outOffset  = static_cast<uint32_t>(m_currentBufferOffset);
    void* ptr  = static_cast<uint8_t*>(m_dynamicUniformMapped[m_currentFrameIndex]) + m_currentBufferOffset;
    m_currentBufferOffset += alignedSize;
    return ptr;
}

VkDescriptorSet vk_DescriptorManager::FindCachedSet(uint64_t hash) const
{
    auto it = m_setCache[m_currentFrameIndex].find(hash);
    if (it != m_setCache[m_currentFrameIndex].end())
        return it->second;
    return VK_NULL_HANDLE;
}

void vk_DescriptorManager::InsertCachedSet(uint64_t hash, VkDescriptorSet set)
{
    m_setCache[m_currentFrameIndex][hash] = set;
}
