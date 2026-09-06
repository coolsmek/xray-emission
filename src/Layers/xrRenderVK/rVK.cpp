// Implements the core CRender class inheriting from the abstract engine interface IRender_interface
// rVK.cpp — Implements CRender for the Vulkan backend.
// Drives the per-frame acquire → record → submit → present pipeline.

#include "stdafx.h"
#include <math.h>

#include "rVK.h"
#include "vk_DiagTimer.h"
#include "Managers/vk_DescriptorManager.h"
#include "../xrRender/SkeletonCustom.h"
#include "Resources/vk_Shader.h"
#include "Resources/vk_ResourceManager.h"
#include "Resources/vk_BufferUtils.h"
#include "../../xrCore/stream_reader.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/x_ray.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include "../xrRender/fbasicvisual.h"
#include "../xrRender/FHierrarhyVisual.h"
#include "../xrRender/LightTrack.h"
#include <vector>
#include "r2_types.h"
#include "../../xrCPU_Pipe/ttapi.h"
#include "../xrRender/FTreeVisual.h"

#if defined(USE_VK)
thread_local u32 g_vkWorkerId = 0;
#endif

// Declared in R_calculate.cpp (shared across all renderers)
float r_dtex_range = 50.f;

extern ENGINE_API BOOL r2_sun_static;

class CGlow : public IRender_Glow
{
public:
	bool bActive;
public:
	CGlow() : bActive(false) {}
	virtual void set_active(bool b) { bActive = b; }
	virtual bool get_active() { return bActive; }
	virtual void set_position(const Fvector& P, const float eps = EPS_L) {}
	virtual void set_direction(const Fvector& D) {}
	virtual void set_radius(float R) {}
	virtual void set_texture(LPCSTR name) {}
	virtual void set_color(const Fcolor& C) {}
	virtual void set_color(float r, float g, float b) {}
};

CRender RImplementation;

CRender::CRender()  { init_cacades(); }
CRender::~CRender() {}

// ── create / destroy ──────────────────────────────────────────────────────────
void CRender::create()
{
    if (!Target)
        Target = xr_new<CRenderTarget>();
    if (!Models)
        Models = xr_new<CModelPool>();

    c_sbase = "s_base";
    c_lmaterial = "L_material";
    
    o.smapsize = 2048;
    if (Core.ParamsData.test(ECoreParams::smap1536)) o.smapsize = 1536;
    if (Core.ParamsData.test(ECoreParams::smap2048)) o.smapsize = 2048;
    if (Core.ParamsData.test(ECoreParams::smap2560)) o.smapsize = 2560;
    if (Core.ParamsData.test(ECoreParams::smap3072)) o.smapsize = 3072;
    if (Core.ParamsData.test(ECoreParams::smap4096)) o.smapsize = 4096;

    o.sunstatic = r2_sun_static;

    // In tool mode: skip systems that require game-level shaders not in the
    // minimal blender library (particles, portals, rain geometry).
    if (!HW.m_bToolMode)
    {
        PSLibrary.OnCreate();
        GMBase.initialize();
        GMRainWet.initialize();
    }
}

void CRender::destroy()
{
    // WAIT FOR GPU TO IDLE BEFORE DESTROYING RENDER TARGETS AND MODELS!
    vkDeviceWaitIdle(HW.m_vkDevice);

    if (!HW.m_bToolMode)
    {
        GMBase.destroy();
        GMRainWet.destroy();
    }

    PSLibrary.OnDestroy();
    xr_delete(Models);
    xr_delete(Target);
}

void CRender::reset_begin()
{
    xr_delete(Target);
}

void CRender::reset_end()
{
    Target = xr_new<CRenderTarget>();
}

// ── Shader element selection (R2 pattern) ────────────────────────────────────
ShaderElement* CRender::rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq)
{
    int id = SE_R2_SHADOW;
    if (PHASE_NORMAL == RImplementation.phase)
        id = ((_sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range)
             ? SE_R2_NORMAL_HQ : SE_R2_NORMAL_LQ;
    return pVisual->shader->E[id]._get();
}

ShaderElement* CRender::rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq)
{
    int id = SE_R2_SHADOW;
    if (PHASE_NORMAL == RImplementation.phase)
        id = ((_sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range)
             ? SE_R2_NORMAL_HQ : SE_R2_NORMAL_LQ;
    return pVisual->shader->E[id]._get();
}

// ── Buffer / SWI / Sector / Portal accessors ─────────────────────────────────
D3DVERTEXELEMENT9* CRender::getVB_Format(int id, BOOL _alt)
{
    auto& DC = _alt ? xDC : nDC;
    if (id < 0 || id >= (int)DC.size()) return nullptr;
    return DC[id].begin();
}

ID3DVertexBuffer* CRender::getVB(int id, BOOL _alt)
{
    auto& VB = _alt ? xVB : nVB;
    if (id < 0 || id >= (int)VB.size()) return nullptr;
    return VB[id];
}

ID3DIndexBuffer* CRender::getIB(int id, BOOL _alt)
{
    auto& IB = _alt ? xIB : nIB;
    if (id < 0 || id >= (int)IB.size()) return nullptr;
    return IB[id];
}

FSlideWindowItem* CRender::getSWI(int id)
{
    if (id < 0 || id >= (int)SWIs.size()) return nullptr;
    return &SWIs[id];
}

IRender_Portal* CRender::getPortal(int id)
{
    if (id < 0 || id >= (int)Portals.size()) return nullptr;
    return Portals[id];
}

IRender_Sector* CRender::getSectorActive() { return pLastSector; }

IRender_Sector* CRender::getSector(int id)
{
    VERIFY(id >= 0 && id < (int)Sectors.size());
    return Sectors[id];
}

// ── render_forward — G-Buffer scene pass ─────────────────────────────────────
void CRender::render_forward()
{
    if (!Target) return;

    static CTimer s_timer;
    s_timer.Start();

    RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

    {
        // level
        phase = PHASE_NORMAL;
        GMBase.r_dsgraph_render_static(1); // normal level, secondary priority
        GMBase.r_dsgraph_render_dynamic(1);
        GMBase.fade_render(); // faded-portals
        GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
        if (g_pGamePersistent) g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
        GMBase.r_dsgraph_render_sorted_hud();
    }

    RImplementation.o.distortion = FALSE; // disable distorion

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug"))
    {
        static int s_frameCount = 0;
        if (++s_frameCount % 25 == 0)
        {
            Msg("VK_PERF: G-Buffer Generation (render_forward) CPU Time: %.2f ms", s_timer.GetElapsed_sec() * 1000.0f);
        }
    }
#endif

}

// ── render_smap_direct — shadow map pass stub ────────────────────────────────
void CRender::render_smap_direct(Fmatrix& /*mCombined*/)
{
    if (!Target) return;
    // Bind shadow RT — no draw until SMAP blender SPIR-V is available (Phase 5).
    Target->u_setrt(Target->rt_smap_surf, ref_rt(), ref_rt(), &Target->rt_smap_dsv);
    VkCommandBuffer cmd = HW.m_vkCmdBuffers[HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT];
    if (Target->m_bRenderingPassActive)
    { vkCmdEndRendering(cmd); Target->m_bRenderingPassActive = false; }
}

#include "../xrRender/FVF.h"

// ── render_menu ─────────────────────────────────────────────────────────────
void CRender::render_menu()
{
    // Clear all intermediate RT pointers → forces vk_EnsureRenderPassActive
    // to target the swapchain backbuffer for all subsequent draws.
    RCache.set_RT(nullptr, 0);
    RCache.set_ZB(nullptr);

#if defined(USE_VK)
    // Validate swapchain is ready before issuing UI draws
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd)
    {
        //Msg("![VK] render_menu: no active command buffer, skipping UI draw");
        return;
    }
#endif

    vk_ScopedPass passMenu(cmd, "[Pass] Main Menu 2D UI", vk_colors::UI);

    RCache.set_CullMode(CULL_NONE);    //UI: no culling
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();

    // Now ALL draws inside OnRenderPPUI_main will go to the swapchain
    // because RCache.pRT[0] == null → vk_EnsureRenderPassActive starts swapchain pass
    if (g_pGamePersistent)
    {
        g_pGamePersistent->OnRenderPPUI_main();
    }

    // End the rendering pass that vk_EnsureRenderPassActive opened
    if (Target && Target->m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        Target->m_bRenderingPassActive = false;
    }
    //old code below, delete-later
    // g_pGamePersistent->OnRenderPPUI_main() leaves its own intermediate render pass active!
    // RCache.set_RT only updates pointers, it does NOT end the Vulkan render pass.
    // We MUST force the pass to end so vk_EnsureRenderPassActive will bind the swapchain!
    //Target->u_setrt(Device.dwWidth, Device.dwHeight, nullptr, nullptr, nullptr, nullptr);
    //RCache.set_RT(nullptr, 0);
    //RCache.set_ZB(nullptr);
}

