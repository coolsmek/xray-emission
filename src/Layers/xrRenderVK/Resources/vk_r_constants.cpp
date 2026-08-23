// vk_r_constants.cpp — VK equivalent of dx10r_constants_cache.cpp + dx10r_constants.cpp
//
// Provides:
//   R_constants::GetCBuffer   — routes set_c_* calls to the correct cbuffer slot in RCache
//   R_constants::flush_cache  — flushes dirty cbuffers into the ring buffer
//   R_constant_table stubs    — parse/parseConstants/parseResources are no-ops for VK;
//                               constant layout is driven by SPIR-V in vk_ShaderReflection.cpp
#include "stdafx.h"
#include "../../xrRender/r_constants_cache.h"
#include "../../xrRenderDX10/dx10ConstantBuffer.h"

// ─── R_constants::GetCBuffer ─────────────────────────────────────────────────
// Routes a constant-set call (set_c_float, set_c_int, etc.) to the correct
// dx10ConstantBuffer slot in RCache.  Logic mirrors dx10r_constants_cache.cpp
// exactly — no DX10 API calls, only array indexing.
dx10ConstantBuffer& R_constants::GetCBuffer(R_constant* C, BufferType BType)
{
    PROF_EVENT("R_constants::GetCBuffer");
    if (BType == BT_PixelBuffer)
    {
        int idx = (C->destination & RC_dest_pixel_cb_index_mask) >> RC_dest_pixel_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aPixelConstants[idx]);
        return *RCache.m_ctx->m_aPixelConstants[idx];
    }
    else if (BType == BT_VertexBuffer)
    {
        int idx = (C->destination & RC_dest_vertex_cb_index_mask) >> RC_dest_vertex_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aVertexConstants[idx]);
        return *RCache.m_ctx->m_aVertexConstants[idx];
    }
    else if (BType == BT_GeometryBuffer)
    {
        int idx = (C->destination & RC_dest_geometry_cb_index_mask) >> RC_dest_geometry_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aGeometryConstants[idx]);
        return *RCache.m_ctx->m_aGeometryConstants[idx];
    }
    else if (BType == BT_HullBuffer)
    {
        int idx = (C->destination & RC_dest_hull_cb_index_mask) >> RC_dest_hull_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aHullConstants[idx]);
        return *RCache.m_ctx->m_aHullConstants[idx];
    }
    else if (BType == BT_DomainBuffer)
    {
        int idx = (C->destination & RC_dest_domain_cb_index_mask) >> RC_dest_domain_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aDomainConstants[idx]);
        return *RCache.m_ctx->m_aDomainConstants[idx];
    }
    else if (BType == BT_Compute)
    {
        int idx = (C->destination & RC_dest_compute_cb_index_mask) >> RC_dest_compute_cb_index_shift;
        VERIFY(idx < CBackend::MaxCBuffers);
        VERIFY(RCache.m_ctx->m_aComputeConstants[idx]);
        return *RCache.m_ctx->m_aComputeConstants[idx];
    }

    FATAL("R_constants::GetCBuffer: unhandled BufferType");
    dx10ConstantBuffer* ptr = nullptr;
    return *ptr; // unreachable
}

// ─── R_constants::flush_cache ────────────────────────────────────────────────
// Flushes all dirty constant buffer slots into the VK ring buffer via Flush().
// vk_FlushDescriptors also calls Flush() per-draw; this covers any other callers.
void R_constants::flush_cache()
{
    PROF_EVENT("R_constants::flush_cache");
    for (int i = 0; i < CBackend::MaxCBuffers; ++i)
    {
        if (RCache.m_ctx->m_aVertexConstants[i])   RCache.m_ctx->m_aVertexConstants[i]->Flush();
        if (RCache.m_ctx->m_aPixelConstants[i])    RCache.m_ctx->m_aPixelConstants[i]->Flush();
        if (RCache.m_ctx->m_aGeometryConstants[i]) RCache.m_ctx->m_aGeometryConstants[i]->Flush();
        if (RCache.m_ctx->m_aHullConstants[i])     RCache.m_ctx->m_aHullConstants[i]->Flush();
        if (RCache.m_ctx->m_aDomainConstants[i])   RCache.m_ctx->m_aDomainConstants[i]->Flush();
        if (RCache.m_ctx->m_aComputeConstants[i])  RCache.m_ctx->m_aComputeConstants[i]->Flush();
    }
}

// ─── R_constant_table stubs ───────────────────────────────────────────────────
// In VK, shader constant metadata comes from SPIR-V reflection (vk_ShaderReflection.cpp),
// not from DX10 ID3DShaderReflection objects. These functions are no-ops that exist
// only to satisfy the linker for shared-layer code that declares them.
BOOL R_constant_table::parseConstants(ID3DShaderReflectionConstantBuffer* /*pTable*/, u32 /*destination*/)
{
    return TRUE;
}

BOOL R_constant_table::parseResources(ID3DShaderReflection* /*pReflection*/, int /*ResNum*/, u32 /*destination*/)
{
    return TRUE;
}

BOOL R_constant_table::parse(void* /*_desc*/, u32 /*destination*/)
{
    return TRUE;
}

