#include "stdafx.h"
#include "vk_DescriptorManager.h"
#include "vk_MemoryManager.h"
#include "../vk_DiagTimer.h"
#include "../../xrCPU_Pipe/ttapi.h"

vk_DescriptorManager DescriptorManager;
extern thread_local u32 g_vkWorkerId;

vk_DescriptorManager::vk_DescriptorManager()
    : m_device(VK_NULL_HANDLE)
    , m_currentFrameIndex(0)
    , m_dynamicAlignment(256)
    , m_maxBufferSize(1024 * 1024 * 64) // 64 MB per frame for constants
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
    {
        for (uint32_t c = 0; c < kMaxConcurrentContexts; ++c)
            m_descriptorPools[i][c] = VK_NULL_HANDLE;
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
        for (uint32_t c = 0; c < kMaxConcurrentContexts; ++c)
        {
            if (m_descriptorPools[i][c] != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_device, m_descriptorPools[i][c], nullptr);
                m_descriptorPools[i][c] = VK_NULL_HANDLE;
            }
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
    constexpr uint32_t kPrimaryMaxSets = 65000;  // matches original pre-split single-context budget
    constexpr uint32_t kWorkerMaxSets  = 65000 / (kMaxConcurrentContexts - 1); // ~4333/worker — proven sufficient, peaked ~810/worker in Steps1-3 validation

    for (uint32_t f = 0; f < VK_MAX_FRAMES_IN_FLIGHT; ++f)
    {
        for (uint32_t c = 0; c < kMaxConcurrentContexts; ++c)
        {
            const uint32_t maxSets = (c == 0) ? kPrimaryMaxSets : kWorkerMaxSets;
            VkDescriptorPoolSize poolSizes[] = {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         maxSets },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          maxSets },
                { VK_DESCRIPTOR_TYPE_SAMPLER,                maxSets },
            };
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
            poolInfo.pPoolSizes    = poolSizes;
            poolInfo.maxSets       = maxSets;
            poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPools[f][c]);
        }
    }
}

void vk_DescriptorManager::CreateDynamicUniformBuffers(VkPhysicalDevice physDevice)
{
    m_maxBufferSize = kPrimaryReserveBytes + kPerWorkerReserveBytes * CHW::VK_GBUFFER_WORKERS;

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

    // Context 0's reserve is fixed/constant — never depends on -vk_mt_record or
    // ttapi readiness, so seed it immediately rather than waiting on
    // EnsureContextLayout() (which is deferred to first BeginFrame() because the
    // *worker* slice split does need Core.Params/ttapi to be ready). Without this,
    // any code path that calls AllocateDynamicUniform before the first real
    // BeginFrame() (e.g. the VK unit-test suite) sees slot 0 sized to zero and
    // silently fails.
    m_sliceBase[0]     = 0;
    m_sliceCapacity[0] = kPrimaryReserveBytes;
    m_sliceOffset[0]   = 0;
}

void vk_DescriptorManager::EnsureContextLayout()
{
    if (m_contextLayoutReady) return;

    m_mtRecordConfigured = strstr(Core.Params, "-vk_mt_record") != nullptr;
    const u32 hwWorkers = ttapi_GetWorkersCount();
    u32 cap = m_mtRecordConfigured ? _min(hwWorkers, CHW::VK_GBUFFER_WORKERS) : 0;
    // Mirrors rVK.cpp's `canUseMT` gate (maxWorkers >= 2) exactly — MUST stay in
    // sync with that threshold, or a context could be sized for MT participation
    // it will never actually get.
    if (cap < 2) cap = 0;
    m_activeWorkerCap = cap;

    m_sliceBase[0]     = 0;
    m_sliceCapacity[0] = kPrimaryReserveBytes;
    m_sliceOffset[0]   = 0;

    // Worker region is a fixed physical allocation (worst case: VK_GBUFFER_WORKERS
    // slots), but SPLIT only among the session's actually-configured cap — so
    // -max-threads N < VK_GBUFFER_WORKERS gives each active worker a bigger slice
    // instead of leaving unused ones idle.
    const size_t workerRegionTotal = kPerWorkerReserveBytes * CHW::VK_GBUFFER_WORKERS;
    const size_t perActiveWorker = (m_activeWorkerCap > 0)
        ? (workerRegionTotal / m_activeWorkerCap) & ~(m_dynamicAlignment - 1)
        : 0;

    for (uint32_t i = 1; i < kMaxConcurrentContexts; ++i)
    {
        if (i <= m_activeWorkerCap)
        {
            m_sliceBase[i]     = kPrimaryReserveBytes + (size_t)(i - 1) * perActiveWorker;
            m_sliceCapacity[i] = perActiveWorker;
        }
        else
        {
            // Structurally never indexed (workerCount this session can't exceed
            // m_activeWorkerCap), zeroed defensively rather than left stale.
            m_sliceBase[i]     = 0;
            m_sliceCapacity[i] = 0;
        }
        m_sliceOffset[i] = 0;
    }

    m_contextLayoutReady = true;
    Msg("* vk_DescriptorManager: layout - mtConfigured=%d activeWorkerCap=%u perWorker=%uMB",
        (int)m_mtRecordConfigured, m_activeWorkerCap, (u32)(perActiveWorker / (1024*1024)));
}