// ─── Multithreaded Static G-Buffer Recording Helpers ──────────────────────────

static void vk_SetupGBufferSecondaryInheritance(
    CRenderTarget* Target,
    VkCommandBufferInheritanceRenderingInfo& inheritanceInfo,
    VkFormat colorFormats[2],
    VkCommandBufferInheritanceInfo& inheritance,
    VkCommandBufferBeginInfo& beginInfo)
{
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
    inheritanceInfo.colorAttachmentCount = 2; // rt_Position + rt_Color
    colorFormats[0] = Target->rt_Position->pRT->format;
    colorFormats[1] = Target->rt_Color->pRT->format;
    inheritanceInfo.pColorAttachmentFormats = colorFormats;
    inheritanceInfo.depthAttachmentFormat = Target->pZB.format;
    inheritanceInfo.stencilAttachmentFormat = Target->pZB.format;
    inheritanceInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.pNext = &inheritanceInfo;

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = &inheritance;
}

// Converts a contiguous [start,end) range over the *virtual* concatenation of
// mapStaticPasses[0][0] then mapStaticPasses[0][1] into the (passBegin,passEnd,
// packetBegin,packetEnd) form r_dsgraph_render_static_range expects. Mirrors the
// existing 2-worker special-casing but works for any worker count/any split point,
// including a chunk that straddles the pass0/pass1 boundary.
static void vk_ResolveStaticRange(u32 start, u32 end, u32 count0,
                                   u32& passBegin, u32& passEnd,
                                   u32& packetBegin, u32& packetEnd)
{
    if (end <= count0)
    {
        passBegin = 0; passEnd = 1;
        packetBegin = start; packetEnd = end;
    }
    else if (start >= count0)
    {
        passBegin = 1; passEnd = 2;
        packetBegin = start - count0; packetEnd = end - count0;
    }
    else
    {
        passBegin = 0; passEnd = 2;
        packetBegin = start; packetEnd = end - count0; // pass0 runs start..count0 implicitly (full tail)
    }
}

struct GBufferStaticWorkerParams
{
    uint32_t workerId;
    uint32_t frameSlot;
    uint32_t priority;
    uint32_t passBegin;
    uint32_t passEnd;
    uint32_t packetBegin;
    uint32_t packetEnd;
    CRenderTarget* target;
    CDSGraphManager* dsgraph;
};

