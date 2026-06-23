// Stateful abstraction and Draw Dispatcher
/*vk_Backend.cpp: This handles overriding the drawing methods. The engine forces global macros to call RCache.Render().
Inside this file, you intercept the indices, vertex counts, and textures that X-Ray has queued up,
and marshal them into actual Vulkan buffer streams right before invoking a draw.

This is the execution powerhouse. When X-Ray calls Render or RenderIndexed, it expects the driver to draw immediately.
This file translates X-Ray's topology calculations into Vulkan commands and matches the structural primitive counts.*/

#include "stdafx.h"
#include "vk_Backend.h"

// Instantiate the global rendering subsystem backend
CBackend vk_Backend;

CBackend::CBackend()
    : m_activeCmdBuffer(VK_NULL_HANDLE)
    , m_currentImageIndex(0)
    , m_boundIndexStream(VK_NULL_HANDLE)
    , m_boundIndexOffset(0)
    , m_boundIndexType(VK_INDEX_TYPE_UINT16)
{
    // Clear standard stream registers safely on initialization
    Memory.mem_fill(m_boundVertexStreams, 0, sizeof(m_boundVertexStreams));
}

CBackend::~CBackend()
{
}

void CBackend::OnFrameBegin(VkCommandBuffer cmdBuffer, u32 imageIndex)
{
    m_activeCmdBuffer   = cmdBuffer;
    m_currentImageIndex = imageIndex;
}

void CBackend::OnFrameEnd()
{
    m_activeCmdBuffer   = VK_NULL_HANDLE;
    m_boundIndexStream  = VK_NULL_HANDLE;
}

void CBackend::SetVertexStream(u32 streamSlot, VkBuffer buffer, VkDeviceSize offset, u32 stride)
{
    if (streamSlot >= 4) return;
    m_boundVertexStreams[streamSlot].VBuffer = buffer;
    m_boundVertexStreams[streamSlot].Offset  = offset;
    m_boundVertexStreams[streamSlot].Stride  = stride;
}

void CBackend::SetIndexStream(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
    m_boundIndexStream = buffer;
    m_boundIndexOffset = offset;
    m_boundIndexType   = indexType;
}

// Handles non-indexed geometry rendering (e.g., screen space quads, particle emitters)
void CBackend::Render(u32 vertexCount, u32 primitiveCount)
{
    R_ASSERT(m_activeCmdBuffer != VK_NULL_HANDLE);

    // 1. Resolve pipeline signature using your vk_PipelineCache layout manager
    // VkPipeline activePipeline = vk_PipelineCache.BindCurrentState(m_activeCmdBuffer);
    // vkCmdBindPipeline(m_activeCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    // 2. Bind active vertex buffers arrays gathered in our slots
    if (m_boundVertexStreams[0].VBuffer != VK_NULL_HANDLE)
    {
        vkCmdBindVertexBuffers(m_activeCmdBuffer, 0, 1,
                               &m_boundVertexStreams[0].VBuffer,
                               &m_boundVertexStreams[0].Offset);
    }

    // 3. Dispatch stateless hardware drawing execution command
    vkCmdDraw(m_activeCmdBuffer, vertexCount, 1, 0, 0);
}

// Handles structural static mesh rendering (e.g., Level meshes, Stalker character hierarchies)
void CBackend::RenderIndexed(u32 indexCount, u32 primitiveCount, u32 baseVertex)
{
    R_ASSERT(m_activeCmdBuffer != VK_NULL_HANDLE);
    R_ASSERT(m_boundIndexStream != VK_NULL_HANDLE);

    // 1. Bind structural mesh vertex storage
    if (m_boundVertexStreams[0].VBuffer != VK_NULL_HANDLE)
    {
        vkCmdBindVertexBuffers(m_activeCmdBuffer, 0, 1,
                               &m_boundVertexStreams[0].VBuffer,
                               &m_boundVertexStreams[0].Offset);
    }

    // 2. Bind hardware index stream reference layout
    vkCmdBindIndexBuffer(m_activeCmdBuffer, m_boundIndexStream, m_boundIndexOffset, m_boundIndexType);

    // 3. Dispatch indexed drawing execution command to the graphics pipeline context
    vkCmdDrawIndexed(m_activeCmdBuffer, indexCount, 1, 0, baseVertex, 0);
}