void vk_DescriptorManager::BeginFrame(uint32_t frameIndex)
{
    EnsureContextLayout();   // one-time; no-op after the first call

    // Main-thread-only, strictly before any worker touches the manager this frame.
    m_currentFrameIndex = frameIndex;
    for (uint32_t c = 0; c < kMaxConcurrentContexts; ++c)
        vkResetDescriptorPool(m_device, m_descriptorPools[m_currentFrameIndex][c], 0);

    for (uint32_t s = 0; s < kCacheShards; ++s)
        m_setCache[m_currentFrameIndex][s].clear();

    for (uint32_t i = 0; i < kMaxConcurrentContexts; ++i)
        m_sliceOffset[i] = 0;   // reclaim each context's slice; base/capacity unchanged
}

VkDescriptorSet vk_DescriptorManager::AllocateDescriptorSet(VkDescriptorSetLayout layout)
{
    const uint32_t slot = g_vkWorkerId;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPools[m_currentFrameIndex][slot];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
    return descriptorSet;
}

void* vk_DescriptorManager::AllocateDynamicUniform(size_t size, uint32_t& outOffset)
{
    if (RCache.m_ctx) RCache.m_ctx->diag_uniformCalls++;

    const uint32_t slot = g_vkWorkerId;
    R_ASSERT2(slot < kMaxConcurrentContexts, "g_vkWorkerId out of range for descriptor ring slicing");

    const size_t alignedSize = (size + m_dynamicAlignment - 1) & ~(m_dynamicAlignment - 1);
    if (m_sliceOffset[slot] + alignedSize > m_sliceCapacity[slot])
    {
        static std::atomic<bool> s_warned[kMaxConcurrentContexts] = {};
        if (!s_warned[slot].exchange(true))
            Msg("! vk_DescriptorManager: dynamic uniform slice exhausted for context %u this frame", slot);
        return nullptr;
    }

    const size_t localOffset = m_sliceOffset[slot];
    outOffset = static_cast<uint32_t>(m_sliceBase[slot] + localOffset);
    void* ptr = static_cast<uint8_t*>(m_dynamicUniformMapped[m_currentFrameIndex]) + m_sliceBase[slot] + localOffset;
    m_sliceOffset[slot] += alignedSize;
    return ptr;
}

VkDescriptorSet vk_DescriptorManager::FindCachedSet(uint64_t hash) const
{
    if (RCache.m_ctx) RCache.m_ctx->diag_descCalls++;
    const uint32_t shard = hash % kCacheShards;
    u64* w = RCache.m_ctx ? &RCache.m_ctx->diag_lockWaitTicks_desc : nullptr;
    { 
        vk_ScopedLockTimer lt(w); 
        std::lock_guard<std::mutex> lock(m_cacheMutex[shard]); 
        auto& cache = m_setCache[m_currentFrameIndex][shard];
        auto it = cache.find(hash);
        return it != cache.end() ? it->second : VK_NULL_HANDLE;
    }
}

void vk_DescriptorManager::InsertCachedSet(uint64_t hash, VkDescriptorSet set)
{
    if (RCache.m_ctx) RCache.m_ctx->diag_descCalls++;
    const uint32_t shard = hash % kCacheShards;
    u64* w = RCache.m_ctx ? &RCache.m_ctx->diag_lockWaitTicks_desc : nullptr;
    vk_ScopedLockTimer lt(w);
    std::lock_guard<std::mutex> lock(m_cacheMutex[shard]);
    m_setCache[m_currentFrameIndex][shard][hash] = set;
}
