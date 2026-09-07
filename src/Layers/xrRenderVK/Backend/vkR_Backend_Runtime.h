#ifndef vkR_Backend_RuntimeH
#define vkR_Backend_RuntimeH
#pragma once

#include "../Managers/vk_PipelineCache.h"

struct VkRecordContext {
    // Cat A1: vk_FlushDescriptors caches
    void* s_lastCmd = nullptr;
    u32 s_dummyCbOffset = 0;

    struct DescriptorCache {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        u32 cbvOffsets[128] = { 0 };
        VkImageView views[128] = { VK_NULL_HANDLE };
        VkSampler samplers[128] = { VK_NULL_HANDLE };
    } s_cache;

    VkDescriptorSet s_lastBoundSet = VK_NULL_HANDLE;
    svector<uint32_t, 32> s_lastDynOffsets;   // dynamic offsets from the last bind

    // Cat A2: BindCurrentState caches
    bool m_hasLastDesc = false;
    vk_PipelineStateDesc m_lastDesc{};
    const vk_PipelineCacheManager::PipelineEntry* m_lastBoundEntry = nullptr;
    VkPipelineLayout m_lastBoundLayout = VK_NULL_HANDLE;
    VkPipeline m_dsLastPipeline = VK_NULL_HANDLE;
    bool m_renderPassDirty = false;

    u32 m_dsCull = 0;
    u32 m_dsDepthTest = 0, m_dsDepthWrite = 0, m_dsDepthCmp = 0;
    u32 m_dsTopology = 0;
    u32 m_dsStencilEn = 0, m_dsStencilFunc = 0, m_dsStencilRef = 0, m_dsStencilCmpMask = 0;
    u32 m_dsStencilWrMask = 0, m_dsStencilFail = 0, m_dsStencilPass = 0, m_dsStencilZFail = 0;

    // Cat B: CBackend members written per draw
    VkCommandBuffer m_activeCmdBuffer = VK_NULL_HANDLE;
    u32 m_currentImageIndex = 0;
    bool m_pipelineDirty = true;
    bool m_texturesDirty = true;

    ID3DVertexShader* vs = nullptr;
    ID3DPixelShader* ps = nullptr;
    ID3DGeometryShader* gs = nullptr;
    ID3D11ComputeShader* cs = nullptr;
    SDeclaration* decl = nullptr;

    // -vk_mt_diag instrumentation (zero-cost when flag is off)
    u64 diag_recordTicks       = 0;  // wall time this worker spent in GBufferStaticWorker
    u64 diag_lockWaitTicks_desc = 0; // ticks blocked acquiring vk_DescriptorManager's cache-shard mutex
    u64 diag_lockWaitTicks_pipe = 0; // ticks blocked acquiring vk_PipelineCache::m_mutex/m_compileMutex
    u32 diag_descCalls    = 0;  // FindCachedSet + InsertCachedSet calls
    u32 diag_uniformCalls = 0;  // AllocateDynamicUniform calls
    u32 diag_pipeHits     = 0;  // BindCurrentState cache hits (m_pipelineDirty was false)
    u32 diag_pipeMisses   = 0;  // BindCurrentState cache misses (lock+find path taken)
    u32 diag_pipeCompiles = 0;  // actual vkCreateGraphicsPipelines calls

    ID3DVertexBuffer* vb = nullptr;
    ID3DIndexBuffer* ib = nullptr;
    u32 vb_stride = 0;

    struct StreamState {
        VkBuffer     VBuffer;
        VkDeviceSize Offset;
        u32          Stride;
    } m_boundVertexStreams[4] = {};

    VkBuffer        m_boundIndexStream = VK_NULL_HANDLE;
    VkDeviceSize    m_boundIndexOffset = 0;
    VkIndexType     m_boundIndexType = VK_INDEX_TYPE_UINT16;

    u32 blend_enable = 0, blend_src = 0, blend_dst = 0, blend_op = 0;
    u32 blend_src_alpha = 0, blend_dst_alpha = 0, blend_op_alpha = 0;
    u32 colorwrite_mask = 0, alpha_ref = 0;
    u32 z_enable = 0, z_write_enable = 0, z_func = 0;
    u32 cull_mode = 0;
    u32 fill_mode = 0;
    u32 stencil_enable = 0, stencil_func = 0, stencil_ref = 0, stencil_mask = 0;
    u32 stencil_writemask = 0, stencil_fail = 0, stencil_pass = 0, stencil_zfail = 0;
    ID3DState* state = nullptr;
    D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = (D3D_PRIMITIVE_TOPOLOGY)0;

    ID3DRenderTargetView* pRT[4] = {};
    ID3DDepthStencilView* pZB = nullptr;

    R_constant_table* ctable = nullptr;
    u32 workerId = 0;   // 0 = primary/main thread; workers use 1..N. Indexes per-worker cbuffer staging.
    bool needsViewProjSeed = false;   // worker-only: true until first real ctable bind this frame
    ref_cbuffer m_aVertexConstants[CBackend::MaxCBuffers] = {};
    ref_cbuffer m_aPixelConstants[CBackend::MaxCBuffers] = {};
    ref_cbuffer m_aGeometryConstants[CBackend::MaxCBuffers] = {};
    ref_cbuffer m_aHullConstants[CBackend::MaxCBuffers] = {};
    ref_cbuffer m_aDomainConstants[CBackend::MaxCBuffers] = {};
    ref_cbuffer m_aComputeConstants[CBackend::MaxCBuffers] = {};

    STextureList* T = nullptr;
    CTexture* textures_ps[CBackend::mtMaxPixelShaderTextures] = {};
    CTexture* textures_vs[CBackend::mtMaxVertexShaderTextures] = {};
    CTexture* textures_gs[CBackend::mtMaxGeometryShaderTextures] = {};
    CTexture* textures_cs[CBackend::mtMaxComputeShaderTextures] = {};
    CTexture* textures_hs[CBackend::mtMaxHullShaderTextures] = {};
    CTexture* textures_ds[CBackend::mtMaxDomainShaderTextures] = {};

    R_xforms xforms;
    R_hemi hemi;
    R_tree tree;
    
    float o_hemi = 0.f;
    float o_hemi_cube[6] = {};
    float o_sun = 0.f;
    
    // Stats accumulators
    u64 stat_calls = 0, stat_verts = 0, stat_polys = 0, stat_vs = 0, stat_ps = 0;
    u32 s_sets_built = 0, s_sets_reused = 0, s_binds = 0;
    R_statistics stat_r;
};

// Global primary context (used by the main thread)
extern VkRecordContext g_vkPrimaryContext;
extern VkRecordContext g_vkWorkerContexts[CHW::VK_GBUFFER_WORKERS];