static void GBufferStaticWorker(LPVOID lpvParams)
{
    GBufferStaticWorkerParams* p = (GBufferStaticWorkerParams*)lpvParams;
    const uint32_t w = p->workerId;
    const uint32_t frameSlot = p->frameSlot;
    VkCommandBuffer sec = HW.m_vkGBufferWorkerSecondary[frameSlot][w];
    VkCommandPool pool = HW.m_vkGBufferWorkerPools[frameSlot][w];
    CRenderTarget* Target = p->target;
    VkRecordContext* ctx = &g_vkWorkerContexts[w];
    ctx->workerId = w + 1;   // primary is 0; workers are 1..VK_GBUFFER_WORKERS
    g_vkWorkerId = w + 1;

    // Switch RCache.m_ctx to this worker's context FIRST
    VkRecordContext* prevCtx = RCache.m_ctx;
    RCache.m_ctx = ctx;

    LARGE_INTEGER wStart; if (g_vkMtDiagEnabled) QueryPerformanceCounter(&wStart);

    // Seed worker context with pass render targets, depth buffer, and pipeline state from primary
    for (int i = 0; i < 4; ++i)
        ctx->pRT[i] = g_vkPrimaryContext.pRT[i];
    ctx->pZB              = g_vkPrimaryContext.pZB;
    ctx->blend_enable     = g_vkPrimaryContext.blend_enable;
    ctx->blend_src        = g_vkPrimaryContext.blend_src;
    ctx->blend_dst        = g_vkPrimaryContext.blend_dst;
    ctx->blend_op         = g_vkPrimaryContext.blend_op;
    ctx->blend_src_alpha  = g_vkPrimaryContext.blend_src_alpha;
    ctx->blend_dst_alpha  = g_vkPrimaryContext.blend_dst_alpha;
    ctx->blend_op_alpha   = g_vkPrimaryContext.blend_op_alpha;
    ctx->colorwrite_mask  = g_vkPrimaryContext.colorwrite_mask;
    ctx->alpha_ref        = g_vkPrimaryContext.alpha_ref;
    ctx->z_enable         = g_vkPrimaryContext.z_enable;
    ctx->z_write_enable   = g_vkPrimaryContext.z_write_enable;
    ctx->z_func           = g_vkPrimaryContext.z_func;
    ctx->cull_mode        = g_vkPrimaryContext.cull_mode;
    ctx->stencil_enable   = g_vkPrimaryContext.stencil_enable;
    ctx->stencil_func     = g_vkPrimaryContext.stencil_func;
    ctx->stencil_ref      = g_vkPrimaryContext.stencil_ref;
    ctx->stencil_mask     = g_vkPrimaryContext.stencil_mask;
    ctx->stencil_writemask= g_vkPrimaryContext.stencil_writemask;
    ctx->stencil_fail     = g_vkPrimaryContext.stencil_fail;
    ctx->stencil_pass     = g_vkPrimaryContext.stencil_pass;
    ctx->stencil_zfail    = g_vkPrimaryContext.stencil_zfail;
    // ── Reset stale per-worker constant state from the previous frame/level ──
    // Worker contexts are global and persist; on save/level reload the old
    // R_constant_table and cbuffers are freed, leaving c_v/c_p/ctable dangling.
    // Must clear BEFORE any set_V/set_P (which only re-resolve when the cached
    // pointer is null) to avoid dereferencing freed constants.
    ctx->ctable = nullptr;
    ctx->xforms.unmap();
    ctx->hemi.unmap();
    ctx->tree.unmap();
    for (int i = 0; i < CBackend::MaxCBuffers; ++i)
    {
        ctx->m_aVertexConstants[i]   = 0;
        ctx->m_aPixelConstants[i]    = 0;
        ctx->m_aGeometryConstants[i] = 0;
    }
    ctx->needsViewProjSeed = true;

    ctx->vs = nullptr;
    ctx->ps = nullptr;
    ctx->gs = nullptr;
    ctx->cs = nullptr;
    ctx->decl = nullptr;
    ctx->state = nullptr;
    ctx->T = nullptr;
    ctx->vb = nullptr;
    ctx->ib = nullptr;
    ctx->vb_stride = 0;
    ctx->m_pipelineDirty = true;
    ctx->m_texturesDirty = true;

    // Reset command pool for this worker
    vkResetCommandPool(HW.m_vkDevice, pool, 0);

    // Begin secondary command buffer
    VkCommandBufferInheritanceRenderingInfo inheritanceInfo{};
    VkFormat colorFormats[2] = {};
    VkCommandBufferInheritanceInfo inheritance{};
    VkCommandBufferBeginInfo beginInfo{};
    vk_SetupGBufferSecondaryInheritance(Target, inheritanceInfo, colorFormats, inheritance, beginInfo);

    vkBeginCommandBuffer(sec, &beginInfo);

    // Re-emit dynamic state (viewport, scissor, front face)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = (float)Target->get_height();
    viewport.width = (float)Target->get_width();
    viewport.height = -(float)Target->get_height();
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(sec, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {Target->get_width(), Target->get_height()};
    vkCmdSetScissor(sec, 0, 1, &scissor);

    g_vkCmdSetFrontFace(sec, VK_FRONT_FACE_CLOCKWISE);

    // Assign worker thread-local context & command buffer
    g_vkRecordingSecondaryGBuffer = true;
    RCache.SetActiveCommandBuffer(sec);

    // Render partition
    p->dsgraph->r_dsgraph_render_static_range(p->priority, p->passBegin, p->passEnd, p->packetBegin, p->packetEnd);

    // Cleanup worker recording
    g_vkRecordingSecondaryGBuffer = false;
    RCache.m_ctx = prevCtx;
    g_vkWorkerId = 0;
    if (g_vkMtDiagEnabled) { LARGE_INTEGER wEnd; QueryPerformanceCounter(&wEnd); ctx->diag_recordTicks = (u64)(wEnd.QuadPart - wStart.QuadPart); }
    vkEndCommandBuffer(sec);
}

// ── Render — master per-frame driver ─────────────────────────────────────────
void CRender::Render()
{
    if (!HW.m_bRenderingFrame) return; // Prevent rendering if the device failed to acquire a frame (e.g. swapchain resize)

    if (!Target) return; // not yet initialised — create() not called

    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;

    // rmNormal(); // Ensure rmNormal is called (stubs in rVK)

    bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
    if (_menu_pp)
    {
        render_menu();
        return;
    }

    IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : nullptr;
    bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;
    if (!(g_pGameLevel && g_hud) || bMenu)
    {
        return;
    }

    if (m_bFirstFrameAfterReset)
    {
        for (light* L : v_all_lights)
            L->m_moving_frames = 0;

        m_bFirstFrameAfterReset = false;
        return;
    }

    RImplementation.o.distortion = FALSE;

    RCache.set_xform_project(Device.mProject);
    RCache.set_xform_view(Device.mView);

    // MT-safety: compute frame-global tree wind on the main thread before dispatch.
    FTreeVisual::PrepareWind();

    const bool rperf = !!strstr(Core.Params, "-vk_render_perf");
    CTimer tPass;
    float msVis = 0, msGBuf = 0, msShadow = 0, msCombine = 0;
    float msStatic = 0, msDyn = 0, msHud = 0, msLods = 0;

    // ── 1. Visibility capture ────────────────────────────────────────────
    if (rperf) tPass.Start();
    {
        vk_ScopedPass passVis(cmd, "[Pass] Visibility & Occlusion (HOM)", vk_colors::Visibility);
        ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
        HOM.MT_RENDER(); // CRITICAL: Run HOM to initialize depth buffer, otherwise everything is culled

        Target->phase_scene_prepare();

        phase = PHASE_NORMAL;


        // Phase 2: Check Sector State and Fallback
        // (Removed pLastSector hack to match DX11 behavior)

        GMBase.traverse(pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
        GMBase.r_dsgraph_capture_static();
        GMBase.r_dsgraph_capture_dynamic();

        RCache.Index.Flush();
        RCache.Vertex.Flush();
        RCache.set_xform_world(Fidentity);
    }
    if (rperf) msVis = tPass.GetElapsed_sec()*1000.f;

    // ── 2. G-Buffer pass ─────────────────────────────────────────────────
    if (rperf) tPass.Start();
    {
        vk_ScopedPass passGBuffer(cmd, "[Pass] G-Buffer Generation", vk_colors::GBuffer);

        CTimer tg;
        // PART 0 - Split pass for secondary command buffers
        // 1. Static Geometry on Secondary Command Buffer
        Target->phase_scene_begin(VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);

        if (rperf) tg.Start();
        {
            const uint32_t frameSlot = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;
            const bool bMtRecord = !!strstr(Core.Params, "-vk_mt_record");
            const u32 count0 = (u32)GMBase.RGraph.mapStaticPasses[0][0].size();
            const u32 count1 = (u32)GMBase.RGraph.mapStaticPasses[0][1].size();
            const u32 totalPackets = count0 + count1;

            // Keep the "don't bother threading trivial workloads" guard, but scale the
            // minimum with worker count instead of a flat 32 — otherwise a high core-count
            // machine partitions a small scene into slivers that cost more in dispatch/
            // join overhead than they save.
            constexpr u32 kMinPacketsPerWorker = 16;
            const u32 hwWorkers = ttapi_GetWorkersCount(); // total ttapi slots, incl. the one that runs inline on this thread
            const u32 maxWorkers = _min(hwWorkers, CHW::VK_GBUFFER_WORKERS);
            // Never create more workers than there are packets to hand out (avoids empty chunks).
            const u32 workerCount = _min(maxWorkers, _max(1u, totalPackets / kMinPacketsPerWorker));
            const bool canUseMT = bMtRecord && (maxWorkers >= 2) && (workerCount >= 2) &&
                                  (totalPackets >= kMinPacketsPerWorker * 2);

            if (canUseMT)
            {
                vk_ScopedPass passStatic(cmd, "Static Geometry (MT Secondary)", vk_colors::GBuffer);

                u64 sortTicks = 0, joinTicks = 0, executeTicks = 0;
                LARGE_INTEGER t0, t1;

                // 1. Sort queues on main thread before workers begin (unchanged from Inc.4)
                if (g_vkMtDiagEnabled) QueryPerformanceCounter(&t0);
                for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
                {
                    auto& queue = GMBase.RGraph.mapStaticPasses[0][iPass];
                    if (!queue.empty())
                    {
                        if (queue.size() < 4096)
                            std::sort(queue.begin(), queue.end());
                        else
                            xr_parallel_sort(queue.begin(), queue.end());
                    }
                }
                if (g_vkMtDiagEnabled) { QueryPerformanceCounter(&t1); sortTicks = (u64)(t1.QuadPart - t0.QuadPart); }

                // 2. Partition into `workerCount` contiguous chunks over the virtual
                //    concatenation of pass0+pass1 — remainder distributed to the first
                //    chunks so no worker differs from another by more than 1 packet.
                GBufferStaticWorkerParams params[CHW::VK_GBUFFER_WORKERS];
                const u32 baseChunk = totalPackets / workerCount;
                const u32 remainder = totalPackets % workerCount;
                u32 cursor = 0;
                for (u32 w = 0; w < workerCount; ++w)
                {
                    const u32 chunkSize = baseChunk + (w < remainder ? 1 : 0);
                    const u32 start = cursor;
                    const u32 end = cursor + chunkSize;
                    cursor = end;

                    u32 passBegin, passEnd, packetBegin, packetEnd;
                    vk_ResolveStaticRange(start, end, count0, passBegin, passEnd, packetBegin, packetEnd);
                    params[w] = { w, frameSlot, 0, passBegin, passEnd, packetBegin, packetEnd, Target, &GMBase };
                }

                // 3. Dispatch to ttapi workers and join
                if (g_vkMtDiagEnabled) QueryPerformanceCounter(&t0);
                for (u32 w = 0; w < workerCount; ++w)
                    ttapi_AddWorker(GBufferStaticWorker, &params[w]);
                ttapi_RunAllWorkers();
                if (g_vkMtDiagEnabled) { QueryPerformanceCounter(&t1); joinTicks = (u64)(t1.QuadPart - t0.QuadPart); }

                // 4. Restore main thread context
                RCache.m_ctx = &g_vkPrimaryContext;
                RCache.SetActiveCommandBuffer(cmd);

                // 5. Aggregate worker stats — IMPORTANT: loop bound is `workerCount` (this
                //    frame's actual dispatch count), NOT CHW::VK_GBUFFER_WORKERS. Contexts
                //    are persistent globals; if workerCount varies frame-to-frame (e.g. a
                //    scene dips below the MT threshold), stale higher-index contexts must
                //    NOT be re-aggregated/re-reset here.
                for (uint32_t w = 0; w < workerCount; ++w)
                {
                    g_vkPrimaryContext.s_sets_built  += g_vkWorkerContexts[w].s_sets_built;
                    g_vkPrimaryContext.s_sets_reused += g_vkWorkerContexts[w].s_sets_reused;
                    g_vkPrimaryContext.s_binds       += g_vkWorkerContexts[w].s_binds;
                    g_vkPrimaryContext.diag_pipeHits += g_vkWorkerContexts[w].diag_pipeHits;
                    g_vkPrimaryContext.diag_pipeMisses += g_vkWorkerContexts[w].diag_pipeMisses;
                    g_vkPrimaryContext.diag_pipeCompiles += g_vkWorkerContexts[w].diag_pipeCompiles;
                    RCache.stat.vs                   += g_vkWorkerContexts[w].stat_vs;
                    RCache.stat.ps                   += g_vkWorkerContexts[w].stat_ps;

                    RCache.stat.r.s_static.verts += g_vkWorkerContexts[w].stat_r.s_static.verts;
                    RCache.stat.r.s_static.dips  += g_vkWorkerContexts[w].stat_r.s_static.dips;
                    RCache.stat.r.s_flora.verts += g_vkWorkerContexts[w].stat_r.s_flora.verts;
                    RCache.stat.r.s_flora.dips  += g_vkWorkerContexts[w].stat_r.s_flora.dips;
                    RCache.stat.r.s_flora_lods.verts += g_vkWorkerContexts[w].stat_r.s_flora_lods.verts;
                    RCache.stat.r.s_flora_lods.dips  += g_vkWorkerContexts[w].stat_r.s_flora_lods.dips;
                    RCache.stat.r.s_details.verts += g_vkWorkerContexts[w].stat_r.s_details.verts;
                    RCache.stat.r.s_details.dips  += g_vkWorkerContexts[w].stat_r.s_details.dips;
                    RCache.stat.r.s_dynamic.verts += g_vkWorkerContexts[w].stat_r.s_dynamic.verts;
                    RCache.stat.r.s_dynamic.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic.dips;
                    RCache.stat.r.s_dynamic_sw.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_sw.verts;
                    RCache.stat.r.s_dynamic_sw.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_sw.dips;
                    RCache.stat.r.s_dynamic_inst.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_inst.verts;
                    RCache.stat.r.s_dynamic_inst.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_inst.dips;
                    RCache.stat.r.s_dynamic_1B.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_1B.verts;
                    RCache.stat.r.s_dynamic_1B.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_1B.dips;
                    RCache.stat.r.s_dynamic_2B.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_2B.verts;
                    RCache.stat.r.s_dynamic_2B.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_2B.dips;
                    RCache.stat.r.s_dynamic_3B.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_3B.verts;
                    RCache.stat.r.s_dynamic_3B.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_3B.dips;
                    RCache.stat.r.s_dynamic_4B.verts += g_vkWorkerContexts[w].stat_r.s_dynamic_4B.verts;
                    RCache.stat.r.s_dynamic_4B.dips  += g_vkWorkerContexts[w].stat_r.s_dynamic_4B.dips;

                    g_vkWorkerContexts[w].s_sets_built = g_vkWorkerContexts[w].s_sets_reused = g_vkWorkerContexts[w].s_binds = 0;
                    g_vkWorkerContexts[w].diag_pipeHits = g_vkWorkerContexts[w].diag_pipeMisses = g_vkWorkerContexts[w].diag_pipeCompiles = 0;
                    g_vkWorkerContexts[w].stat_vs = g_vkWorkerContexts[w].stat_ps = 0;
                    g_vkWorkerContexts[w].stat_r.s_static.verts = g_vkWorkerContexts[w].stat_r.s_static.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_flora.verts = g_vkWorkerContexts[w].stat_r.s_flora.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_flora_lods.verts = g_vkWorkerContexts[w].stat_r.s_flora_lods.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_details.verts = g_vkWorkerContexts[w].stat_r.s_details.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic.verts = g_vkWorkerContexts[w].stat_r.s_dynamic.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_sw.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_sw.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_inst.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_inst.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_1B.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_1B.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_2B.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_2B.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_3B.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_3B.dips = 0;
                    g_vkWorkerContexts[w].stat_r.s_dynamic_4B.verts = g_vkWorkerContexts[w].stat_r.s_dynamic_4B.dips = 0;
                }

                // 6. Execute all secondaries IN CHUNK ORDER — chunk order == ascending
                //    worker index == the order the sorted queue was sliced in. This must
                //    be preserved: the sort upstream produced state-coherent adjacency,
                //    and out-of-order execution would reintroduce redundant pipeline/
                //    descriptor rebinds at chunk boundaries (defeats this increment's
                //    "cache hit rate stays flat" check).
                if (g_vkMtDiagEnabled) QueryPerformanceCounter(&t0);
                VkCommandBuffer secs[CHW::VK_GBUFFER_WORKERS];
                for (u32 w = 0; w < workerCount; ++w)
                    secs[w] = HW.m_vkGBufferWorkerSecondary[frameSlot][w];
                vkCmdExecuteCommands(cmd, workerCount, secs);
                if (g_vkMtDiagEnabled) { QueryPerformanceCounter(&t1); executeTicks = (u64)(t1.QuadPart - t0.QuadPart); }

                // 7. Clear static queues on main thread post-join
                for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
                    GMBase.RGraph.mapStaticPasses[0][iPass].clear();

                if (g_vkMtDiagEnabled)
                {
                    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
                    auto ms = [&](u64 ticks){ return 1000.0 * (double)ticks / (double)freq.QuadPart; };
                    for (u32 w = 0; w < workerCount; ++w)
                    {
                        auto& c = g_vkWorkerContexts[w];
                        Msg("VK MT DIAG f%u W%u/%u: record=%.3fms lockWait[desc=%.3fms pipe=%.3fms] calls[desc=%u unif=%u pipeHit=%u pipeMiss=%u pipeCompile=%u]",
                            Device.dwFrame, w, workerCount, ms(c.diag_recordTicks),
                            ms(c.diag_lockWaitTicks_desc), ms(c.diag_lockWaitTicks_pipe),
                            c.diag_descCalls, c.diag_uniformCalls, c.diag_pipeHits, c.diag_pipeMisses, c.diag_pipeCompiles);
                        c.diag_recordTicks = c.diag_lockWaitTicks_desc = c.diag_lockWaitTicks_pipe = 0;
                        c.diag_descCalls = c.diag_uniformCalls = c.diag_pipeHits = c.diag_pipeMisses = c.diag_pipeCompiles = 0;
                    }
                    Msg("VK MT DIAG f%u: workers=%u sort=%.3fms join=%.3fms execute=%.3fms",
                        Device.dwFrame, workerCount, ms(sortTicks), ms(joinTicks), ms(executeTicks));
                }
            }
            else
            {
                vk_ScopedPass passStatic(cmd, "Static Geometry (Secondary)", vk_colors::GBuffer);
                
                VkCommandBuffer sec = HW.m_vkGBufferStaticSecondary[frameSlot];
                
                VkCommandBufferInheritanceRenderingInfo inheritanceInfo{};
                VkFormat colorFormats[2] = {};
                VkCommandBufferInheritanceInfo inheritance{};
                VkCommandBufferBeginInfo beginInfo{};
                vk_SetupGBufferSecondaryInheritance(Target, inheritanceInfo, colorFormats, inheritance, beginInfo);
                
                vkBeginCommandBuffer(sec, &beginInfo);
                
                // Re-emit state
                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = (float)Target->get_height();
                viewport.width = (float)Target->get_width();
                viewport.height = -(float)Target->get_height();
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(sec, 0, 1, &viewport);
                
                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = {Target->get_width(), Target->get_height()};
                vkCmdSetScissor(sec, 0, 1, &scissor);
                
                g_vkCmdSetFrontFace(sec, VK_FRONT_FACE_CLOCKWISE);
                
                g_vkRecordingSecondaryGBuffer = true;
                RCache.SetActiveCommandBuffer(sec);
                
                GMBase.r_dsgraph_render_static(0);
                
                g_vkRecordingSecondaryGBuffer = false;
                RCache.SetActiveCommandBuffer(cmd);
                
                vkEndCommandBuffer(sec);
                vkCmdExecuteCommands(cmd, 1, &sec);
            }
        }
        if (rperf) msStatic = tg.GetElapsed_sec()*1000.f;

        // Revert to primary command buffer rendering for dynamic pass
        if (Target->m_bRenderingPassActive)
        {
            vkCmdEndRendering(cmd);
            Target->m_bRenderingPassActive = false;
        }
        Target->phase_scene_begin(); // Re-open LOAD_OP_LOAD pass

        if (rperf) tg.Start();
        {
            vk_ScopedPass passDynamic(cmd, "Dynamic Geometry", vk_colors::GBuffer);
            GMBase.r_dsgraph_render_dynamic(0);
        }
        if (rperf) msDyn = tg.GetElapsed_sec()*1000.f;
        // Target->disable_aniso();

        // PART 1
        Target->phase_scene_begin();
        GMBase.r_dsgraph_capture_hud();

        if (rperf) tg.Start();
        {
            vk_ScopedPass passHUD(cmd, "HUD & 1st-Person Geometry", vk_colors::GBuffer);
            RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x81, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
            GMBase.r_dsgraph_render_hud();
            RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        }
        if (rperf) msHud = tg.GetElapsed_sec()*1000.f;

        if (rperf) tg.Start();
        {
            vk_ScopedPass passLODs(cmd, "Level LODs", vk_colors::GBuffer);
            GMBase.r_dsgraph_render_lods(true, true);
        }
        if (rperf) msLods = tg.GetElapsed_sec()*1000.f;
        Target->phase_scene_end();
    }
    if (rperf) msGBuf = tPass.GetElapsed_sec()*1000.f;

    // ── Phase B: Sun Cascades (Shadow Map + Accumulation) ────────────
    if (rperf) tPass.Start();
    {
        vk_ScopedPass passShadows(cmd, "[Pass] Sun Cascades & Shadow Maps", vk_colors::Shadows);
        
        Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
        BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS);
        
        if (bSUN)
        {
            render_sun_cascades();            // real cascades: smap render + accumulation
            Target->accum_direct_finalize();  // close pass, accumulator -> SHADER_READ_ONLY
        }
        else
        {
            Target->accum_direct(0);
        }
    }
    if (rperf) msShadow = tPass.GetElapsed_sec()*1000.f;

    // ── Forward pass (transparents, particles) moved to phase_combine() ──

    // ── 3. Accumulator pass (stubs until Phase 5 light shaders) ──────────

    // ── 4. Combine — blit rt_Color to the acquired swapchain image ────────
    if (rperf) tPass.Start();
    {
        vk_ScopedPass passCombine(cmd, "[Pass] Deferred Combine & Presentation", vk_colors::PostProcess);
        const uint32_t imageIndex = HW.m_vkCurrentImageIndex;
        Target->phase_combine(cmd, HW.m_vkSCImages[imageIndex], HW.m_vkSCImageViews[imageIndex]);
    }
    if (rperf) msCombine = tPass.GetElapsed_sec()*1000.f;

    if (rperf)
        Msg("  VK-RENDER f%u vis=%.2f gbuf=%.2f [stat=%.2f dyn=%.2f hud=%.2f lods=%.2f] shadow=%.2f combine=%.2f | draws=%u verts=%u polys=%u",
            Device.dwFrame, msVis, msGBuf, msStatic, msDyn, msHud, msLods, msShadow, msCombine,
            RCache.stat.calls, RCache.stat.verts, RCache.stat.polys);

    static bool s_pipeStats = !!strstr(Core.Params, "-vk_pipe_stats");
    if (s_pipeStats) {
        Msg("VK PIPE STATS f%u: %u hits, %u misses, %u compiles", Device.dwFrame, 
            g_vkPrimaryContext.diag_pipeHits, g_vkPrimaryContext.diag_pipeMisses, g_vkPrimaryContext.diag_pipeCompiles);
    }
    static bool s_descStats = !!strstr(Core.Params, "-vk_desc_stats");
    if (s_descStats) {
        Msg("VK DESC STATS f%u: %u built, %u reused, %u binds", Device.dwFrame, 
            g_vkPrimaryContext.s_sets_built, g_vkPrimaryContext.s_sets_reused, g_vkPrimaryContext.s_binds);
    }
    g_vkPrimaryContext.diag_pipeHits = g_vkPrimaryContext.diag_pipeMisses = g_vkPrimaryContext.diag_pipeCompiles = 0;
    g_vkPrimaryContext.s_sets_built = g_vkPrimaryContext.s_sets_reused = g_vkPrimaryContext.s_binds = 0;
}

// ── Skeleton Wallmarks ──────────────────────────────────────────────────────────
void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm) {}
void CRender::add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start, const Fvector& dir, float size) {}
void CRender::add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start, const Fvector& dir, float size) {}

