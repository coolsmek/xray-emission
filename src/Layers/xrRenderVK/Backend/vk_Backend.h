// Stateful abstraction and Draw Dispatcher
/* This is the strategic header file for intercepting X-Ray's drawings. It needs to expose the primary
structure signatures that the shared engine layers expect when executing functions through the global RCache context wrapper. */

#pragma once
#include "stdafx.h"
#include <vulkan/vulkan.h>

class CBackend
{
public:
    // Structural vectors to store bound geometry stream pointers
    // These correspond to what X-Ray queues up prior to an RCache.Render() call
    struct StreamState
    {
        VkBuffer     VBuffer;
        VkDeviceSize Offset;
        u32          Stride;
    };

private:
    VkCommandBuffer m_activeCmdBuffer;
    u32             m_currentImageIndex;

    // Track active bound pipeline geometry indices to minimize redundant vkCmdBindVertexBuffers calls
    StreamState     m_boundVertexStreams[4];
    VkBuffer        m_boundIndexStream;
    VkDeviceSize    m_boundIndexOffset;
    VkIndexType     m_boundIndexType;

public:
    CBackend();
    ~CBackend();

    // Setup active frame target references before processing geometry
    void OnFrameBegin(VkCommandBuffer cmdBuffer, u32 imageIndex);
    void OnFrameEnd();

    // The Engine Overrides — These mirror the core execution calls invoked by X-Ray's high-level managers
    void Render(u32 vertexCount, u32 primitiveCount);
    void RenderIndexed(u32 indexCount, u32 primitiveCount, u32 baseVertex);

    // Backend binding state updates called by Shader/Resource managers
    void SetVertexStream(u32 streamSlot, VkBuffer buffer, VkDeviceSize offset, u32 stride);
    void SetIndexStream(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);

    VkCommandBuffer GetActiveCommandBuffer() const { return m_activeCmdBuffer; }
};

// Declare the external link to sync with X-Ray's global macro structures
extern CBackend vk_Backend;
