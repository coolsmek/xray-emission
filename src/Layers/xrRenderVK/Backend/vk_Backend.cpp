// Stateful abstraction and Draw Dispatcher
/*vk_Backend.cpp: Implements the Vulkan-specific non-inline members of the shared CBackend class.
The inline drawing logic has been moved to vkR_Backend_Runtime.h to integrate with X-Ray's execution flow. */

#include "stdafx.h"

// Note: No local CBackend vk_Backend instantiation here anymore.
// The engine instantiates 'CBackend RCache;' globally in R_Backend.cpp.
thread_local VkRecordContext* CBackend::m_ctx = &g_vkPrimaryContext;

void CBackend::OnFrameBegin(VkCommandBuffer cmdBuffer, u32 imageIndex)
{
    m_ctx->m_activeCmdBuffer   = cmdBuffer;
    m_ctx->m_currentImageIndex = imageIndex;

    // Set default front face matching X-Ray's CW expectations under inverted viewport Y
    if (m_ctx->m_activeCmdBuffer)
    {
        g_vkCmdSetFrontFace(m_ctx->m_activeCmdBuffer, VK_FRONT_FACE_CLOCKWISE);
    }

    // Per-frame sub-range isolation (Option A):
    if (cmdBuffer != VK_NULL_HANDLE)
    {
        const u32 frameSlot     = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;
        const u32 framesInFlight = CHW::MAX_FRAMES_IN_FLIGHT;
        Vertex.FrameReset(frameSlot, framesInFlight);
        Index.FrameReset(frameSlot, framesInFlight);
    }

    // Zero per-frame render stats
    if (cmdBuffer != VK_NULL_HANDLE)
        Memory.mem_fill(&stat, 0, sizeof(stat));

    // Clear standard stream registers safely on initialization
    Memory.mem_fill(m_ctx->m_boundVertexStreams, 0, sizeof(m_ctx->m_boundVertexStreams));

    m_ctx->m_boundIndexStream = VK_NULL_HANDLE;
    m_ctx->m_boundIndexOffset = 0;
    m_ctx->m_boundIndexType   = VK_INDEX_TYPE_UINT16;

    // Reset CPU-side pointer caches
    Invalidate();
    InvalidateCtx();
}

void CBackend::InvalidateCtx()
{
    auto* c = m_ctx;
    c->pRT[0] = c->pRT[1] = c->pRT[2] = c->pRT[3] = nullptr;
    c->pZB = nullptr;

    c->decl = nullptr; c->vb = nullptr; c->ib = nullptr; c->vb_stride = 0;
    c->state = nullptr; c->ps = nullptr; c->vs = nullptr; c->gs = nullptr; c->cs = nullptr;
    c->ctable = nullptr;

    c->stencil_enable = c->stencil_func = c->stencil_ref = c->stencil_mask = u32(-1);
    c->stencil_writemask = c->stencil_fail = c->stencil_pass = c->stencil_zfail = u32(-1);
    c->fill_mode = c->cull_mode = c->z_enable = c->z_write_enable = c->z_func = u32(-1);
    c->alpha_ref = c->colorwrite_mask = u32(-1);
    c->blend_enable = c->blend_src = c->blend_dst = c->blend_op = u32(-1);
    c->blend_src_alpha = c->blend_dst_alpha = c->blend_op_alpha = u32(-1);

    c->m_texturesDirty = true;
    c->m_pipelineDirty = true;

    for (int i = 0; i < mtMaxPixelShaderTextures; ++i) c->textures_ps[i] = nullptr;
    for (int i = 0; i < 4; ++i) c->textures_vs[i] = nullptr;

    c->s_lastBoundSet = VK_NULL_HANDLE;
    c->s_lastDynOffsets.clear();
}

void CBackend::SetVertexStream(u32 streamSlot, VkBufferWrapper* buf, VkDeviceSize offset, u32 stride)
{
    if (streamSlot >= 4) return;
    if (!buf) return;
    m_ctx->m_boundVertexStreams[streamSlot].VBuffer = buf->buffer;
    m_ctx->m_boundVertexStreams[streamSlot].Offset  = offset;
    m_ctx->m_boundVertexStreams[streamSlot].Stride  = stride;

    if (buf->buffer && m_ctx->m_activeCmdBuffer)
    {
        VkDeviceSize vkSize = (buf->size > offset) ? (buf->size - offset) : buf->size;
        VkDeviceSize vkStride = stride;
        g_vkCmdBindVertexBuffers2(m_ctx->m_activeCmdBuffer, streamSlot, 1, &buf->buffer, &offset, &vkSize, &vkStride);
    }
}

void CBackend::SetIndexStream(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
    m_ctx->m_boundIndexStream = buffer;
    m_ctx->m_boundIndexOffset = offset;
    m_ctx->m_boundIndexType   = indexType;
}