extern thread_local bool g_vkRecordingSecondaryGBuffer;

extern PFN_vkCmdPushDescriptorSetKHR g_vkCmdPushDescriptorSetKHR;
extern PFN_vkCmdBindVertexBuffers2   g_vkCmdBindVertexBuffers2;
extern PFN_vkCmdSetFrontFace         g_vkCmdSetFrontFace;
extern PFN_vkCmdSetCullMode            g_vkCmdSetCullMode;
extern PFN_vkCmdSetDepthTestEnable     g_vkCmdSetDepthTestEnable;
extern PFN_vkCmdSetDepthWriteEnable    g_vkCmdSetDepthWriteEnable;
extern PFN_vkCmdSetDepthCompareOp      g_vkCmdSetDepthCompareOp;
extern PFN_vkCmdSetPrimitiveTopology   g_vkCmdSetPrimitiveTopology;

extern PFN_vkCmdSetStencilTestEnable   g_vkCmdSetStencilTestEnable;
extern PFN_vkCmdSetStencilOp           g_vkCmdSetStencilOp;
extern PFN_vkCmdSetStencilCompareMask  g_vkCmdSetStencilCompareMask;
extern PFN_vkCmdSetStencilWriteMask    g_vkCmdSetStencilWriteMask;
extern PFN_vkCmdSetStencilReference    g_vkCmdSetStencilReference;

extern VkSampler g_smp_nofilter;
extern VkSampler g_smp_linear;
extern VkSampler g_smp_rtlinear;
extern VkSampler g_smp_material;
extern VkSampler g_smp_smap;

#if defined(USE_VK)
#include "../Managers/vk_DescriptorManager.h"
#include "../Managers/vk_PipelineCache.h"
#include "../vkRenderDeviceRender.h"
#include "../../xrRender/ResourceManager.h"
#include "../../xrRenderDX10/dx10ConstantBuffer.h"
#endif

IC void CBackend::set_xform(u32 ID, const Fmatrix& _M) { stat.xforms++; }
IC void CBackend::set_RT(ID3DRenderTargetView* RT, u32 ID)
{
    if (m_ctx->pRT[ID] != RT)
    {
        PGO(Msg("PGO:setRT"));
        stat.target_rt++;
        m_ctx->pRT[ID] = RT;
        pRT[ID] = RT;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
        m_ctx->m_renderPassDirty = true;
#endif
    }
}

IC void CBackend::set_ZB(ID3DDepthStencilView* ZB)
{
    if (m_ctx->pZB != ZB)
    {
        PGO(Msg("PGO:setZB"));
        stat.target_zb++;
        m_ctx->pZB = ZB;
        pZB = ZB;   // FIX: keep base pZB in sync — get_ZB() reads this, not m_ctx->pZB.
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
        m_ctx->m_renderPassDirty = true;
#endif
    }
}

ICF VkCommandBuffer CBackend::GetActiveCommandBuffer() const
{
    return m_ctx->m_activeCmdBuffer;
}

ICF bool CBackend::CheckAndResetRenderPassDirty()
{
    bool dirty = m_ctx->m_renderPassDirty;
    m_ctx->m_renderPassDirty = false;
    return dirty;
}

ICF u32 CBackend::GetCurrentImageIndex() const
{
    return m_ctx->m_currentImageIndex;
}

ICF void CBackend::set_Format(SDeclaration* _decl)
{
    if (m_ctx->decl != _decl)
    {
        PGO(Msg("PGO:v_format:%x",_decl));
#ifdef DEBUG
#if defined(USE_VK)
        InterlockedIncrement((volatile LONG*)&stat.decl);
#else
        stat.decl++;
#endif
#endif
        m_ctx->decl = _decl;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
#endif
    }
}

ICF void CBackend::set_PS(ID3DPixelShader* _ps, LPCSTR _n)
{
    if (m_ctx->ps != _ps)
    {
        PGO(Msg("PGO:Pshader:%x",_ps));
#if defined(USE_VK)
        m_ctx->stat_ps++;
#else
        stat.ps++;
#endif
        m_ctx->ps = _ps;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
#endif
#ifdef DEBUG
        ps_name = _n;
#endif
    }
}

ICF void CBackend::set_GS(ID3DGeometryShader* _gs, LPCSTR _n)
{
    if (m_ctx->gs != _gs)
    {
        PGO(Msg("PGO:Gshader:%x",_gs));        m_ctx->gs = _gs;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
#endif
    }
#ifdef DEBUG
        gs_name = _n;
#endif
}

ICF void CBackend::set_HS(ID3D11HullShader* _hs, LPCSTR _n)
{
    if (hs != _hs)
    {
        PGO(Msg("PGO:Hshader:%x",_hs));
        hs = _hs;
#ifdef DEBUG
        hs_name = _n;
#endif
    }
}

ICF void CBackend::set_DS(ID3D11DomainShader* _ds, LPCSTR _n)
{
    if (ds != _ds)
    {
        PGO(Msg("PGO:Dshader:%x",_ds));
        ds = _ds;
#ifdef DEBUG
        ds_name = _n;
#endif
    }
}

ICF void CBackend::set_CS(ID3D11ComputeShader* _cs, LPCSTR _n)
{
    if (m_ctx->cs != _cs)
    {
        PGO(Msg("PGO:Cshader:%x",_cs));
        m_ctx->cs = _cs;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
#endif
#ifdef DEBUG
        cs_name = _n;
#endif
    }
}

ICF bool CBackend::is_TessEnabled() { return (ds != 0 || hs != 0); }

ICF void CBackend::set_VS(ID3DVertexShader* _vs, LPCSTR _n)
{
    if (m_ctx->vs != _vs)
    {
        PGO(Msg("PGO:Vshader:%x",_vs));
#if defined(USE_VK)
        m_ctx->stat_vs++;
#else
        stat.vs++;
#endif
        m_ctx->vs = _vs;
#if defined(USE_VK)
        m_ctx->m_pipelineDirty = true;
#endif
#ifdef DEBUG
        vs_name = _n;
#endif
    }
}

ICF void CBackend::set_Vertices(ID3DVertexBuffer* _vb, u32 _vb_stride)
{
    if ((m_ctx->vb != _vb) || (m_ctx->vb_stride != _vb_stride))
    {
        PGO(Msg("PGO:VB:%x,%d",_vb,_vb_stride));
#ifdef DEBUG
        //m_ctx->stat.vb++;
#endif
        m_ctx->vb = _vb;
        m_ctx->vb_stride = _vb_stride;

        // Vulkan Binding
        if (_vb && m_ctx->m_activeCmdBuffer)
        {
            VkDeviceSize vkOffset = 0;
            VkDeviceSize vkSize = _vb->size;
            VkDeviceSize vkStride = _vb_stride;
            g_vkCmdBindVertexBuffers2(m_ctx->m_activeCmdBuffer, 0, 1, &_vb->buffer, &vkOffset, &vkSize, &vkStride);
        }
    }
}

