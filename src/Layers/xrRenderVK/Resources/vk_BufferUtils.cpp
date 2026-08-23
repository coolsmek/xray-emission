#include "stdafx.h"
#include <mutex>
#include "vk_BufferUtils.h"             // pulls in xrD3DDefs.h and vk_MemoryManager.h
#include "../Managers/vk_MemoryManager.h"
#include "../xrRender/dxRenderDeviceRender.h"

// Removed s_transferCmdPool and GetTransferPool since transfers are now queued to the main thread.

// ────────────────────────────────────────────────────────────────────────────────
// vk_CreateDeviceLocalBuffer
// ────────────────────────────────────────────────────────────────────────────────
// Creates a device-local (VRAM) buffer for geometry/constant data.
// Caller must fill it via vk_UploadBuffer (staging + transfer).
//
VkResult vk_CreateDeviceLocalBuffer(
    VkDeviceSize       size,
    VkBufferUsageFlags usage,
    VkBuffer&          outBuffer,
    VmaAllocation&     outAlloc)
{
    // Add VK_BUFFER_USAGE_TRANSFER_DST_BIT so this buffer can receive transfers.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    return MemoryManager.CreateBuffer(&bufferInfo, &allocInfo, &outBuffer, &outAlloc);
}

// ────────────────────────────────────────────────────────────────────────────────
// vk_CreateStagingBuffer
// ────────────────────────────────────────────────────────────────────────────────
// Creates a host-visible staging buffer (CPU-accessible, cached-coherent).
// Returns a persistently-mapped pointer valid until vk_DestroyBuffer() is called.
//
VkResult vk_CreateStagingBuffer(
    VkDeviceSize   size,
    VkBuffer&      outBuffer,
    VmaAllocation& outAlloc,
    void*&         outMappedPtr)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage  = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags  = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                     | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    VkResult res = MemoryManager.CreateBuffer(&bufferInfo, &allocInfo, &outBuffer, &outAlloc, &allocationInfo);
    if (res == VK_SUCCESS)
        outMappedPtr = allocationInfo.pMappedData;
    else
        outMappedPtr = nullptr;

    return res;
}

// ────────────────────────────────────────────────────────────────────────────────
// vk_UploadBuffer
// ────────────────────────────────────────────────────────────────────────────────
// Copies srcData -> dstBuffer via a temporary staging buffer + one-shot command.
// Allocates a staging buffer, records a copy command, submits to graphics queue,
// and waits for completion (safe for load-time use).
//
bool vk_UploadBuffer(
    const void*  srcData,
    VkDeviceSize byteSize,
    VkBuffer     dstBuffer)
{
    if (!srcData || byteSize == 0)
        return false;

    // Create temporary staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    void* stagingPtr;

    VkResult res = vk_CreateStagingBuffer(byteSize, stagingBuffer, stagingAlloc, stagingPtr);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_UploadBuffer: Failed to create staging buffer");
        return false;
    }

    // Copy source data into staging buffer
    CopyMemory(stagingPtr, srcData, byteSize);

    // Queue the transfer for the main thread
    HW.QueueTransfer(
        [=](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size      = byteSize;
            vkCmdCopyBuffer(cmd, stagingBuffer, dstBuffer, 1, &copyRegion);
        },
        [=]() {
            // Queue is already idle (FlushDeferredTransfers did SubmitQueueAndWait),
            // so destroy synchronously — do NOT go through vk_DestroyBuffer, which
            // re-enqueues into m_pendingTransfers and would leak (that entry is never flushed).
            MemoryManager.DestroyBuffer(stagingBuffer, stagingAlloc);
        }
    );

    return true;
}

// ────────────────────────────────────────────────────────────────────────────────
// vk_DestroyBuffer
// ────────────────────────────────────────────────────────────────────────────────
// Thin wrapper over MemoryManager.DestroyBuffer() for consistency.
//
void vk_DestroyBuffer(VkBuffer buffer, VmaAllocation alloc)
{
    if (buffer != VK_NULL_HANDLE)
    {
        HW.QueueTransfer(
            [](VkCommandBuffer){},                                   // no GPU work
            [=]() { MemoryManager.DestroyBuffer(buffer, alloc); }    // runs after all prior copies
        );
    }
}

// ────────────────────────────────────────────────────────────────────────────────
// Vertex declaration helpers
// ────────────────────────────────────────────────────────────────────────────────

uint32_t vk_GetDeclLength(const D3DVERTEXELEMENT9* dcl)
{
    if (!dcl) return 0;
    uint32_t count = 0;
    while (dcl[count].Stream != 0xFF) ++count;
    return count;
}

uint32_t vk_GetDeclVertexSize(const D3DVERTEXELEMENT9* dcl, uint16_t stream)
{
    if (!dcl) return 0;
    uint32_t stride = 0;
    for (uint32_t i = 0; dcl[i].Stream != 0xFF; ++i)
    {
        if (dcl[i].Stream != stream) continue;
        uint32_t end = dcl[i].Offset + D3DDeclTypeSize(dcl[i].Type);
        if (end > stride) stride = end;
    }
    return stride;
}