// ── shader_compile ─────────────────────────────────────────────────────────────
// Called by the shared ResourceManager to create shader hardware objects.
//
// Flow:
//   1. Check if pSrcData is already SPIR-V (magic 0x07230203).
//      If yes → create VkShaderModule immediately, store blob as-is.
//   2. If not SPIR-V (raw HLSL text from .vs/.ps files) →
//      call vk_CompileHlslToSpirv() which runs shaderc + disk-caches the result.
//      If compile succeeds → replace blob with SPIR-V and create VkShaderModule.
//   3. Allocate the typed wrapper and return it in `result`.
//      A VK_NULL_HANDLE module is legal here; the pipeline cache will skip any
//      draw call that tries to use an uncompiled shader instead of crashing.
// ─────────────────────────────────────────────────────────────────────────────
HRESULT CRender::shader_compile(
    LPCSTR          name,
    DWORD const*    pSrcData,
    UINT            SrcDataLen,
    LPCSTR          pFunctionName,
    LPCSTR          pTarget,
    DWORD           Flags,
    void*&          result)
{
    result = nullptr;
    if (!pTarget || !pSrcData || SrcDataLen == 0)
        return E_INVALIDARG;

    // ── 1. Try pre-compiled SPIR-V path ──────────────────────────────────────
    VkShaderModule module = vk_CreateShaderModule(
        HW.m_vkDevice, pSrcData, (size_t)SrcDataLen);

    // finalBlob / finalSize hold the blob that the wrapper will own.
    // Defaults to a copy of the raw input; overwritten if HLSL compile succeeds.
    void* finalBlob = xr_malloc(SrcDataLen);
    UINT  finalSize = SrcDataLen;
    CopyMemory(finalBlob, pSrcData, SrcDataLen);

    // ── 2. HLSL runtime compile (shaderc) ────────────────────────────────────
    if (module == VK_NULL_HANDLE)
    {
        // pTarget is "vs_3_0", "ps_4_0", etc.  First two chars identify the stage.
        char stageName[3] = { pTarget[0], pTarget[1], '\0' };
        LPCSTR entry = (pFunctionName && pFunctionName[0]) ? pFunctionName : "main";

        const int m_skinning = Engine.External.GetSkinningMode();
        D3D_SHADER_MACRO defines[128];
        int def_it = 0;

        if (m_skinning < 0)
        {
            defines[def_it].Name = "SKIN_NONE";
            defines[def_it].Definition = "1";
            def_it++;
        }
        else if (0 == m_skinning)
        {
            defines[def_it].Name = "SKIN_0";
            defines[def_it].Definition = "1";
            def_it++;
        }
        else if (1 == m_skinning)
        {
            defines[def_it].Name = "SKIN_1";
            defines[def_it].Definition = "1";
            def_it++;
        }
        else if (2 == m_skinning)
        {
            defines[def_it].Name = "SKIN_2";
            defines[def_it].Definition = "1";
            def_it++;
        }
        else if (3 == m_skinning)
        {
            defines[def_it].Name = "SKIN_3";
            defines[def_it].Definition = "1";
            def_it++;
        }
        else if (4 == m_skinning)
        {
            defines[def_it].Name = "SKIN_4";
            defines[def_it].Definition = "1";
            def_it++;
        }

        // Vulkan renderer mandatory macros to fix HLSL compilation
        defines[def_it].Name = "USE_VK";
        defines[def_it].Definition = "1";
        def_it++;

        defines[def_it].Name = "USE_HWSMAP";
        defines[def_it].Definition = "1";
        def_it++;

        defines[def_it].Name = "USE_HWSMAP_PCF";
        defines[def_it].Definition = "1";
        def_it++;

        // SMAP_size — shadow.h scales shadow-map UVs by float(SMAP_size). Without it,
        // DXC leaves it undefined (broken shadows). Match the shadow RT resolution (2048).
        static char c_smapsize_vk[16];
        xr_sprintf(c_smapsize_vk, "%d", (o.smapsize ? (u32)o.smapsize : 2048u));
        defines[def_it].Name = "SMAP_size";
        defines[def_it].Definition = c_smapsize_vk;
        def_it++;

        // [Path B diagnostic] Only combine_1 needs the inline static-sun branch.
        // Applying it globally can miscompile other shaders that branch on it.

        // If the user has fallback static sun enabled (e.g. sun shadows are off, but they still want light)
        // we inject the inline static sun into combine_1.
        if (name && strstr(name, "combine_1") && RImplementation.o.sunstatic)
        {
            defines[def_it].Name = "USE_R2_STATIC_SUN";
            defines[def_it].Definition = "1";
            def_it++;
        }

        // Shaderc doesn't support legacy DX9 FX technique keywords
        // Redefining FXVS via macros causes "#define redefinition" errors when common.h is included.
        // Instead, we manually strip invocations of FXVS; and FXPS; from the root shader source.
        std::string sourceCode(reinterpret_cast<const char*>(pSrcData), SrcDataLen);
        size_t pos;
        while ((pos = sourceCode.find("FXVS;")) != std::string::npos)
            sourceCode.replace(pos, 5, "     ");
        while ((pos = sourceCode.find("FXPS;")) != std::string::npos)
            sourceCode.replace(pos, 5, "     ");

        defines[def_it].Name = nullptr;
        defines[def_it].Definition = nullptr;

        std::vector<uint32_t> spirv = vk_CompileHlslToSpirv(
            sourceCode.c_str(),
            sourceCode.size(),
            stageName, entry, name, defines);

        if (!spirv.empty())
        {
            // Replace the raw HLSL copy with compiled SPIR-V
            xr_free(finalBlob);
            finalSize = static_cast<UINT>(spirv.size() * sizeof(uint32_t));
            finalBlob = xr_malloc(finalSize);
            CopyMemory(finalBlob, spirv.data(), finalSize);
            module = vk_CreateShaderModule(HW.m_vkDevice, finalBlob, finalSize);
        }
        // If spirv is empty the compile failed; module stays VK_NULL_HANDLE.
        // The pipeline cache guards against null modules and skips those draws.
    }

    // Resolved entry point — used by pipeline cache to set pName correctly.
    LPCSTR resolvedEntry = (pFunctionName && pFunctionName[0]) ? pFunctionName : "main";

    // ── 3. Allocate typed wrapper ─────────────────────────────────────────────
    const char t0 = pTarget[0];
    const char t1 = pTarget[1];

    if (t0 == 'v' && t1 == 's')
    {
        auto* w   = xr_new<VkVertexShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else if (t0 == 'p' && t1 == 's')
    {
        auto* w   = xr_new<VkPixelShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else if (t0 == 'g' && t1 == 's')
    {
        auto* w   = xr_new<VkGeometryShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else if (t0 == 'h' && t1 == 's')
    {
        auto* w   = xr_new<VkHullShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else if (t0 == 'd' && t1 == 's')
    {
        auto* w   = xr_new<VkDomainShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else if (t0 == 'c' && t1 == 's')
    {
        auto* w   = xr_new<VkComputeShaderWrapper>();
        w->pSPIRV = finalBlob;  w->size = finalSize;  w->module = module;
        strncpy_s(w->entryPoint, sizeof(w->entryPoint), resolvedEntry, _TRUNCATE);
        strncpy_s(w->name, sizeof(w->name), name ? name : "", _TRUNCATE);
        result    = w;
    }
    else
    {
        xr_free(finalBlob);
        vk_DestroyShaderModule(HW.m_vkDevice, module);
        return E_FAIL;
    }

    return TRUE;
}

void vk_stat_element_add(R_statistics_element* elem, u32 _verts)
{
    VkRecordContext* ctx = RCache.m_ctx;
    if (!ctx) return;
    
    if (elem == &RCache.stat.r.s_static) { ctx->stat_r.s_static.verts += _verts; ctx->stat_r.s_static.dips++; }
    else if (elem == &RCache.stat.r.s_flora) { ctx->stat_r.s_flora.verts += _verts; ctx->stat_r.s_flora.dips++; }
    else if (elem == &RCache.stat.r.s_flora_lods) { ctx->stat_r.s_flora_lods.verts += _verts; ctx->stat_r.s_flora_lods.dips++; }
    else if (elem == &RCache.stat.r.s_details) { ctx->stat_r.s_details.verts += _verts; ctx->stat_r.s_details.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic) { ctx->stat_r.s_dynamic.verts += _verts; ctx->stat_r.s_dynamic.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_sw) { ctx->stat_r.s_dynamic_sw.verts += _verts; ctx->stat_r.s_dynamic_sw.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_inst) { ctx->stat_r.s_dynamic_inst.verts += _verts; ctx->stat_r.s_dynamic_inst.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_1B) { ctx->stat_r.s_dynamic_1B.verts += _verts; ctx->stat_r.s_dynamic_1B.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_2B) { ctx->stat_r.s_dynamic_2B.verts += _verts; ctx->stat_r.s_dynamic_2B.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_3B) { ctx->stat_r.s_dynamic_3B.verts += _verts; ctx->stat_r.s_dynamic_3B.dips++; }
    else if (elem == &RCache.stat.r.s_dynamic_4B) { ctx->stat_r.s_dynamic_4B.verts += _verts; ctx->stat_r.s_dynamic_4B.dips++; }
    else {
        elem->verts += _verts;
        elem->dips++;
    }
}

// ── Viewport range helpers (mirror R4 RSSetViewports calls) ──────────────────
// X-Ray uses these to partition the [0,1] depth range so that HUD/first-person
// geometry always wins the depth test against world geometry:
//   rmNear   → [0.00, 0.02]  — HUD / first-person models (always in front)
//   rmFar    → [0.999, 1.0]  — sky / far-plane geometry
//   rmNormal → [0.00, 1.00]  — default full range
//
// In Vulkan we achieve the same thing via vkCmdSetViewport with minDepth/maxDepth.
// The negative height + Y-offset is the standard Vulkan Y-flip used throughout
// the rest of the renderer (see u_setrt / phase_scene_begin).
// ─────────────────────────────────────────────────────────────────────────────
void CRender::rmNear()
{
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;
    IRender_Target* T = getTarget();
    VkViewport vp{};
    vp.x        = 0.f;
    vp.y        = (float)T->get_height();
    vp.width    = (float)T->get_width();
    vp.height   = -(float)T->get_height();
    vp.minDepth = 0.f;
    vp.maxDepth = 0.02f;    // HUD depth slice — in front of all world geometry
    vkCmdSetViewport(cmd, 0, 1, &vp);
}

void CRender::rmFar()
{
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;
    IRender_Target* T = getTarget();
    VkViewport vp{};
    vp.x        = 0.f;
    vp.y        = (float)T->get_height();
    vp.width    = (float)T->get_width();
    vp.height   = -(float)T->get_height();
    vp.minDepth = 0.99999f; // Sky / far-plane slice
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
}

void CRender::rmNormal()
{
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;
    IRender_Target* T = getTarget();
    VkViewport vp{};
    vp.x        = 0.f;
    vp.y        = (float)T->get_height();
    vp.width    = (float)T->get_width();
    vp.height   = -(float)T->get_height();
    vp.minDepth = 0.f;      // Full depth range — world geometry
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
}

// ── Level Load / Unload ────────────────────────────────────────────────────────

void CRender::level_Load(IReader* fs)
{
    R_ASSERT(g_pGameLevel);
    R_ASSERT(!b_loaded);

    pApp->LoadBegin();
    // Match every DX loader (r4_loader.cpp, r3, r2, r1): enable deferred texture
    // loading so load-screen and level resources stream correctly during the load phase.
    dxRenderDeviceRender::Instance().Resources->DeferredLoad(TRUE);

    // ── Shaders ───────────────────────────────────────────────────────────────
    g_pGamePersistent->LoadTitle();
    {
        IReader* chunk = fs->open_chunk(fsL_SHADERS);
        R_ASSERT2(chunk, "Level doesn't have shader chunk (fsL_SHADERS). Was it compiled?");
        u32 count = chunk->r_u32();
        Shaders.resize(count);
        for (u32 i = 0; i < count; i++)
        {
            string512 n_sh, n_tlist;
            LPCSTR n = LPCSTR(chunk->pointer());
            chunk->skip_stringZ();
            if (0 == n[0]) continue;
            xr_strcpy(n_sh,   n);
            LPSTR delim = strchr(n_sh, '/');
            if (!delim) continue;
            *delim = 0;
            xr_strcpy(n_tlist, delim + 1);
            Shaders[i] = dxRenderDeviceRender::Instance().Resources->Create(n_sh, n_tlist);
        }
        chunk->close();
    }

    // ── Geometry (VB / IB / SWI) ─────────────────────────────────────────────
    g_pGamePersistent->LoadTitle();
    {
        CStreamReader* geom = FS.rs_open("$level$", "level.geom");
        R_ASSERT2(geom, "level.geom not found");
        LoadBuffers(geom, FALSE);
        LoadSWIs(geom);
        FS.r_close(geom);
    }
    {
        CStreamReader* geomx = FS.rs_open("$level$", "level.geomx");
        if (!geomx)
        {
            Msg("! CRender::level_Load — level.geomx not found, skipping alternate geometry");
        }
        else
        {
            LoadBuffers(geomx, TRUE);
            FS.r_close(geomx);
        }
    }

    // ── Visuals / Sectors / Lights / HOM ─────────────────────────────────────
    // These subsystems will be wired in Phase 4; stub calls keep the load flow
    // consistent with R4 so PhaseN diffs are minimal.
    g_pGamePersistent->LoadTitle();
    {
        IReader* chunk = fs->open_chunk(fsL_VISUALS);
        if (chunk) { LoadVisuals(chunk); chunk->close(); }
    }

    LoadSectors(fs);
    HOM.Load();
    LoadLights(fs);

    // ── Details ───────────────────────────────────────────────────────────────
    Details = xr_new<CDetailManager>();
    Details->Load();

    pApp->LoadEnd();
    b_loaded = TRUE;
}

void CRender::level_Unload()
{
    if (!g_pGameLevel) return;
    if (!b_loaded)     return;

    // WAIT FOR GPU TO IDLE BEFORE DESTROYING IN-USE BUFFERS!
    vkDeviceWaitIdle(HW.m_vkDevice);

    GMBase.clear();
    GMRainWet.clear();

    // ── HOM ───────────────────────────────────────────────────────────────────
    HOM.Unload();

    // ── Details ───────────────────────────────────────────────────────────────
    Details->Unload();
    xr_delete(Details);

    // ── Sectors ───────────────────────────────────────────────────────────────
    xr_delete(rmPortals);
    pLastSector    = nullptr;
    pOutdoorSector = nullptr;
    vLastCameraPos.set(0.f, 0.f, 0.f);
    for (u32 i = 0; i < Sectors.size(); i++) xr_delete(Sectors[i]);
    Sectors.clear();
    for (u32 i = 0; i < Portals.size(); i++) xr_delete(Portals[i]);
    Portals.clear();

    // ── Lights ────────────────────────────────────────────────────────────────
    Lights.Unload();

    // ── Visuals ───────────────────────────────────────────────────────────────
    for (u32 i = 0; i < Visuals.size(); i++)
    {
        Visuals[i]->Release();
        xr_delete(Visuals[i]);
    }
    Visuals.clear();

    // ── SWI ───────────────────────────────────────────────────────────────────
    for (u32 i = 0; i < SWIs.size(); i++) xr_free(SWIs[i].sw);
    SWIs.clear();

    // ── VB / IB ───────────────────────────────────────────────────────────────
    // explicit resource releasing
    for (auto& b : nVB) _RELEASE(b);
    for (auto& b : xVB) _RELEASE(b);
    for (auto& b : nIB) _RELEASE(b);
    for (auto& b : xIB) _RELEASE(b);
    nVB.clear(); xVB.clear();
    nIB.clear(); xIB.clear();
    nDC.clear(); xDC.clear();

    // ── Shaders ───────────────────────────────────────────────────────────────
    Shaders.clear_and_free();

    b_loaded = FALSE;
}

// ── LoadBuffers ───────────────────────────────────────────────────────────────
// Reads vertex and index buffer chunks from the level.geom[x] stream and
// uploads them to device-local VkBuffers via vk_ResourceManager.
//
void CRender::LoadBuffers(CStreamReader* base_fs, BOOL _alternative)
{
    R_ASSERT2(base_fs, "LoadBuffers: null stream reader");

    xr_vector<VertexDeclarator>&   _DC = _alternative ? xDC : nDC;
    xr_vector<ID3DVertexBuffer*>&  _VB = _alternative ? xVB : nVB;
    xr_vector<ID3DIndexBuffer*>&   _IB = _alternative ? xIB : nIB;

    // ── Vertex Buffers ────────────────────────────────────────────────────────
    {
        CStreamReader* fs = base_fs->open_chunk(fsL_VB);
        R_ASSERT2(fs, "LoadBuffers: fsL_VB chunk missing in level.geom[x]");

        u32 count = fs->r_u32();
        _DC.resize(count);
        _VB.resize(count);

        // Scratch buffer for reading the declaration (MAXD3DDECLLENGTH+1 elements)
        const u32 kDeclBufSize = (MAXD3DDECLLENGTH + 1) * sizeof(D3DVERTEXELEMENT9);
        D3DVERTEXELEMENT9* dcl = (D3DVERTEXELEMENT9*)_alloca(kDeclBufSize);

        for (u32 i = 0; i < count; i++)
        {
            // Peek at the declaration to measure its length without consuming it yet.
            fs->r(dcl, kDeclBufSize);
            fs->advance(-(int)kDeclBufSize);

            u32 declLen  = vk_GetDeclLength(dcl) + 1;   // +1 for D3DDECL_END sentinel
            u32 vStride  = vk_GetDeclVertexSize(dcl, 0);

            _DC[i].resize(declLen);
            fs->r(_DC[i].begin(), declLen * sizeof(D3DVERTEXELEMENT9));

            u32 vCount = fs->r_u32();
            Msg("* [VK LoadVB%s] %d verts, stride %d bytes (%d Kb)",
                _alternative ? "x" : "", vCount, vStride, (vCount * vStride) / 1024);

            BYTE* pData = xr_alloc<BYTE>(vCount * vStride);
            fs->r(pData, vCount * vStride);
            _VB[i] = ResourceManager.CreateVertexBuffer(pData, (VkDeviceSize)(vCount * vStride));
            xr_free(pData);
        }
        fs->close();
    }

    // ── Index Buffers ─────────────────────────────────────────────────────────
    {
        CStreamReader* fs = base_fs->open_chunk(fsL_IB);
        R_ASSERT2(fs, "LoadBuffers: fsL_IB chunk missing in level.geom[x]");

        u32 count = fs->r_u32();
        _IB.resize(count);

        for (u32 i = 0; i < count; i++)
        {
            u32 iCount = fs->r_u32();   // number of 16-bit indices
            Msg("* [VK LoadIB%s] %d indices (%d Kb)",
                _alternative ? "x" : "", iCount, (iCount * 2) / 1024);

            BYTE* pData = xr_alloc<BYTE>(iCount * 2);
            fs->r(pData, iCount * 2);
            _IB[i] = ResourceManager.CreateIndexBuffer(pData, (VkDeviceSize)(iCount * 2));
            xr_free(pData);
        }
        fs->close();
    }
}

// ── Loader helpers (Phase 4 stubs) ───────────────────────────────────────────

void CRender::LoadSWIs(CStreamReader* base_fs)
{
    if (!base_fs->find_chunk(fsL_SWIS)) return;
    CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
    if (!fs) return;

    u32 item_count = fs->r_u32();
    for (u32 i = 0; i < SWIs.size(); i++) xr_free(SWIs[i].sw);
    SWIs.clear_not_free();
    SWIs.resize(item_count);

    for (u32 c = 0; c < item_count; c++)
    {
        FSlideWindowItem& swi = SWIs[c];
        swi.reserved[0] = fs->r_u32();
        swi.reserved[1] = fs->r_u32();
        swi.reserved[2] = fs->r_u32();
        swi.reserved[3] = fs->r_u32();
        swi.count       = fs->r_u32();
        VERIFY(nullptr == swi.sw);
        swi.sw = xr_alloc<FSlideWindow>(swi.count);
        fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
    }
    fs->close();
}

void CRender::LoadVisuals(IReader* fs)
{
    IReader* chunk = 0;
    u32 index = 0;
    dxRender_Visual* V = 0;
    ogf_header H;

    while ((chunk = fs->open_chunk(index)) != 0)
    {
        chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
        V = Models->Instance_Create(H.type);
        V->Load(0, chunk, 0);
        Visuals.push_back(V);

        chunk->close();
        index++;
    }
}

void CRender::LoadLights(IReader* fs)
{
    Lights.Load(fs);
    Lights.LoadHemi();
}

struct b_portal
{
    u16 sector_front;
    u16 sector_back;
    svector<Fvector, 6> vertices;
};

void CRender::LoadSectors(IReader* fs)
{
    // allocate memory for portals
    u32 size = fs->find_chunk(fsL_PORTALS);
    R_ASSERT(0==size%sizeof(b_portal));
    u32 count = size / sizeof(b_portal);
    Portals.resize(count);
    for (u32 c = 0; c < count; c++)
        Portals[c] = xr_new<CPortal>();

    // load sectors
    IReader* S = fs->open_chunk(fsL_SECTORS);
    for (u32 i = 0; ; i++)
    {
        IReader* P = S->open_chunk(i);
        if (0 == P) break;

        CSector* __S = xr_new<CSector>();
        __S->load(*P);
        Sectors.push_back(__S);

        P->close();
    }
    S->close();

    // load portals
    if (count)
    {
        CDB::Collector CL;
        fs->find_chunk(fsL_PORTALS);
        for (u32 i = 0; i < count; i++)
        {
            b_portal P;
            fs->r(&P, sizeof(P));
            CPortal* __P = (CPortal*)Portals[i];
            __P->Setup(P.vertices.begin(), P.vertices.size(),
                       (CSector*)getSector(P.sector_front),
                       (CSector*)getSector(P.sector_back));
            for (u32 j = 2; j < P.vertices.size(); j++)
                CL.add_face_packed_D(
                    P.vertices[0], P.vertices[j - 1], P.vertices[j],
                    u32(i)
                );
        }
        if (CL.getTS() < 2)
        {
            Fvector v1, v2, v3;
            v1.set(-20000.f, -20000.f, -20000.f);
            v2.set(-20001.f, -20001.f, -20001.f);
            v3.set(-20002.f, -20002.f, -20002.f);
            CL.add_face_packed_D(v1, v2, v3, 0);
        }

        // build portal model
        rmPortals = xr_new<CDB::MODEL>();
        rmPortals->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
    }
    else
    {
        rmPortals = 0;
    }

    pLastSector = 0;

    // Search for default sector - assume "default" or "outdoor" sector is the largest one
    CSector* largest_sector = 0;
    float largest_sector_vol = 0;
    for (u32 s = 0; s < Sectors.size(); s++)
    {
        CSector* sec = (CSector*)Sectors[s];
        dxRender_Visual* V = sec->root();
        float vol = V->vis.box.getvolume();
        if (vol > largest_sector_vol)
        {
            largest_sector_vol = vol;
            largest_sector = sec;
        }
    }
    pOutdoorSector = largest_sector;
}

// ─── Screenshot stubs ─────────────────────────────────────────────────────────
// TakeScreenshot, Screenshot, ScreenshotAsyncBegin/End, and ScreenshotImpl are
// already provided (with empty VK bodies) by the shared r__screenshot.cpp.
IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent) { return xr_new<CROS_impl>(); }
void CRender::ros_destroy(IRender_ObjectSpecific* & p) { xr_delete(p); }
IRender_Light* CRender::light_create() { return Lights.Create(); }
IRender_Glow* CRender::glow_create() { return xr_new<CGlow>(); }

IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data) { return Models->Create(name, data); }
IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data) { return Models->CreateChild(name, data); }
IRenderVisual* CRender::model_Duplicate(IRenderVisual* V) { return Models->Instance_Duplicate((dxRender_Visual*)V); }

void CRender::model_Delete(IRenderVisual*& V, BOOL bDiscard)
{
	dxRender_Visual* pVisual = (dxRender_Visual*)V;
    if (Models)
	    Models->Delete(pVisual, bDiscard);
	V = 0;
}

void CRender::model_Delete_Deffered(IRenderVisual*& V)
{
	dxRender_Visual* pVisual = (dxRender_Visual*)V;
	Models->DeleteDeffered(pVisual);
	V = 0;
}

IRenderVisual* CRender::model_CreatePE(LPCSTR name)
{
    PS::CPEDef* SE = PSLibrary.FindPED(name);
    R_ASSERT3(SE, "Particle effect doesn't exist", name);
    return Models->CreatePE(SE);
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
	PS::CPEDef* SE = PSLibrary.FindPED(name);
	if (SE) return Models->CreatePE(SE);
	else
	{
		PS::CPGDef* SG = PSLibrary.FindPGD(name);
		R_ASSERT3(SG, "Particle effect or group doesn't exist", name);
		return Models->CreatePG(SG);
	}
}

void CRender::model_Logging(BOOL bEnable) { Models->Logging(bEnable); }
void CRender::models_Prefetch() { Models->Prefetch(); }
void CRender::models_PrefetchOne(LPCSTR name, bool assert) { Models->Prefetch_One(name, assert); }
void CRender::models_Clear(BOOL b_complete) { Models->ClearPool(b_complete); }
bool CRender::models_Exists(LPCSTR name) { return Models->Exists(name); }

void CRender::TakeScreenshot(LPCSTR /*path*/, Fvector2 /*dimensions*/, DxEncoding /*encoding*/)
{
    // NYI under VK
}

void CRender::ScreenshotAsyncEnd(CMemoryWriter& /*memory_writer*/)
{
    // NYI under VK
}

IRenderVisual* CRender::getVisual(int id)
{
    VERIFY(id < int(Visuals.size()));
    return Visuals[id];
}

void CRender::FlushAndCloseUIPass()
{
    if (!Target) return;
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (cmd && Target->m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        Target->m_bRenderingPassActive = false;
    }
    RCache.set_RT(nullptr, 0);
    RCache.set_RT(nullptr, 1);
    RCache.set_ZB(nullptr);
}

void CRender::RenderToTarget(RRT target)
{
    if (!Target) return;

    // Close the UI pass that pda->Draw() opened, then copy the swapchain image
    // (which now holds the PDA 2D UI) into the PDA RT texture that the 3D PDA
    // mesh samples. Mirrors r4_R_render.cpp:382-403 (CopyResource) via vkCmdCopyImage.
    FlushAndCloseUIPass();

    ref_rt* RT = nullptr;
    switch (target)
    {
    case rtPDA: RT = &Target->rt_ui_pda;   break;   // add this RT if not present
    case rtSVP: RT = &Target->rt_secondVP; break;
    default:    return;
    }
    if (!RT || !*RT || !(*RT)->pRT) return;

    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;

    const uint32_t imageIndex = HW.m_vkCurrentImageIndex;
    VkImage src = HW.m_vkSCImages[imageIndex];
    VkImage dst = (*RT)->pRT->image;

    // src (swapchain) COLOR_ATTACHMENT → TRANSFER_SRC ; dst (RT) → TRANSFER_DST
    vk_TransitionImageLayout(cmd, src, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    vk_TransitionImageLayout(cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT,
        (*RT)->pRT->currentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.srcOffsets[1] = {(int32_t)HW.m_vkSCExtent.width, (int32_t)HW.m_vkSCExtent.height, 1};
    region.dstSubresource = region.srcSubresource;
    region.dstOffsets[1] = {(int32_t)(*RT)->pRT->width, (int32_t)(*RT)->pRT->height, 1};
    vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &region, VK_FILTER_LINEAR);

    // Restore layouts: swapchain back to COLOR_ATTACHMENT, RT to SHADER_READ_ONLY.
    vk_TransitionImageLayout(cmd, src, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    vk_TransitionImageLayout(cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    (*RT)->pRT->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