ICF void CBackend::set_Indices(ID3DIndexBuffer* _ib)
{
    if (m_ctx->ib != _ib)
    {
        PGO(Msg("PGO:IB:%x",_ib));
#ifdef DEBUG
        //m_ctx->stat.ib++;
#endif
        m_ctx->ib = _ib;
    }
}

// ─── set_VS overloads ────────────────────────────────────────────────────────
// VK has no input-layout signature concept (no ID3DBlob* per VS).
// Just forward to the base set_VS(ID3DVertexShader*, LPCSTR) above.
ICF void CBackend::set_VS(ref_vs& _vs)
{
    set_VS(_vs ? _vs->vs : nullptr, _vs ? _vs->cName.c_str() : nullptr);
}

ICF void CBackend::set_VS(SVS* _vs)
{
    if (_vs)
        set_VS(_vs->vs, _vs->cName.c_str());
}

// ─── set_Geometry ─────────────────────────────────────────────────────────────
// Binds vertex declaration, vertex buffer, and index buffer in one call.
// Under USE_DX10/VK, set_Format takes SDeclaration* (not a raw D3D9 dcl ptr).
IC void CBackend::set_Geometry(SGeometry* _geom)
{
    set_Format(_geom->dcl._get());          // SDeclaration*
    set_Vertices(_geom->vb, _geom->vb_stride);
    set_Indices(_geom->ib);
}

// ─── set_Constants ────────────────────────────────────────────────────────────
// Caches the constant table and distributes cbuffer slots to m_aVertex/PixelConstants.
// The actual GPU upload (ring-buffer write + VkUpdateDescriptorSets) happens lazily
// inside vk_FlushDescriptors() just before each draw call.
IC void CBackend::set_Constants(R_constant_table* _C)
{
    if (m_ctx->ctable == _C) return;
    m_ctx->ctable = _C;
    m_ctx->xforms.unmap();
    m_ctx->hemi.unmap();
    m_ctx->tree.unmap();

    if (!_C) return;

    PGO(Msg("PGO:c-table"));

    // Clear current slot arrays
    for (int i = 0; i < MaxCBuffers; ++i)
    {
        m_ctx->m_aPixelConstants[i]    = 0;
        m_ctx->m_aVertexConstants[i]   = 0;
        m_ctx->m_aGeometryConstants[i] = 0;
    }

    // Walk the constant table and assign each cbuffer to the correct slot array
    for (auto& rec : _C->m_CBTable)
    {
        u32 key = rec.first;
        u32 idx = key & CB_BufferIndexMask;
        VERIFY(idx < (u32)MaxCBuffers);

        switch (key & CB_BufferTypeMask)
        {
        case CB_BufferPixelShader:    m_ctx->m_aPixelConstants[idx]    = rec.second; break;
        case CB_BufferVertexShader:   m_ctx->m_aVertexConstants[idx]   = rec.second; break;
        case CB_BufferGeometryShader: m_ctx->m_aGeometryConstants[idx] = rec.second; break;
        // Hull/Domain/Compute not wired yet — silently skip
        default: break;
        }
    }

#if defined(USE_VK)
    // Worker contexts don't call set_V/set_P per-draw (only set_W). The first time
    // a REAL shader table is bound this frame, seed view/proj using THIS table
    // (guaranteed to declare m_V/m_P if the shader needs them) instead of borrowing
    // an unrelated table from the primary (e.g. HOM's occlusion shader).
    if (m_ctx->workerId != 0 && m_ctx->needsViewProjSeed)
    {
        m_ctx->xforms.set_V(g_vkPrimaryContext.xforms.get_V());
        m_ctx->xforms.set_P(g_vkPrimaryContext.xforms.get_P());
        m_ctx->needsViewProjSeed = false;
    }
#endif

    // ── Call automatic constant handlers (screen_res, timers, fog, etc.) ──────
    // Mirrors dx10R_Backend_Runtime.h — each R_constant with a handler
    // (e.g. binder_screen_res) writes its current value into the cbuffer here.
    for (auto it = _C->table.begin(); it != _C->table.end(); ++it)
    {
        R_constant* Cs = &**it;
        if (!Cs || !Cs->handler)
            continue;
#if defined(USE_VK)
        // Skip a handler ONLY if the actual cbuffer backing this constant has no
        // CPU storage yet (calling handler->setup would crash in
        // dx10ConstantBuffer::Access on a null GetRawData()).
        //
        // The cbuffer slot is encoded in Cs->destination — SAME decode as
        // R_constants::GetCBuffer (vk_r_constants.cpp). Cs->vs.index / ps.index are
        // BYTE OFFSETS within the buffer, so the old "index / (64*sizeof(Fvector4))"
        // math produced a garbage slot and wrongly skipped L_material / hemi_cube_*
        // handlers → those constants stayed 0 → flat-grey, unlit dynamic geometry
        // (weapons, NPCs, the 3D PDA).
        bool cbuf_ready = true;

        if (Cs->vs.index != 0xFFFF) // constant lives in a VS cbuffer
        {
            u32 slot = (Cs->destination & RC_dest_vertex_cb_index_mask)
                       >> RC_dest_vertex_cb_index_shift;
            cbuf_ready = (slot < (u32)MaxCBuffers) && m_ctx->m_aVertexConstants[slot]
                         && (m_ctx->m_aVertexConstants[slot]->GetRawData() != nullptr);
        }

        if (cbuf_ready && Cs->ps.index != 0xFFFF) // and/or a PS cbuffer
        {
            u32 slot = (Cs->destination & RC_dest_pixel_cb_index_mask)
                       >> RC_dest_pixel_cb_index_shift;
            cbuf_ready = (slot < (u32)MaxCBuffers) && m_ctx->m_aPixelConstants[slot]
                         && (m_ctx->m_aPixelConstants[slot]->GetRawData() != nullptr);
        }

        if (!cbuf_ready)
        {
            if (Cs->name == "L_material")
                Msg("![vk_set_Constants] SKIPPED L_material handler! cbuf_ready=false");
            continue;
        }
#endif
        Cs->handler->setup(Cs);
    }
}

