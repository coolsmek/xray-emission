// vk_ResourceManager.h — Asset Lifecycle Tracking for xrRenderVK
//
// Owns all VkBuffer allocations created during level load (vertex + index buffers).
// Returns opaque ID3DVertexBuffer* / ID3DIndexBuffer* handles compatible with the
// CBackend::set_Vertices / set_Indices path in vkR_Backend_Runtime.h.
// All allocations are freed together by FreeAll() during level_Unload().
#pragma once
#include "stdafx.h"
#include <vulkan/vulkan.h>
#include "../Managers/vk_MemoryManager.h"   // VmaAllocation

class vk_ResourceManager
{
public:
    // ── Geometry buffer allocation ────────────────────────────────────────────
    // Allocates a device-local vertex buffer, uploads srcData, and returns a
    // heap-allocated VkBufferWrapper* (aliased as ID3DVertexBuffer*) ready for
    // vkCmdBindVertexBuffers.  Returns nullptr on failure.
    ID3DVertexBuffer* CreateVertexBuffer(const void* srcData, VkDeviceSize byteSize);

    // Allocates a device-local index buffer, uploads srcData.
    // X-Ray level geometry always uses 16-bit (UINT16) indices.
    ID3DIndexBuffer*  CreateIndexBuffer (const void* srcData, VkDeviceSize byteSize);

    // Dynamic geometry streams (persistently mapped host-visible buffers)
    ID3DVertexBuffer* CreateDynamicVertexBuffer(VkDeviceSize byteSize);
    ID3DIndexBuffer*  CreateDynamicIndexBuffer(VkDeviceSize byteSize);
    void              DestroyDynamicBuffer(void* opaqueWrapper);

private:

    // Internal helper — allocates device-local buffer, uploads data, records
    // tracking entry, sets wrapper->buffer.  Returns nullptr on any failure.
    ID3DVertexBuffer* AllocAndUpload(const void* srcData, VkDeviceSize byteSize,
                                     VkBufferUsageFlags extraUsage);
};

extern vk_ResourceManager ResourceManager;
