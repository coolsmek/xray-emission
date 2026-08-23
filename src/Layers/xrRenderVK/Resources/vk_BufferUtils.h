// Vertex/Index/Uniform Buffer helpers backed by VMA (vk_MemoryManager)
#pragma once
#include <vulkan/vulkan.h>
#include "../Managers/vk_MemoryManager.h"   // for VmaAllocation
#include "../xrRender/xrD3DDefs.h"          // D3DVERTEXELEMENT9 full definition

// Returns the byte size of a D3DDECLTYPE value (matches the D3DDECLTYPE enum).
inline uint32_t D3DDeclTypeSize(uint8_t type)
{
    switch (type) {
    case 0:  return 4;   // FLOAT1
    case 1:  return 8;   // FLOAT2
    case 2:  return 12;  // FLOAT3
    case 3:  return 16;  // FLOAT4
    case 4:  return 4;   // D3DCOLOR
    case 5:  return 4;   // UBYTE4
    case 6:  return 4;   // SHORT2
    case 7:  return 8;   // SHORT4
    case 8:  return 4;   // UBYTE4N
    case 9:  return 4;   // SHORT2N
    case 10: return 8;   // SHORT4N
    case 11: return 4;   // USHORT2N
    case 12: return 8;   // USHORT4N
    case 13: return 4;   // UDEC3
    case 14: return 4;   // DEC3N
    case 15: return 4;   // FLOAT16_2
    case 16: return 8;   // FLOAT16_4
    default: return 0;
    }
}

// Returns the number of elements in a D3DVERTEXELEMENT9 array (not counting
// the D3DDECL_END sentinel whose Stream == 0xFF).
uint32_t vk_GetDeclLength(const D3DVERTEXELEMENT9* dcl);

// Returns the total stride in bytes for the given stream index, computed as
// max(element.Offset + elementSize) across all elements in that stream.
// Pass stream=0 for the common single-stream case.
uint32_t vk_GetDeclVertexSize(const D3DVERTEXELEMENT9* dcl, uint16_t stream = 0);

// ── Device-local buffer ───────────────────────────────────────────────────────
// Creates a device-local (VRAM) buffer.  Fill it by calling vk_UploadBuffer.
// usage     : VkBufferUsageFlags — e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT.
//             VK_BUFFER_USAGE_TRANSFER_DST_BIT is added automatically.
VkResult vk_CreateDeviceLocalBuffer(
    VkDeviceSize       size,
    VkBufferUsageFlags usage,
    VkBuffer&          outBuffer,
    VmaAllocation&     outAlloc);

// ── Host-visible staging buffer ───────────────────────────────────────────────
// Creates a persistently-mapped, host-coherent staging buffer.
// The mapped pointer is valid until vk_DestroyBuffer is called.
VkResult vk_CreateStagingBuffer(
    VkDeviceSize   size,
    VkBuffer&      outBuffer,
    VmaAllocation& outAlloc,
    void*&         outMappedPtr);

// ── One-shot upload ────────────────────────────────────────────────────────────
// Copies srcData -> dstBuffer via a temporary staging buffer + one-shot submit.
// Blocks until the transfer queue is idle (safe for load-time use).
// Returns true on success, false on any error (staging alloc, cmd alloc, submit).
bool vk_UploadBuffer(
    const void*  srcData,
    VkDeviceSize byteSize,
    VkBuffer     dstBuffer);

// ── Destroy ───────────────────────────────────────────────────────────────────
void vk_DestroyBuffer(VkBuffer buffer, VmaAllocation alloc);

// ── Transfer pool lifetime ─────────────────────────────────────────────────────
// Call once during device teardown to release the internal transfer command pool.