// ─── D3D → VK translation helpers ────────────────────────────────────────────
// D3DCULL values: NONE=1, CW=2, CCW=3
IC VkCullModeFlags TranslateCullMode(u32 d3dCull)
{
    switch (d3dCull)
    {
    case D3DCULL_CW:  return VK_CULL_MODE_FRONT_BIT;   // front faces are CW in VK default
    case D3DCULL_CCW: return VK_CULL_MODE_BACK_BIT;    // back faces are CCW
    default:          return VK_CULL_MODE_NONE;
    }
}

// D3D compare ops are simply (VkCompareOp)(d3dOp - 1): NEVER=1→0, LESS=2→1, …, ALWAYS=8→7
IC VkCompareOp TranslateCompareOp(u32 d3dCmp)
{
    if (d3dCmp < 1 || d3dCmp > 8) return VK_COMPARE_OP_ALWAYS;
    return (VkCompareOp)(d3dCmp - 1);
}

// D3D stencil ops are also (VkStencilOp)(d3dOp - 1): KEEP=1→0, ZERO=2→1, …
IC VkStencilOp TranslateStencilOp(u32 d3dOp)
{
    if (d3dOp < 1 || d3dOp > 8) return VK_STENCIL_OP_KEEP;
    return (VkStencilOp)(d3dOp - 1);
}

// ─── set_CullMode ─────────────────────────────────────────────────────────────
IC void CBackend::set_CullMode(u32 _mode)
{
    if (m_ctx->cull_mode != _mode)
    {
        m_ctx->cull_mode = _mode;
        if (m_ctx->m_activeCmdBuffer)
            g_vkCmdSetCullMode(m_ctx->m_activeCmdBuffer, TranslateCullMode(_mode));
    }
}

// ─── set_FillMode ─────────────────────────────────────────────────────────────
// VK polygon-mode dynamic state requires VK_EXT_extended_dynamic_state3.
// For now just cache the value; the pipeline-cache reads fill_mode when compiling.
ICF void CBackend::set_FillMode(u32 _mode)
{
    m_ctx->fill_mode = _mode;
}

// ─── set_Stencil ──────────────────────────────────────────────────────────────
IC void CBackend::set_Stencil(u32 _enable, u32 _func, u32 _ref, u32 _mask, u32 _writemask,
                               u32 _fail, u32 _pass, u32 _zfail)
{
    if (m_ctx->stencil_enable != _enable)
    {
        m_ctx->stencil_enable = _enable;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilTestEnable(m_ctx->m_activeCmdBuffer, _enable ? VK_TRUE : VK_FALSE);
    }
    if (!m_ctx->stencil_enable) return;

    constexpr VkStencilFaceFlags kFaces = VK_STENCIL_FACE_FRONT_AND_BACK;

    if (m_ctx->stencil_func != _func)
    {
        m_ctx->stencil_func = _func;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilOp(m_ctx->m_activeCmdBuffer, kFaces,
                TranslateStencilOp(m_ctx->stencil_fail),
                TranslateStencilOp(m_ctx->stencil_pass),
                TranslateStencilOp(m_ctx->stencil_zfail),
                TranslateCompareOp(_func));
    }
    if (m_ctx->stencil_ref != _ref)
    {
        m_ctx->stencil_ref = _ref;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilReference(m_ctx->m_activeCmdBuffer, kFaces, _ref);
    }
    if (m_ctx->stencil_mask != _mask)
    {
        m_ctx->stencil_mask = _mask;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilCompareMask(m_ctx->m_activeCmdBuffer, kFaces, _mask);
    }
    if (m_ctx->stencil_writemask != _writemask)
    {
        m_ctx->stencil_writemask = _writemask;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilWriteMask(m_ctx->m_activeCmdBuffer, kFaces, _writemask);
    }
    if (m_ctx->stencil_fail != _fail || m_ctx->stencil_pass != _pass || m_ctx->stencil_zfail != _zfail)
    {
        m_ctx->stencil_fail  = _fail;
        m_ctx->stencil_pass  = _pass;
        m_ctx->stencil_zfail = _zfail;
        if (m_ctx->m_activeCmdBuffer)
            vkCmdSetStencilOp(m_ctx->m_activeCmdBuffer, kFaces,
                TranslateStencilOp(_fail),
                TranslateStencilOp(_pass),
                TranslateStencilOp(_zfail),
                TranslateCompareOp(m_ctx->stencil_func));
    }
}

// ─── set_Z / set_ZFunc ────────────────────────────────────────────────────────
IC void CBackend::set_Z(u32 _enable)
{
    if (m_ctx->z_enable != _enable)
    {
        m_ctx->z_enable = _enable;
        if (m_ctx->m_activeCmdBuffer)
            g_vkCmdSetDepthTestEnable(m_ctx->m_activeCmdBuffer, _enable ? VK_TRUE : VK_FALSE);
    }
}

IC void CBackend::set_ZWrite(u32 _enable)
{
    if (m_ctx->z_write_enable != _enable)
    {
        m_ctx->z_write_enable = _enable;
        if (m_ctx->m_activeCmdBuffer)
            g_vkCmdSetDepthWriteEnable(m_ctx->m_activeCmdBuffer, _enable ? VK_TRUE : VK_FALSE);
    }
}

IC void CBackend::set_ZFunc(u32 _func)
{
    if (m_ctx->z_func != _func)
    {
        m_ctx->z_func = _func;
        if (m_ctx->m_activeCmdBuffer)
            g_vkCmdSetDepthCompareOp(m_ctx->m_activeCmdBuffer, TranslateCompareOp(_func));
    }
}

// ─── set_AlphaRef ─────────────────────────────────────────────────────────────
// Vulkan has no fixed-function alpha test. Store the value for any shader that
// reads it via a push constant or uniform, but don't emit a GPU command.
IC void CBackend::set_AlphaRef(u32 _value)
{
    m_ctx->alpha_ref = _value;
}

// ─── set_ColorWriteEnable ─────────────────────────────────────────────────────
// VK color write masks are per-attachment pipeline state.
// Cache the value; the pipeline cache picks it up at compile time.
IC void CBackend::set_ColorWriteEnable(u32 _mask)
{
    m_ctx->colorwrite_mask = _mask;
}

// ─── set_Scissor ──────────────────────────────────────────────────────────────
IC void CBackend::set_Scissor(Irect* R)
{
    if (!m_ctx->m_activeCmdBuffer) return;
    if (R)
    {
        VkRect2D sc;
        sc.offset        = { 0, 0 };
        sc.extent        = HW.m_vkSCExtent; // Force full screen scissor fallback for diagnostics
        vkCmdSetScissor(m_ctx->m_activeCmdBuffer, 0, 1, &sc);
    }
    else
    {
        VkRect2D full{};
        full.extent = HW.m_vkSCExtent;
        vkCmdSetScissor(m_ctx->m_activeCmdBuffer, 0, 1, &full);
    }
}

