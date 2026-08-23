// vk_ResourceManager.cpp — Asset Lifecycle Tracking implementation
#include "stdafx.h"
#include "vk_ResourceManager.h"
#include "vk_BufferUtils.h"

vk_ResourceManager ResourceManager;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper
// ─────────────────────────────────────────────────────────────────────────────
ID3DVertexBuffer* vk_ResourceManager::AllocAndUpload(
    const void*        srcData,
    VkDeviceSize       byteSize,
    VkBufferUsageFlags extraUsage)
{
    if (!srcData || byteSize == 0)
        return nullptr;

    VkBuffer      buf   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;

    VkResult res = vk_CreateDeviceLocalBuffer(byteSize, extraUsage, buf, alloc);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_ResourceManager: failed to create device-local buffer (%d)", (int)res);
        return nullptr;
    }

    if (!vk_UploadBuffer(srcData, byteSize, buf))
    {
        Msg("! vk_ResourceManager: upload failed, releasing buffer");
        vk_DestroyBuffer(buf, alloc);
        return nullptr;
    }

    // Heap-allocate a VkBufferWrapper and set its buffer handle.
    // VkBufferWrapper is typedef'd as both ID3DVertexBuffer and ID3DIndexBuffer.
    VkBufferWrapper* wrapper = xr_new<VkBufferWrapper>();
    wrapper->buffer  = buf;
    wrapper->memory  = VK_NULL_HANDLE;  // memory owned by VMA via alloc
    wrapper->size    = (size_t)byteSize;

    // No longer tracking globally

    return static_cast<ID3DVertexBuffer*>(wrapper);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
ID3DVertexBuffer* vk_ResourceManager::CreateVertexBuffer(const void* srcData, VkDeviceSize byteSize)
{
    return AllocAndUpload(srcData, byteSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

ID3DIndexBuffer* vk_ResourceManager::CreateIndexBuffer(const void* srcData, VkDeviceSize byteSize)
{
    // Cast is safe: ID3DIndexBuffer is the same typedef as ID3DVertexBuffer (both VkBufferWrapper)
    return static_cast<ID3DIndexBuffer*>(
        AllocAndUpload(srcData, byteSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic API
// ─────────────────────────────────────────────────────────────────────────────
ID3DVertexBuffer* vk_ResourceManager::CreateDynamicVertexBuffer(VkDeviceSize byteSize)
{
    if (byteSize == 0) return nullptr;

    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;

    // Use staging buffer as dynamic host-visible buffer (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    // Wait, vk_CreateStagingBuffer hardcodes usage to VK_BUFFER_USAGE_TRANSFER_SRC_BIT!
    // We should create it manually here to include VERTEX_BUFFER_BIT.
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResultInfo{};
    if (vmaCreateBuffer(MemoryManager.GetAllocator(), &bufInfo, &allocInfo, &buf, &alloc, &allocResultInfo) != VK_SUCCESS)
    {
        return nullptr;
    }
    mappedPtr = allocResultInfo.pMappedData;
    R_ASSERT2(mappedPtr != nullptr, "VMA failed to map Dynamic Vertex Buffer! Check VMA usage flags.");

    VkMemoryPropertyFlags memFlags = 0;
    vmaGetAllocationMemoryProperties(MemoryManager.GetAllocator(), alloc, &memFlags);
    Msg("  VK dyn VB: size=%.1fMB deviceLocal=%d hostVisible=%d hostCoherent=%d",
        byteSize / (1024.0*1024.0),
        (memFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 1 : 0,
        (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? 1 : 0,
        (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0);

    VkBufferWrapper* wrapper = xr_new<VkBufferWrapper>();
    wrapper->buffer = buf;
    wrapper->memory = VK_NULL_HANDLE; // Owned by VMA
    wrapper->alloc = alloc;
    wrapper->mapped_ptr = mappedPtr;
    wrapper->size = (size_t)byteSize;

    return static_cast<ID3DVertexBuffer*>(wrapper);
}

ID3DIndexBuffer* vk_ResourceManager::CreateDynamicIndexBuffer(VkDeviceSize byteSize)
{
    if (byteSize == 0) return nullptr;

    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResultInfo{};
    if (vmaCreateBuffer(MemoryManager.GetAllocator(), &bufInfo, &allocInfo, &buf, &alloc, &allocResultInfo) != VK_SUCCESS)
    {
        return nullptr;
    }
    mappedPtr = allocResultInfo.pMappedData;
    R_ASSERT2(mappedPtr != nullptr, "VMA failed to map Dynamic Index Buffer! Check VMA usage flags.");

    VkMemoryPropertyFlags memFlags = 0;
    vmaGetAllocationMemoryProperties(MemoryManager.GetAllocator(), alloc, &memFlags);
    Msg("  VK dyn IB: size=%.1fMB deviceLocal=%d hostVisible=%d hostCoherent=%d",
        byteSize / (1024.0*1024.0),
        (memFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 1 : 0,
        (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? 1 : 0,
        (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0);

    VkBufferWrapper* wrapper = xr_new<VkBufferWrapper>();
    wrapper->buffer = buf;
    wrapper->memory = VK_NULL_HANDLE;
    wrapper->alloc = alloc;
    wrapper->mapped_ptr = mappedPtr;
    wrapper->size = (size_t)byteSize;

    return static_cast<ID3DIndexBuffer*>(wrapper);
}

void vk_ResourceManager::DestroyDynamicBuffer(void* opaqueWrapper)
{
    if (!opaqueWrapper) return;
    VkBufferWrapper* wrapper = static_cast<VkBufferWrapper*>(opaqueWrapper);
    vk_DestroyBuffer(wrapper->buffer, static_cast<VmaAllocation>(wrapper->alloc));
    xr_delete(wrapper);
}

unsigned long VkBufferWrapper::Release()
{
    long count = --refcount;
    if (count == 0)
    {
        if (alloc || buffer)
        {
            vk_DestroyBuffer(buffer, static_cast<VmaAllocation>(alloc));
        }
        xr_delete(this);
    }
    return count;
}

int VkDeviceWrapper::CreateBuffer(const D3D_BUFFER_DESC* desc, const void* subData, VkBufferWrapper** ppBuffer)
{
    if (!ppBuffer) return -1;
    const D3D_SUBRESOURCE_DATA* pSub = static_cast<const D3D_SUBRESOURCE_DATA*>(subData);
    const void* pInitData = pSub ? pSub->pSysMem : nullptr;
    VkDeviceSize byteWidth = desc->ByteWidth;
    VkBufferWrapper* buf = nullptr;
    if (desc->BindFlags & D3D_BIND_INDEX_BUFFER)
        buf = ResourceManager.CreateIndexBuffer(pInitData, byteWidth);
    else
        buf = ResourceManager.CreateVertexBuffer(pInitData, byteWidth);
    if (!buf) return -2;
    *ppBuffer = buf;
    return 0;
}