void CBackend::set_States(ref_state& _state)
{
    // Extract the SimulatorStates configured by the shader pass (blender)
    const auto& states = _state->state_code.GetStates();
    
    BOOL bStencilEnable = m_ctx->stencil_enable;
        u32  stencilFunc = m_ctx->stencil_func;
        u32  stencilRef = m_ctx->stencil_ref;
        u32  stencilMask = m_ctx->stencil_mask;
        u32  stencilWriteMask = m_ctx->stencil_writemask;
        u32  stencilFail = m_ctx->stencil_fail;
        u32  stencilPass = m_ctx->stencil_pass;
        u32  stencilZFail = m_ctx->stencil_zfail;
        
        BOOL bUpdateStencil = FALSE;

        // Alpha blend state tracking
        BOOL bBlendEnable = m_ctx->blend_enable;
        u32 blendSrc = m_ctx->blend_src;
        u32 blendDst = m_ctx->blend_dst;
        u32 blendOp = m_ctx->blend_op;
        u32 blendSrcAlpha = m_ctx->blend_src_alpha;
        u32 blendDstAlpha = m_ctx->blend_dst_alpha;
        u32 blendOpAlpha = m_ctx->blend_op_alpha;
        BOOL bUpdateBlend = FALSE;

        for (const auto& S : states)
        {
            if (S.type == 0) // RenderState
            {
                switch (S.v1)
                {
                case D3DRS_ZENABLE:
                    set_Z(S.v2 ? TRUE : FALSE);
                    break;
                case D3DRS_ZWRITEENABLE:
                    set_ZWrite(S.v2 ? TRUE : FALSE);
                    break;
                case D3DRS_ZFUNC:
                    set_ZFunc(S.v2);
                    break;
                case D3DRS_COLORWRITEENABLE:
                    set_ColorWriteEnable(S.v2);
                    break;
                case D3DRS_ALPHATESTENABLE:
                    // In Vulkan (like DX10/11), alpha test is handled in the shader via `clip(tex.a - aref)`.
                    // If alpha test is disabled, we force alpha_ref to 0 so the shader discard fails.
                    // If it is enabled, a subsequent D3DRS_ALPHAREF will overwrite it with the correct value.
                    if (!S.v2) set_AlphaRef(0);
                    break;
                case D3DRS_ALPHAREF:
                    set_AlphaRef(S.v2);
                    break;
                case D3DRS_CULLMODE:
                    set_CullMode(S.v2);
                    break;
                case D3DRS_STENCILENABLE:
                    bStencilEnable = S.v2 ? TRUE : FALSE;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILFUNC:
                    stencilFunc = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILREF:
                    stencilRef = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILMASK:
                    stencilMask = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILWRITEMASK:
                    stencilWriteMask = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILFAIL:
                    stencilFail = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILPASS:
                    stencilPass = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_STENCILZFAIL:
                    stencilZFail = S.v2;
                    bUpdateStencil = TRUE;
                    break;
                case D3DRS_ALPHABLENDENABLE:
                    bBlendEnable = S.v2 ? TRUE : FALSE;
                    bUpdateBlend = TRUE;
                    break;
                case D3DRS_SRCBLEND:
                    blendSrc = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                case D3DRS_DESTBLEND:
                    blendDst = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                case D3DRS_BLENDOP:
                    blendOp = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                // Wait, these might not be defined if X-Ray uses separate alpha blend.
                // It usually uses D3DRS_SRCBLENDALPHA etc. Wait, wait. Does XRay define D3DRS_SRCBLENDALPHA? 
                // Let's check dx9 headers. Actually, X-Ray's SimulatorStates array uses the D3D9 enums.
                case 207: // D3DRS_SRCBLENDALPHA
                    blendSrcAlpha = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                case 208: // D3DRS_DESTBLENDALPHA
                    blendDstAlpha = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                case 209: // D3DRS_BLENDOPALPHA
                    blendOpAlpha = S.v2;
                    bUpdateBlend = TRUE;
                    break;
                }
            }
        }
        
        if (bUpdateStencil)
        {
            set_Stencil(bStencilEnable, stencilFunc, stencilRef, stencilMask, stencilWriteMask, stencilFail, stencilPass, stencilZFail);
        }

        if (bUpdateBlend)
        {
            m_ctx->blend_enable = bBlendEnable;
            m_ctx->blend_src = blendSrc;
            m_ctx->blend_dst = blendDst;
            m_ctx->blend_op = blendOp;
            m_ctx->blend_src_alpha = blendSrcAlpha;
            m_ctx->blend_dst_alpha = blendDstAlpha;
            m_ctx->blend_op_alpha = blendOpAlpha;
        }

    if (m_ctx->state != _state->state)
    {
        PGO(Msg("PGO:state_block"));
#ifdef DEBUG
        stat.states++;
#endif
        m_ctx->state = _state->state;
    }

#if defined(USE_VK)
    m_ctx->m_pipelineDirty = true;
#endif
}

void CBackend::set_Blend(u32 enable, u32 src, u32 dst, u32 op,
                         u32 srcA, u32 dstA, u32 opA)
{
    m_ctx->blend_enable    = enable ? TRUE : FALSE;
    m_ctx->blend_src       = src;
    m_ctx->blend_dst       = dst;
    m_ctx->blend_op        = op;
    m_ctx->blend_src_alpha = srcA;
    m_ctx->blend_dst_alpha = dstA;
    m_ctx->blend_op_alpha  = opA;
#if defined(USE_VK)
    m_ctx->m_pipelineDirty = true;
#endif
}