IC VkPrimitiveTopology TranslateTopologyVK(D3DPRIMITIVETYPE T)
{
    static VkPrimitiveTopology translateTable[] =
    {
        VK_PRIMITIVE_TOPOLOGY_MAX_ENUM, // None
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST, // D3DPT_POINTLIST = 1
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST, // D3DPT_LINELIST = 2
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, // D3DPT_LINESTRIP = 3
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // D3DPT_TRIANGLELIST = 4
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, // D3DPT_TRIANGLESTRIP = 5
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, // D3DPT_TRIANGLEFAN = 6
    };
    VERIFY(T < sizeof(translateTable)/sizeof(translateTable[0]));
    VERIFY(T >= 0);
    VkPrimitiveTopology result = translateTable[T];
    VERIFY(result != VK_PRIMITIVE_TOPOLOGY_MAX_ENUM);
    return result;
}

IC u32 GetIndexCountVK(D3DPRIMITIVETYPE T, u32 iPrimitiveCount)
{
    switch (T)
    {
    case D3DPT_POINTLIST:      return iPrimitiveCount;
    case D3DPT_LINELIST:       return iPrimitiveCount * 2;
    case D3DPT_LINESTRIP:      return iPrimitiveCount + 1;
    case D3DPT_TRIANGLELIST:   return iPrimitiveCount * 3;
    case D3DPT_TRIANGLESTRIP:  return iPrimitiveCount + 2;
    default: NODEFAULT;
#ifdef DEBUG
        return 0;
#endif
    }
}

#if defined(USE_VK)
// ─── vk_EnsureRenderPassActive ─────────────────────────────────────────────────
// Ensures an active Dynamic Rendering pass before a draw call. X-Ray's UI
// bypasses CRenderTarget::u_setrt, so we must automatically catch it here.
void vk_EnsureRenderPassActive(VkCommandBuffer cmd);

// ─── vk_FlushDescriptors (Optimized via Push Descriptors) ────────────────────
// Called immediately before every draw call.
// 1. Flushes dirty CBVs into the ring buffer (fixed-slot binding indices).
// 2. Writes texture/sampler descriptors from RCache.textures_ps / textures_vs.
// 3. Pushes bindings directly into the command buffer using VK_KHR_push_descriptor,
//    bypassing all vkUpdateDescriptorSets and descriptor pool allocations.
//
IC void vk_FlushDescriptors(VkCommandBuffer cmd)
{
    if (!cmd)
        return;

    VkRecordContext* ctx = RCache.m_ctx;

    VkPipelineLayout layout = PipelineCache.GetLastBoundLayout(ctx);
    if (layout == VK_NULL_HANDLE)
        return;

    static const uint32_t kMaxCB = CBackend::MaxCBuffers;

    // ── 0. Static Caches & State Tracking ─────────────────────────────────────
    static CTexture* s_pNullTexture = nullptr;
    static bool s_vkDebugChecked = false;
    static bool s_vkDebugEnabled = false;

    if (!s_vkDebugChecked) {
        s_vkDebugEnabled = strstr(Core.Params, "-vkdebug") != nullptr;
        s_vkDebugChecked = true;
    }

    auto reset_cache = [&]() {
        ctx->s_cache.layout = VK_NULL_HANDLE;
        Memory.mem_fill(ctx->s_cache.cbvOffsets, 0xFF, sizeof(ctx->s_cache.cbvOffsets));
        Memory.mem_fill(ctx->s_cache.views, 0, sizeof(ctx->s_cache.views));
        Memory.mem_fill(ctx->s_cache.samplers, 0, sizeof(ctx->s_cache.samplers));
    };

    bool bDirtyPush = false;
    bool layoutChanged = false;
    int dirtyReasonBinding = -1;
    const char* dirtyReasonStr = nullptr;

    if (ctx->s_lastCmd != cmd) {
        reset_cache();
        ctx->s_lastCmd = cmd;
        ctx->s_lastBoundSet = VK_NULL_HANDLE;
        layoutChanged = true;
        
        // Allocate a dummy chunk of zeroes ONCE per frame
        uint32_t range = 256;
        void* pDummy = DescriptorManager.AllocateDynamicUniform(range, ctx->s_dummyCbOffset);
        if (pDummy) memset(pDummy, 0, range);
    }

    if (ctx->s_cache.layout != layout) {
        reset_cache();
        ctx->s_cache.layout = layout;
        bDirtyPush = true;
        layoutChanged = true;
        dirtyReasonStr = "LAYOUT_CHANGED";
    }

    if (layoutChanged) {
        bDirtyPush = true;
    }

    if (!s_pNullTexture) {
        vkRenderDeviceRender* devRender = (vkRenderDeviceRender*)Device.m_pRender;
        if (devRender && devRender->Resources) {
            // NOTE: "$null" never creates a VkImageView/VkSampler (Load() early-returns),
            // so its get_SRView() is nullptr and cannot serve as a fallback.
            // Use a real texture name so the full load path assigns a valid view+sampler
            // (missing DDS falls back to the generated 1x1 dummy in vk_TextureLoad.cpp).
            s_pNullTexture = devRender->Resources->_CreateTexture("ui\\ui_pop_up_active_back");
            if (s_pNullTexture) {
                s_pNullTexture->Load();   // force surface creation now
                static bool bWarned = false;
                if (!bWarned && !s_pNullTexture->get_SRView()) {
                    Msg("! VK ERROR: Fallback texture failed to produce a valid SRView!");
                    bWarned = true;
                }
            }
        }
    }



    // Early out removed: we must always run Step 1 and 2 to ensure we don't skip
    // updating dynamic offsets if the shader bound a DIFFERENT, but already-flushed
    // cbuffer. Step 4's `offsetsChanged` check will prevent redundant API calls.

    // ── 1. Flush dirty cbuffers ───────────────────────────────────────────────
    // Every cbuffer used this draw must live in THIS frame's ring slice.
    // The ring resets per-frame, so a cbuffer not re-flushed this frame has a
    // stale offset pointing at reused memory. Force-flush if dirty OR not yet
    // flushed this frame.
    auto ensureFlushed = [&](ref_cbuffer& cb) {
        if (cb && (cb->IsDirty() || cb->GetFlushFrame() != Device.dwFrame))
            cb->Flush();   // Flush allocates a fresh ring slice + stamps the frame
    };
    for (int i = 0; i < kMaxCB; ++i)
    {
        ensureFlushed(ctx->m_aVertexConstants[i]);
        ensureFlushed(ctx->m_aPixelConstants[i]);
        ensureFlushed(ctx->m_aGeometryConstants[i]);
        ensureFlushed(ctx->m_aHullConstants[i]);
        ensureFlushed(ctx->m_aDomainConstants[i]);
        ensureFlushed(ctx->m_aComputeConstants[i]);
    }

    // ── 2. Hash & Cache Preparation ───────────────────────────────────────────
    const auto& entry = PipelineCache.GetLastBoundEntry(ctx);
    uint64_t hash = 0;
    auto hash_combine = [&](uint64_t val) {
        val ^= val >> 30;
        val *= 0xbf58476d1ce4e5b9ULL;
        val ^= val >> 27;
        val *= 0x94d049bb133111ebULL;
        val ^= val >> 31;
        hash ^= val + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };

    VkBuffer dynBuf = DescriptorManager.GetCurrentDynamicBuffer();
    hash_combine((uint64_t)layout);
    hash_combine((uint64_t)dynBuf);

    svector<uint32_t, 32> dynOffsets;
    svector<uint32_t, 32> dynRanges;

    // 2a. CBVs
    for (uint32_t bindingIdx : entry.dynamicUniformBindings)
    {
        ref_cbuffer cbRef = nullptr;
        if (bindingIdx < 14)
            cbRef = ctx->m_aVertexConstants[bindingIdx];
        else if (bindingIdx >= 14 && bindingIdx < 28)
            cbRef = ctx->m_aPixelConstants[bindingIdx - 14];
        else if (bindingIdx >= 28 && bindingIdx < 42)
            cbRef = ctx->m_aGeometryConstants[bindingIdx - 28];
        else if (bindingIdx >= 42 && bindingIdx < 56)
            cbRef = ctx->m_aHullConstants[bindingIdx - 42];
        else if (bindingIdx >= 56 && bindingIdx < 70)
            cbRef = ctx->m_aDomainConstants[bindingIdx - 56];
        else if (bindingIdx >= 70 && bindingIdx < 84)
            cbRef = ctx->m_aComputeConstants[bindingIdx - 70];

        uint32_t offset = ctx->s_dummyCbOffset;
        uint32_t range = 256; 

        if (cbRef && cbRef->GetRawSize() > 0)
        {
            offset = cbRef->GetDynamicOffset();
            range  = cbRef->GetRawSize();
        }

        if (ctx->s_cache.cbvOffsets[bindingIdx] != offset) {
            ctx->s_cache.cbvOffsets[bindingIdx] = offset;
            bDirtyPush = true;
            
            static u32 log_count = 0;
#ifdef VK_ENABLE_TESTS
            if (log_count < 100) {
                if (cbRef) Msg("VK DEBUG: cbuffer offset changed for binding %u, range %u", bindingIdx, range);
                log_count++;
            }
#endif
        }

        hash_combine(range);
        dynOffsets.push_back(offset);
        dynRanges.push_back(range);
    }

    // 2b. Textures & Samplers (Resolve and Cache only if dirty)
    if (ctx->m_texturesDirty || layoutChanged)
    {
        for (const auto& tb : entry.textureBindings)
        {
            VkImageView view = VK_NULL_HANDLE;
            if (s_pNullTexture && s_pNullTexture->get_SRView())
                view = s_pNullTexture->get_SRView()->view;

            if (!tb.isVS) {
                if (tb.engineSlot < 16) {
                    if (CTexture* T = ctx->textures_ps[tb.engineSlot])
                        if (auto* srv = T->get_SRView()) view = srv->view;
                }
            } else {
                u32 vslot = tb.engineSlot - (u32)CTexture::rstVertex;
                if (vslot < 4) {
                    if (CTexture* T = ctx->textures_vs[vslot])
                        if (auto* srv = T->get_SRView()) view = srv->view;
                }
            }

            if (ctx->s_cache.views[tb.binding] != view) {
                ctx->s_cache.views[tb.binding] = view;
                bDirtyPush = true;
                dirtyReasonBinding = tb.binding;
                dirtyReasonStr = "TEXTURE_VIEW_CHANGED";
            }
        }

        for (const auto& sb : entry.samplerBindings)
        {
            // Sampler binding pairs with the same engine slot as its texture:
            //   PS: SAMPLER 72..87 → slot (binding-72), image was 56..71 → slot (binding-56)
            //   VS: SAMPLER 92..95 → slot (binding-92)
            VkSampler s = VK_NULL_HANDLE;
            CTexture* T = nullptr;
            if (sb.first >= 72 && sb.first < 88) {            // PS sampler
                u32 slot = sb.first - 72;
                if (slot < 16) T = ctx->textures_ps[slot];
            } else if (sb.first >= 92 && sb.first < 96) {     // VS sampler
                u32 slot = sb.first - 92;
                if (slot < 4) T = ctx->textures_vs[slot];
            }
            if (T) { if (auto* srv = T->get_SRView()) s = srv->sampler; }
            // Fallback to the null texture's sampler so the descriptor is always valid.
            if (s == VK_NULL_HANDLE && s_pNullTexture && s_pNullTexture->get_SRView())
                s = s_pNullTexture->get_SRView()->sampler;

            if (ctx->s_cache.samplers[sb.first] != s) {
                ctx->s_cache.samplers[sb.first] = s;
                bDirtyPush = true;
                dirtyReasonBinding = sb.first;
                dirtyReasonStr = "SAMPLER_CHANGED";
            }
        }
    }

    // 2c. Hash directly from cache (Fast Path)
    for (const auto& tb : entry.textureBindings) {
        hash_combine((uint64_t)ctx->s_cache.views[tb.binding]);
    }
    for (const auto& sb : entry.samplerBindings) {
        hash_combine((uint64_t)ctx->s_cache.samplers[sb.first]);
    }

    // ── 3. Find or Build Descriptor Set ───────────────────────────────────────
    VkDescriptorSet set = VK_NULL_HANDLE;

    if (!bDirtyPush && ctx->s_lastBoundSet != VK_NULL_HANDLE) {
        set = ctx->s_lastBoundSet;
        ctx->s_sets_reused++;
    } else {
        set = DescriptorManager.FindCachedSet(hash);
        if (set != VK_NULL_HANDLE) {
            ctx->s_sets_reused++;
        } else {
            set = DescriptorManager.AllocateDescriptorSet(PipelineCache.GetLastBoundSetLayout(0, ctx));
            if (set != VK_NULL_HANDLE) {
                static u32 s_dbg = 0;
                if (s_dbg < 15) {
                    Msg("VK DIAG set=%p layout=%p tex=%u smp=%u ubo=%u texDirty=%d layoutChg=%d",
                        (void*)set, (void*)layout,
                        (u32)entry.textureBindings.size(), (u32)entry.samplerBindings.size(),
                        (u32)entry.dynamicUniformBindings.size(),
                        (int)ctx->m_texturesDirty, (int)layoutChanged);
                    for (const auto& tb : entry.textureBindings)
                        Msg("   tex b=%u slot=%u vs=%d view=%p", tb.binding, tb.engineSlot,
                            (int)tb.isVS, (void*)ctx->s_cache.views[tb.binding]);
                    for (const auto& sb : entry.samplerBindings)
                        Msg("   smp b=%u samp=%p", sb.first, (void*)ctx->s_cache.samplers[sb.first]);
                    s_dbg++;
                }

                svector<VkDescriptorBufferInfo, 32> bufInfos;
                svector<VkDescriptorImageInfo,  64> imgInfos;
                svector<VkWriteDescriptorSet,  128> writes;

                for (u32 i = 0; i < entry.dynamicUniformBindings.size(); ++i) {
                    VkDescriptorBufferInfo bi{};
                    bi.buffer = dynBuf;
                    bi.offset = 0;
                    bi.range  = dynRanges[i];
                    bufInfos.push_back(bi);

                    VkWriteDescriptorSet w{};
                    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet          = set;
                    w.dstBinding      = entry.dynamicUniformBindings[i];
                    w.dstArrayElement = 0;
                    w.descriptorCount = 1;
                    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    w.pBufferInfo     = &bufInfos.back();
                    writes.push_back(w);
                }

                for (const auto& tb : entry.textureBindings) {
                    VkImageView view = ctx->s_cache.views[tb.binding];
                    if (view == VK_NULL_HANDLE) continue;

                    VkDescriptorImageInfo ii{};
                    ii.sampler     = VK_NULL_HANDLE;
                    ii.imageView   = view;
                    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfos.push_back(ii);

                    VkWriteDescriptorSet wImg{};
                    wImg.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    wImg.dstSet          = set;
                    wImg.dstBinding      = tb.binding;
                    wImg.dstArrayElement = 0;
                    wImg.descriptorCount = 1;
                    wImg.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    wImg.pImageInfo      = &imgInfos.back();
                    writes.push_back(wImg);
                }

                for (const auto& sb : entry.samplerBindings) {
                    VkSampler s = ctx->s_cache.samplers[sb.first];
                    if (s == VK_NULL_HANDLE) continue;

                    VkDescriptorImageInfo si{};
                    si.sampler   = s;
                    si.imageView = VK_NULL_HANDLE;
                    imgInfos.push_back(si);

                    VkWriteDescriptorSet wSamp{};
                    wSamp.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    wSamp.dstSet          = set;
                    wSamp.dstBinding      = sb.first;
                    wSamp.dstArrayElement = 0;
                    wSamp.descriptorCount = 1;
                    wSamp.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
                    wSamp.pImageInfo      = &imgInfos.back();
                    writes.push_back(wSamp);
                }

                if (!writes.empty()) {
                    vkUpdateDescriptorSets(HW.m_vkDevice, (uint32_t)writes.size(), writes.begin(), 0, nullptr);
                }
                DescriptorManager.InsertCachedSet(hash, set);
                ctx->s_sets_built++;
            } else {
                static bool s_logged = false;
                if (!s_logged) {
                    Msg("! VK ERROR: Descriptor Pool Exhausted! Draw skipped.");
                    s_logged = true;
                }
            }
        }
    }

    // ── 4. Bind ───────────────────────────────────────────────────────────────
    // Dynamic offsets are applied at BIND time, not baked into the set.
    // Two draws can share the same cached set (same textures) yet require
    // different per-object cbuffer offsets, so we must rebind whenever EITHER
    // the set changed OR any dynamic offset changed.
    bool offsetsChanged = (dynOffsets.size() != ctx->s_lastDynOffsets.size());
    if (!offsetsChanged) {
        for (u32 i = 0; i < dynOffsets.size(); ++i)
            if (dynOffsets[i] != ctx->s_lastDynOffsets[i]) { offsetsChanged = true; break; }
    }

    if (set != ctx->s_lastBoundSet || offsetsChanged) {
        vk_EnsureRenderPassActive(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set,
                                (uint32_t)dynOffsets.size(), dynOffsets.begin());
        ctx->s_binds++;
        ctx->s_lastBoundSet = set;
        ctx->s_lastDynOffsets = dynOffsets;
    }

    ctx->m_texturesDirty = false;
}
#endif

IC void CBackend::Compute(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    m_ctx->stat_calls++;
    if (m_ctx->m_activeCmdBuffer)
        vkCmdDispatch(m_ctx->m_activeCmdBuffer, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

IC void CBackend::Render(D3DPRIMITIVETYPE _T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
    if (!m_ctx->m_activeCmdBuffer) return;

    u32 iIndexCount = GetIndexCountVK(_T, PC);

#if defined(USE_VK)
    extern u64 g_tDSGraph, g_tState, g_tTex, g_tLOD, g_tVis;
    static u64 g_tBind=0, g_tFlush=0, g_tDraw=0; static u32 g_nDraw=0;
    static u32 g_lastFramePerf = 0;
    const bool dp = !!strstr(Core.Params, "-vk_draw_perf");
    if (dp && g_lastFramePerf != Device.dwFrame) {
        if (g_lastFramePerf != 0 && g_nDraw > 0) {
            float fBind = float(g_tBind) / float(CPU::qpc_freq) * 1000.f;
            float fFlush = float(g_tFlush) / float(CPU::qpc_freq) * 1000.f;
            float fDraw = float(g_tDraw) / float(CPU::qpc_freq) * 1000.f;
            float fDS = float(g_tDSGraph) / float(CPU::qpc_freq) * 1000.f;
            float fSt = float(g_tState) / float(CPU::qpc_freq) * 1000.f;
            float fTx = float(g_tTex) / float(CPU::qpc_freq) * 1000.f;
            float fL = float(g_tLOD) / float(CPU::qpc_freq) * 1000.f;
            float fV = float(g_tVis) / float(CPU::qpc_freq) * 1000.f;
            Msg("VK DRAW PERF f%u: dsgraph=%.3fms (st=%.3f tx=%.3f lod=%.3f vis=%.3f) bind=%.3fms flush=%.3fms draw=%.3fms (draws=%u)", 
                g_lastFramePerf, fDS, fSt, fTx, fL, fV, fBind, fFlush, fDraw, g_nDraw);
        }
        g_tBind = g_tFlush = g_tDraw = g_tDSGraph = g_tState = g_tTex = g_tLOD = g_tVis = 0;
        g_nDraw = 0;
        g_lastFramePerf = Device.dwFrame;
    }

    m_ctx->m_PrimitiveTopology = (D3D_PRIMITIVE_TOPOLOGY)_T;
    vk_EnsureRenderPassActive(m_ctx->m_activeCmdBuffer);
    
    u64 a = dp ? CPU::GetCLK() : 0;
    PipelineCache.BindCurrentState(m_ctx->m_activeCmdBuffer, m_ctx);
    u64 b = dp ? CPU::GetCLK() : 0;

    // ─── THE TRANSFORMATION UNIFORM FLUSH HARNESS ───
    // Force-flush the global engine constant table so it re-uploads
    // the projection matrix for our fonts (and now 3D scene transforms)
    this->constants.flush_cache();

    if (!m_ctx->pRT[0]) {
        // UI: force no-cull, but keep the dynamic-state cache coherent so the
        // next 3D draw re-applies its real cull mode via ApplyDynamicStates.
        if (m_ctx->m_dsCull != VK_CULL_MODE_NONE) {
            vkCmdSetCullMode(m_ctx->m_activeCmdBuffer, VK_CULL_MODE_NONE);
            m_ctx->m_dsCull = VK_CULL_MODE_NONE;
        }
    }
    vk_FlushDescriptors(m_ctx->m_activeCmdBuffer);
    u64 c = dp ? CPU::GetCLK() : 0;

    // TEMP DIAG — dynamic/skinned draw tracing
    static bool s_traceDyn = !!strstr(Core.Params, "-vk_trace_dyn");
    if (s_traceDyn) {
        static u32 s_scene = 0, s_ui = 0;
        if (m_ctx->pRT[0] != nullptr) {          // SCENE draws only
            if (s_scene < 100) {
                Msg("VK SCENE DRAW: pRT0=%p pRT1=%p pZB=%p ib=%p vb=%p idxCount=%u cull=%u",
                    m_ctx->pRT[0], m_ctx->pRT[1], m_ctx->pZB,
                    m_ctx->ib, m_ctx->vb, iIndexCount, m_ctx->cull_mode);
                s_scene++;
            }
        } else {
            s_ui++;  // count UI draws silently
        }
        static u32 s_lastF = 0;
        if (s_lastF != Device.dwFrame) {
            Msg("VK FRAME %u: scene_draws_logged=%u ui_draws=%u", s_lastF, s_scene, s_ui);
            s_lastF = Device.dwFrame; s_ui = 0;
        }
    }

    // ─── DEFINITIVE INDEX BUFFER BINDING ───
    // Bind the index buffer for THIS draw (do not rely on set_Indices' change-guard,
    // which can skip rebinding when the ib pointer is unchanged across frames/cmdbufs).
    if (m_ctx->ib != nullptr)
    {
        vkCmdBindIndexBuffer(m_ctx->m_activeCmdBuffer, m_ctx->ib->buffer, 0, VK_INDEX_TYPE_UINT16);
    }
    else if (this->QuadIB && this->QuadIB->buffer != VK_NULL_HANDLE)
    {
        vkCmdBindIndexBuffer(m_ctx->m_activeCmdBuffer, this->QuadIB->buffer, 0, VK_INDEX_TYPE_UINT16);
    }
#endif

    // Dispatch indexed drawing execution command
    vkCmdDrawIndexed(m_ctx->m_activeCmdBuffer, iIndexCount, 1, startI, baseV, 0);
    
    u64 d = dp ? CPU::GetCLK() : 0;
    if (dp) { g_tBind += b - a; g_tFlush += c - b; g_tDraw += d - c; ++g_nDraw; }

    m_ctx->stat_calls++;
    m_ctx->stat_verts += countV;
    m_ctx->stat_polys += PC;
}

IC void CBackend::Render(D3DPRIMITIVETYPE _T, u32 startV, u32 PC)
{
    if (!m_ctx->m_activeCmdBuffer) return;

    u32 iVertexCount = GetIndexCountVK(_T, PC); // for non-indexed, index count maps to vertex count

#if defined(USE_VK)
    m_ctx->m_PrimitiveTopology = (D3D_PRIMITIVE_TOPOLOGY)_T;
    vk_EnsureRenderPassActive(m_ctx->m_activeCmdBuffer);
    PipelineCache.BindCurrentState(m_ctx->m_activeCmdBuffer, m_ctx);
    if (!m_ctx->pRT[0]) {
        // UI: force no-cull, but keep the dynamic-state cache coherent so the
        // next 3D draw re-applies its real cull mode via ApplyDynamicStates.
        if (m_ctx->m_dsCull != VK_CULL_MODE_NONE) {
            vkCmdSetCullMode(m_ctx->m_activeCmdBuffer, VK_CULL_MODE_NONE);
            m_ctx->m_dsCull = VK_CULL_MODE_NONE;
        }
        // [Phase 11] Removed this->constants.flush_cache() hack here.
        // It was forcing redundant constant uploads on every single UI draw call, destroying CPU performance.
    }
    vk_FlushDescriptors(m_ctx->m_activeCmdBuffer);

#endif

    // Dispatch non-indexed drawing execution command
    vkCmdDraw(m_ctx->m_activeCmdBuffer, iVertexCount, 1, startV, 0);

    m_ctx->stat_calls++;
    m_ctx->stat_verts += iVertexCount;
    m_ctx->stat_polys += PC;
}

#if defined(USE_VK)
inline CTexture* CBackend::get_ActiveTexture(u32 stage)
{
    CTexture* tex = NULL;
    if (stage < CTexture::rstVertex) tex = m_ctx->textures_ps[stage];
    else if (stage < CTexture::rstGeometry) tex = m_ctx->textures_vs[stage - CTexture::rstVertex];
    else if (stage < CTexture::rstHull) tex = m_ctx->textures_gs[stage - CTexture::rstGeometry];
    else if (stage < CTexture::rstDomain) tex = m_ctx->textures_hs[stage - CTexture::rstHull];
    else if (stage < CTexture::rstCompute) tex = m_ctx->textures_ds[stage - CTexture::rstDomain];
    else if (stage < CTexture::rstInvalid) tex = m_ctx->textures_cs[stage - CTexture::rstCompute];
    return tex;
}

inline R_constant* CBackend::get_c(LPCSTR n)
{
    R_constant_table* ct = m_ctx->ctable;
    if (ct) return ct->get(n);
    else return nullptr;
}

inline R_constant* CBackend::get_c(shared_str& n)
{
    R_constant_table* ct = m_ctx->ctable;
    if (ct) return ct->get(n);
    else return nullptr;
}

#endif
#endif // vkR_Backend_Runtime_included
