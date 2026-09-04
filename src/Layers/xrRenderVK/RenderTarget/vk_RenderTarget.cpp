#include "stdafx.h"
#include "vk_RenderTarget.h"
#include "../r2_types.h"
#include "../../xrRender/FVF.h"
#include "../../../xrEngine/igame_persistent.h"
#include "../../../xrEngine/environment.h"
#include "../../xrRender/LightTrack.h"
#include "../blender_light_direct.h"
#include "../blender_light_mask.h"
#include "../../xrRender/dxRenderDeviceRender.h"
#include "../Resources/vk_Texture.h"
extern ENGINE_API float psHUD_FOV;

VkSampler g_smp_nofilter = VK_NULL_HANDLE;
VkSampler g_smp_linear   = VK_NULL_HANDLE;
VkSampler g_smp_rtlinear = VK_NULL_HANDLE;
VkSampler g_smp_material = VK_NULL_HANDLE;
VkSampler g_smp_smap     = VK_NULL_HANDLE;

static VkSampler CreateSamplerVK(VkFilter filter, VkSamplerAddressMode addr,
                                 bool compare = false)
{
    VkSamplerCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter    = filter;
    ci.minFilter    = filter;
    ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.addressModeU = addr;
    ci.addressModeV = addr;
    ci.addressModeW = addr;
    ci.maxLod       = VK_LOD_CLAMP_NONE;
    if (compare)
    {
        ci.compareEnable = VK_TRUE;
        ci.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
        ci.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    else
    {
        ci.borderColor   = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    }
    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(HW.m_vkDevice, &ci, nullptr, &s);
    return s;
}

// Helper: current per-frame command buffer.
// In tool mode Spherical passes the command buffer via preRenderCallback;
// XrRenderVK_RenderSceneTool stores it in HW.m_vkToolCmd before calling phases.
static inline VkCommandBuffer GetFrameCmd()
{
    if (HW.m_bToolMode)
        return HW.m_vkToolCmd;
    const uint32_t slot = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;
    return HW.m_vkCmdBuffers[slot];
}

CRenderTarget::CRenderTarget()
{
    dwWidth = Device.dwWidth;
    dwHeight = Device.dwHeight;

    m_bRenderingPassActive     = false;
    m_activeRendertargetsCount = 0;
    m_lastSwapchainClearFrame  = ~0u;
    dwAccumulatorClearMark     = u32(-1);
    dwLightMarkerID            = 5;

    Msg("* [RT] creating render targets %dx%d", dwWidth, dwHeight);

    rt_Position.create  (r2_RT_P,        dwWidth, dwHeight, D3DFMT_A16B16G16R16F);
    rt_Normal.create    (r2_RT_N,        dwWidth, dwHeight, D3DFMT_A16B16G16R16F);
    rt_Color.create     (r2_RT_albedo,   dwWidth, dwHeight, D3DFMT_A8R8G8B8);
    rt_Accumulator.create(r2_RT_accum,   dwWidth, dwHeight, D3DFMT_A16B16G16R16F);

    rt_Generic_0.create("rt_Generic_0",  dwWidth, dwHeight, D3DFMT_A8R8G8B8);
    rt_Generic_1.create("rt_Generic_1",  dwWidth, dwHeight, D3DFMT_A8R8G8B8);
    rt_ui_pda.create(r2_RT_ui, dwWidth, dwHeight, D3DFMT_A8R8G8B8);
    rt_secondVP.create("rt_secondVP", dwWidth, dwHeight, D3DFMT_A8R8G8B8);

    rt_smap_surf.create (r2_RT_smap_surf,  2048, 2048, D3DFMT_R32F);
    rt_smap_depth.create(r2_RT_smap_depth, 2048, 2048, D3DFMT_D24X8);
    rt_smap_depth_minmax.create(r2_RT_smap_depth_minmax, 4, 4, D3DFMT_R32F);

    t_dummy_material = dxRenderDeviceRender::Instance().Resources->_CreateTexture(r2_material);
    t_dummy_sunmask = dxRenderDeviceRender::Instance().Resources->_CreateTexture("sunmask");

    m_material_lut_wrapper = new VkTexture2DWrapper();
    if (vk_CreateMaterialLUT(*m_material_lut_wrapper) == VK_SUCCESS)
    {
        t_dummy_material->surface_set(m_material_lut_wrapper);
    }

    // Bind the surface of a known white texture to our dummy texture refs
    ref_texture t_white = dxRenderDeviceRender::Instance().Resources->_CreateTexture("door\\door_white_01");
    if (t_white && t_white->surface_get())
    {
        t_dummy_sunmask->surface_set(t_white->surface_get());
    }

    g_smp_nofilter = CreateSamplerVK(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_smp_linear   = CreateSamplerVK(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_REPEAT);
    g_smp_rtlinear = CreateSamplerVK(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_smp_material = CreateSamplerVK(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_smp_smap     = CreateSamplerVK(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, true);

    Msg("* [RT] render targets created, setting up depth buffer");

    pZB.image         = HW.m_vkDepthImage;
    pZB.view          = HW.m_vkDepthView;
    pZB.format        = HW.m_vkDepthFormat;
    pZB.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (rt_smap_depth && rt_smap_depth->pZRT)
    {
        rt_smap_dsv.image         = rt_smap_depth->pZRT->image;
        rt_smap_dsv.view          = rt_smap_depth->pZRT->view;
        rt_smap_dsv.format        = static_cast<VkDSVWrapper*>(rt_smap_depth->pZRT)->format;
        rt_smap_dsv.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    Msg("* [RT] creating s_combine shader");
    {
        CBlender_combine b_combine;
        s_combine.create(&b_combine, "r2\\combine");
    }
    Msg("* [RT] s_combine created, creating s_accum_direct");
    {
        CBlender_accum_direct b_accum_direct;
        s_accum_direct.create(&b_accum_direct, "r2\\accum_direct_cascade");
    }
    Msg("* [RT] creating s_accum_mask");
    {
        CBlender_accum_direct_mask b_accum_mask;
        s_accum_mask.create(&b_accum_mask, "r3\\accum_mask");
    }
    Msg("* [RT] creating s_occq");
    s_occq.create("hidden", "occq");
    Msg("* [RT] s_accum_direct created, creating g_combine geometry");
    g_combine.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
    Msg("* [RT] constructor done");
}

CRenderTarget::~CRenderTarget()
{
    rt_Position.destroy();
    rt_Normal.destroy();
    rt_Color.destroy();
    rt_Accumulator.destroy();
    rt_Generic_0.destroy();
    rt_Generic_1.destroy();
    rt_ui_pda.destroy();
    rt_secondVP.destroy();
    rt_smap_surf.destroy();
    rt_smap_depth.destroy();
    rt_smap_depth_minmax.destroy();

    if (t_dummy_material) t_dummy_material->surface_set(0);
    if (t_dummy_sunmask) t_dummy_sunmask->surface_set(0);
    if (m_material_lut_wrapper) {
        vk_DestroyTexture2D(*m_material_lut_wrapper);
        delete m_material_lut_wrapper;
    }

    if (g_smp_nofilter) vkDestroySampler(HW.m_vkDevice, g_smp_nofilter, nullptr);
    if (g_smp_linear)   vkDestroySampler(HW.m_vkDevice, g_smp_linear,   nullptr);
    if (g_smp_rtlinear) vkDestroySampler(HW.m_vkDevice, g_smp_rtlinear, nullptr);
    if (g_smp_material) vkDestroySampler(HW.m_vkDevice, g_smp_material, nullptr);
    if (g_smp_smap)     vkDestroySampler(HW.m_vkDevice, g_smp_smap,     nullptr);

    // pZB and rt_smap_dsv are non-owning wrappers — do NOT destroy here.
}

void CRenderTarget::reset_begin()
{
    // Normally destroy RTs here. For now, rely on Target recreation or
    // leave them alone since they are screen-sized and might be recreated
    // inside their own logic if dwWidth/dwHeight change.
}

void CRenderTarget::reset_end()
{
    // Refresh the hardware depth buffer handle which was recreated by CHW::Reset
    pZB.image         = HW.m_vkDepthImage;
    pZB.view          = HW.m_vkDepthView;
    pZB.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// ─────────────────────────────────────────────────────────────────────────────
// u_setrt — bind render targets and begin a VkCmdBeginRendering pass
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb, VkRenderingFlags flags)
{
    u_setrt(dwWidth, dwHeight,
        _1 ? _1->pRT : nullptr,
        _2 ? _2->pRT : nullptr,
        _3 ? _3->pRT : nullptr,
        zb, flags);
}

void CRenderTarget::u_setrt(u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3, ID3DDepthStencilView* zb, VkRenderingFlags flags)
{
    VkCommandBuffer cmd = GetFrameCmd();

    // End previous rendering pass if active
    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        m_bRenderingPassActive = false;
    }

    if (!_1 && !zb) return;

    // Sync RCache so pipeline hashes get the correct formats
    RCache.set_RT(_1, 0);
    RCache.set_RT(_2, 1);
    RCache.set_RT(_3, 2);
    RCache.set_RT(nullptr, 3);
    RCache.set_ZB(zb);

    uint32_t rtCount = 0;
    VkRenderingAttachmentInfo colorAttachments[3]{};
    
    // Batch barriers
    VkImageMemoryBarrier barriers[4]{};
    uint32_t barrierCount = 0;
    
    auto buildBarrier = [&](VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier& b = barriers[barrierCount++];
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.pNext = nullptr;
        b.srcAccessMask = (VkAccessFlags)srcAccess;
        b.dstAccessMask = (VkAccessFlags)dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = aspect;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    };

    auto setupColorAttachment = [&](ID3DRenderTargetView* rtv, uint32_t index) {
        if (!rtv) return;

        VkImageLayout oldLayout = rtv->currentLayout;

        if (rtv->currentLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            buildBarrier(rtv->image, VK_IMAGE_ASPECT_COLOR_BIT,
                rtv->currentLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            rtv->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        colorAttachments[index].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[index].imageView   = rtv->view;
        colorAttachments[index].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[index].loadOp      = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                              ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                              : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachments[index].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        if (colorAttachments[index].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            colorAttachments[index].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        rtCount = index + 1;
    };

    setupColorAttachment(_1, 0);
    setupColorAttachment(_2, 1);
    setupColorAttachment(_3, 2);

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth   = (zb != nullptr);
    bool hasStencil = false;
    if (hasDepth)
    {
        // Determine stencil aspect only for formats that have a stencil plane.
        hasStencil = (HW.m_vkDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
                      HW.m_vkDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                      HW.m_vkDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT);
        VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT |
                                         (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u);

        VkImageLayout oldLayout = zb->currentLayout;

        if (zb->currentLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            buildBarrier(zb->image, depthAspect,
                zb->currentLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            zb->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = zb->view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (depthAttachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            depthAttachment.clearValue.depthStencil = {1.0f, 0};
    }

    // Flush all batched barriers (color + depth) in ONE call, after all buildBarrier calls
    if (barrierCount > 0)
    {
        vk_TransitionImages(cmd, barriers, barrierCount,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    VkRenderingInfo renderInfo{};
    renderInfo.sType                    = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent.width  = W;
    renderInfo.renderArea.extent.height = H;
    renderInfo.layerCount               = 1;
    renderInfo.flags                    = flags;
    renderInfo.colorAttachmentCount     = rtCount;
    renderInfo.pColorAttachments        = rtCount > 0 ? colorAttachments : nullptr;
    // Only set stencil attachment if the depth format actually has a stencil plane.
    // Passing a depth-only image view as pStencilAttachment is a spec violation.
    renderInfo.pDepthAttachment         = hasDepth ? &depthAttachment : nullptr;
    renderInfo.pStencilAttachment       = (hasDepth && hasStencil) ? &depthAttachment : nullptr;

    vkCmdBeginRendering(cmd, &renderInfo);
    RCache.m_ctx->m_pipelineDirty = true; // NEW: force full re-eval on the first draw of every new pass instance

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = (float)H;
    viewport.width    = (float)W;
    viewport.height   = -(float)H;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {W, H};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    m_bRenderingPassActive     = true;
    m_activeRendertargetsCount = rtCount;

    // Clear the dirty flag so vk_EnsureRenderPassActive doesn't immediately 
    // kill this explicitly crafted pass on the next draw call.
    RCache.CheckAndResetRenderPassDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// phase_scene_prepare — clear G-Buffer and depth at the start of the frame
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::phase_scene_prepare()
{
    vk_ScopedPass passPrep(GetFrameCmd(), "Clear G-Buffer & Depth", vk_colors::GBuffer);

    // DX11's immediate context resets pipeline state per-draw, so its scene starts
    // clean even after the 3D-PDA path draws ~120 UI primitives first (see DX11
    // capture actions 2-122, then phase_scene_prepare at 124). VK CACHES state and
    // only rebinds on change, so those UI draws' blend/stencil/cull/vertex-layout
    // state leak into the scene's first G-buffer draws → glitched position/color and
    // the PDA losing the depth fight. Invalidate() (same call OnFrameBegin uses)
    // forces the scene to rebind every state fresh, matching DX11's clean start.
    RCache.InvalidateCtx();

    // Force RTs to UNDEFINED so u_setrt clears them properly ONCE per frame
    // r3 G-Buffer is 2-RT packed: slot0=rt_Position (packed normal+depth+hemi), slot1=rt_Color (albedo+gloss)
    // rt_Normal is NOT a separate MRT in r3 — normals are encoded into rt_Position.xy via gbuf_pack_normal()
    if (rt_Position && rt_Position->pRT) rt_Position->pRT->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (rt_Color    && rt_Color->pRT)    rt_Color->pRT->currentLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
    pZB.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    RCache.set_RT(rt_Position ? rt_Position->pRT : nullptr, 0);
    RCache.set_RT(rt_Color    ? rt_Color->pRT    : nullptr, 1);
    RCache.set_RT(nullptr, 2);
    RCache.set_ZB(&pZB);
    u_setrt(rt_Position, rt_Color, ref_rt(), &pZB);

    // We end the pass immediately because we just wanted to issue the LOAD_OP_CLEAR
    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(GetFrameCmd());
        m_bRenderingPassActive = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// phase_scene_begin — open G-Buffer pass (rt_Position + rt_Color + depth; r3 2-RT packed layout)
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::phase_scene_begin(VkRenderingFlags flags)
{

    // r3 G-Buffer: slot0=rt_Position (packed), slot1=rt_Color (albedo+gloss)
    RCache.set_RT(rt_Position ? rt_Position->pRT : nullptr, 0);
    RCache.set_RT(rt_Color    ? rt_Color->pRT    : nullptr, 1);
    RCache.set_RT(nullptr, 2);
    RCache.set_ZB(&pZB);
    u_setrt(rt_Position, rt_Color, ref_rt(), &pZB, flags);

    // Stencil - write 0x1 at pixel pos
    RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);

    // Misc - draw only front-faces
    RCache.set_CullMode(CULL_CCW);
    RCache.set_ColorWriteEnable();

    // Fix: Explicitly re-enable Depth Test/Write/Compare to recover from Invalidate()
    RCache.set_Z(TRUE);
    RCache.set_ZWrite(TRUE);
    RCache.set_ZFunc(D3DCMP_LESSEQUAL);
}

// ─────────────────────────────────────────────────────────────────────────────
// phase_scene_end — close G-Buffer pass; transition G-Buffer → SHADER_READ_ONLY
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::phase_scene_end()
{
    VkCommandBuffer cmd = GetFrameCmd();

    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        m_bRenderingPassActive = false;
    }

    // Transition G-Buffer images so the lighting/combine pass can sample them.
    // r3 G-Buffer is 2 RTs only: rt_Position (packed) and rt_Color (albedo).
    
    VkImageMemoryBarrier barriers[2];
    uint32_t barrierCount = 0;
    
    auto transitionToRead = [&](ref_rt& rt) {
        if (!rt || !rt->pRT) return;
        VkImageMemoryBarrier& b = barriers[barrierCount++];
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.pNext = nullptr;
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = rt->pRT->currentLayout;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = rt->pRT->image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        
        rt->pRT->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };

    transitionToRead(rt_Position);
    transitionToRead(rt_Color);

    if (barrierCount > 0)
    {
        vk_TransitionImages(cmd, barriers, barrierCount,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    }

    // Clear RCache RT slots so no stale pointers remain.
    RCache.set_RT(nullptr, 0);
    RCache.set_RT(nullptr, 1);
    RCache.set_RT(nullptr, 2);
    RCache.set_ZB(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// phase_combine — deferred combine quad → rt_Generic_0, then blit to swapchain
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::phase_combine(VkCommandBuffer cmd, VkImage swapchainImage, VkImageView /*swapchainView*/)
{
    if (!s_combine || !g_combine) return;

    // ── 1. Force-clear rt_Generic_0/1 on first use this frame ────────────────
    if (rt_Generic_0 && rt_Generic_0->pRT) rt_Generic_0->pRT->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (rt_Generic_1 && rt_Generic_1->pRT) rt_Generic_1->pRT->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // ── 2. Open rendering pass targeting rt_Generic_0 (low) + rt_Generic_1 (high) ──
    u_setrt(rt_Generic_0, rt_Generic_1, ref_rt(), &pZB);
    RCache.set_RT(rt_Generic_0 ? rt_Generic_0->pRT : nullptr, 0);
    RCache.set_RT(rt_Generic_1 ? rt_Generic_1->pRT : nullptr, 1);
    RCache.set_ZB(&pZB);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable();

    // Sky must NOT touch stencil. It is drawn before the stencil-masked combine
    // quad; if it inherits the scene pass's REPLACE/ref=1 stencil state it will
    // write stencil=1 into sky pixels, causing the combine quad (test 1<=stencil)
    // to pass on the sky and overwrite it. Force stencil off for the sky draws.
    RCache.set_Stencil(FALSE);

    {
        vk_ScopedPass passSky(cmd, "Environment Sky & Clouds", vk_colors::PostProcess);
        if (g_pGamePersistent)
        {
            g_pGamePersistent->Environment().RenderSky();
            g_pGamePersistent->Environment().RenderClouds();
        }
    }

    // ── 3. Gather environment & sun constants (mirrors R4 phase_combine) ──────
    Fvector4 ambclr = { 0.2f, 0.2f, 0.2f, 0.f };
    Fvector4 envclr = { 0.5f, 0.5f, 0.5f, 1.f };
    Fvector4 fogclr = { 0.f,  0.f,  0.f,  0.f };
    Fvector4 sunclr = { 1.f, 0.95f, 0.8f, 0.5f };
    Fvector4 sundir = { 0.f, -1.f, 0.f, 0.f };

    if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
    {
        CEnvDescriptorMixer& envdesc = *g_pGamePersistent->Environment().CurrentEnv;
        const float minamb = 0.001f;
        ambclr.set(
            _max(envdesc.ambient.x * 2.f, minamb),
            _max(envdesc.ambient.y * 2.f, minamb),
            _max(envdesc.ambient.z * 2.f, minamb), 0.f);
        envclr.set(
            envdesc.hemi_color.x * 2.f + EPS,
            envdesc.hemi_color.y * 2.f + EPS,
            envdesc.hemi_color.z * 2.f + EPS,
            envdesc.weight);
        fogclr.set(envdesc.fog_color.x, envdesc.fog_color.y, envdesc.fog_color.z, 0.f);

        light* fuckingsun = (light*)RImplementation.Lights.sun_adapted._get();
        if (fuckingsun)
        {
            Fvector L_dir, L_clr;
            L_clr.set(fuckingsun->color.r, fuckingsun->color.g, fuckingsun->color.b);
            // u_diffuse2s: approximate specular intensity from diffuse
            float L_spec = L_clr.x * 0.3f + L_clr.y * 0.48f + L_clr.z * 0.22f;
            Device.mView.transform_dir(L_dir, fuckingsun->direction);
            L_dir.normalize();
            sunclr.set(L_clr.x, L_clr.y, L_clr.z, L_spec);
            sundir.set(L_dir.x, L_dir.y, L_dir.z, 0.f);
        }
    }

    if (strstr(Core.Params, "-no_env"))
    {
        ambclr.set(0.f, 0.f, 0.f, 0.f);
        envclr.set(0.f, 0.f, 0.f, 0.f);
    }

    // ── 4. Fill FVF::TL fullscreen NDC quad ──────────────────────────────────
    u32 Offset;
    const float scale_X = float(Device.dwWidth)  / float(TEX_jitter);
    const float scale_Y = float(Device.dwHeight) / float(TEX_jitter);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f,  1.f, 0.f, 1.f, 0, 0.f,     scale_Y); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0, 0.f,     0.f);     pv++;
    pv->set( 1.f,  1.f, 1.f, 1.f, 0, scale_X, scale_Y); pv++;
    pv->set( 1.f, -1.f, 1.f, 0.f, 0, scale_X, 0.f);     pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // ── 5. Draw — combine_1.vs + combine_1_nomsaa.ps ─────────────────────────
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);

    RCache.set_Element(s_combine->E[0]);
    RCache.set_Geometry(g_combine);

    RCache.set_c("m_v2w",          Device.mInvView);

    RCache.set_c("L_ambient",      ambclr);
    RCache.set_c("Ldynamic_color", sunclr);
    RCache.set_c("Ldynamic_dir",   sundir);
    // RCache.set_c("env_color",      envclr); // Now handled by binder_env_color
    RCache.set_c("fog_color",      fogclr);

    /* Now handled by binder_pos_decompression_params
    float VertTan = -1.0f * tanf(deg2rad(Device.fFOV / 2.0f));
    float HorzTan = -VertTan / Device.fASPECT;
    RCache.set_c("pos_decompression_params",
                 HorzTan, VertTan,
                 (2.0f * HorzTan) / (float)Device.dwWidth,
                 (2.0f * VertTan) / (float)Device.dwHeight);
    RCache.set_c("pos_decompression_params2",
                 (float)Device.dwWidth, (float)Device.dwHeight,
                 1.0f / (float)Device.dwWidth, 1.0f / (float)Device.dwHeight);
    */

    {
        vk_ScopedPass passCombine1(cmd, "Deferred Combine Shading (combine_1)", vk_colors::PostProcess);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }

    // End the combine_1 render pass since render_forward needs a different attachment setup
    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        m_bRenderingPassActive = false;
    }

    // Forward rendering (transparents, particles, HUD etc.)
    // Only bind rt_Generic_0 and the base depth buffer to match DX11 logic,
    // avoiding pipeline compilation failure on colorFormatCount mismatch!
    u_setrt(rt_Generic_0, nullptr, nullptr, &pZB);
    RCache.set_RT(rt_Generic_0 ? rt_Generic_0->pRT : nullptr, 0);
    RCache.set_RT(nullptr, 1);
    RCache.set_ZB(&pZB);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();

    RImplementation.render_forward();

    // End the forward render pass
    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        m_bRenderingPassActive = false;
    }
    RCache.set_RT(nullptr, 0);
    RCache.set_RT(nullptr, 1);
    // RCache.set_ZB(nullptr);  // Reverted: UI dynamic rendering needs the depth attachment format to compile stencil pipelines
    RCache.set_Stencil(FALSE);

    // ── 6. Blit rt_Generic_0 → swapchain ─────────────────────────────────────
    if (!rt_Generic_0 || !rt_Generic_0->pRT) return;
    VkImage srcImage = rt_Generic_0->pRT->image;

    vk_ScopedPass passBlit(cmd, "Blit rt_Generic_0 -> Swapchain Backbuffer", vk_colors::PostProcess);

    vk_TransitionImageLayout(cmd, srcImage, VK_IMAGE_ASPECT_COLOR_BIT,
        rt_Generic_0->pRT->currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    rt_Generic_0->pRT->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    vk_TransitionImageLayout(cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.srcOffsets[0] = {0, 0, 0};
    region.srcOffsets[1] = {(int32_t)dwWidth, (int32_t)dwHeight, 1};
    region.dstSubresource = region.srcSubresource;
    region.dstOffsets[0] = {0, 0, 0};
    region.dstOffsets[1] = {(int32_t)HW.m_vkSCExtent.width, (int32_t)HW.m_vkSCExtent.height, 1};
    vkCmdBlitImage(cmd,
        srcImage,       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region, VK_FILTER_LINEAR);

    // Transition swapchain → COLOR_ATTACHMENT_OPTIMAL so UI can draw over it
    vk_TransitionImageLayout(cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    m_lastSwapchainClearFrame = Device.dwFrame;
}

// ─────────────────────────────────────────────────────────────────────────────
// phase_accumulator — bind rt_Accumulator; clear it on the first call per frame
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::reset_light_marker(bool bResetStencil)
{
    dwLightMarkerID = 5;
    if (bResetStencil)
    {
        u32 Offset;
        float _w = float(Device.dwWidth);
        float _h = float(Device.dwHeight);
        u32 C = color_rgba(255, 255, 255, 255);
        float eps = 0;
        float _dw = 0.5f;
        float _dh = 0.5f;
        FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-_dw, _h - _dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(-_dw, -_dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(_w - _dw, _h - _dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(_w - _dw, -_dh, eps, 1.f, C, 0, 0);
        pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);
        RCache.set_Element(s_occq->E[2]);
        RCache.set_Geometry(g_combine);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }
}

void CRenderTarget::increment_light_marker()
{
    dwLightMarkerID += 2;
    const u32 iMaxMarkerValue = 255;
    if (dwLightMarkerID > iMaxMarkerValue)
        reset_light_marker(true);
}

void CRenderTarget::phase_accumulator()
{
    if (!rt_Accumulator || !rt_Accumulator->pRT) return;

    if (dwAccumulatorClearMark != Device.dwFrame)
    {
        // First accumulator call this frame — force LOAD_OP_CLEAR via UNDEFINED layout
        dwAccumulatorClearMark = Device.dwFrame;
        rt_Accumulator->pRT->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        u_setrt(rt_Accumulator, ref_rt(), ref_rt(), &pZB);
        RCache.set_RT(rt_Accumulator->pRT, 0);
        RCache.set_ZB(&pZB);

        reset_light_marker();

        RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
        RCache.set_Z(FALSE); // Prevents depth testing against the scene, but allows stencil test
        RCache.set_CullMode(CULL_NONE);
        RCache.set_ColorWriteEnable();
    }
    else
    {
        u_setrt(rt_Accumulator, ref_rt(), ref_rt(), &pZB);
        RCache.set_RT(rt_Accumulator->pRT, 0);
        RCache.set_ZB(&pZB);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Accumulator stubs (Phase 5 will add real light-volume shaders)
// ─────────────────────────────────────────────────────────────────────────────
void CRenderTarget::accum_direct(u32 /*sub_phase*/)
{
    // Phase 13: Call phase_accumulator to clear rt_Accumulator to black (zero
    // dynamic lights). combine_1.ps adds static sun + hemisphere lighting inline
    // via USE_R2_STATIC_SUN + hmodel(), so a zero accumulator still produces a
    // correctly lit scene. Real dynamic light accumulation comes in Phase 5.
    VkCommandBuffer cmd = GetFrameCmd();

    phase_accumulator();

    // Close the accumulator render pass (we drew nothing this phase)
    if (m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        m_bRenderingPassActive = false;
    }

    // Transition rt_Accumulator → SHADER_READ_ONLY so combine_1.ps can sample it
    if (rt_Accumulator && rt_Accumulator->pRT)
    {
        vk_TransitionImageLayout(
            cmd, rt_Accumulator->pRT->image, VK_IMAGE_ASPECT_COLOR_BIT,
            rt_Accumulator->pRT->currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        rt_Accumulator->pRT->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    RCache.set_RT(nullptr, 0);
    RCache.set_ZB(nullptr);
}

void CRenderTarget::accum_point(class light* /*L*/)
{
    // Phase 5 stub.
}

void CRenderTarget::accum_spot(class light* /*L*/)
{
    // Phase 5 stub.
}

void CRenderTarget::phase_smap_direct(class light* L, u32 sub_phase)
{
    if (strstr(Core.Params, "-pb_no_smap")) {
    // Skip the smap pass entirely — just end any active pass and bail.
    VkCommandBuffer c = GetFrameCmd();
    if (m_bRenderingPassActive) { vkCmdEndRendering(c); m_bRenderingPassActive = false; }
    return;
}

    VkCommandBuffer cmd = GetFrameCmd();
    if (m_bRenderingPassActive) { vkCmdEndRendering(cmd); m_bRenderingPassActive = false; }

    // Force LOAD_OP_CLEAR: depth=1.0, color=1.0 (R32F "far")
    rt_smap_surf->pRT->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rt_smap_dsv.currentLayout        = VK_IMAGE_LAYOUT_UNDEFINED;

    const u32 smapW = rt_smap_surf->pRT->width;   // 2048
    const u32 smapH = rt_smap_surf->pRT->height;  // 2048
    u_setrt(smapW, smapH,
        rt_smap_surf->pRT,
        nullptr, nullptr,
        &rt_smap_dsv);

    RCache.set_RT(rt_smap_surf->pRT, 0);
    RCache.set_ZB(&rt_smap_dsv);

    // Shadow pass state: front-face cull off / depth write ON / no stencil
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();
    RCache.set_Z(TRUE);                  // re-enable depth TEST (accumulator's phase left it FALSE)
    RCache.set_ZWrite(TRUE);             // re-enable depth WRITE so the shadow map is populated
    RCache.set_ZFunc(D3DCMP_LESSEQUAL);  // ensure a sane compare (optional, matches shadow convention)

    // Viewport and Scissor are now handled centrally in vk_EnsureRenderPassActive
    // when the dynamic pass is actually opened on the first draw call.
}

void CRenderTarget::accum_direct_cascade(u32 sub_phase, const Fmatrix& xf, const Fmatrix& xfPrev, float bias)
{
    if (strstr(Core.Params, "-pb_no_accum")) {
        VkCommandBuffer c = GetFrameCmd();
        if (m_bRenderingPassActive) { vkCmdEndRendering(c); m_bRenderingPassActive = false; }
        return;
    }

    VkCommandBuffer cmd = GetFrameCmd();
    if (m_bRenderingPassActive) { vkCmdEndRendering(cmd); m_bRenderingPassActive = false; }

    string64 cascadeName;
    xr_sprintf(cascadeName, "Sun Accum Direct Cascade #%u", sub_phase);
    vk_ScopedPass passCascade(cmd, cascadeName, vk_colors::Lighting);

    // Sun shader samples s_smap = rt_smap_depth. Transition the DEPTH image to a
    // read-only layout after the smap write pass.
    if (rt_smap_depth && rt_smap_depth->pZRT)
    {
        VkImageAspectFlags smapAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        vk_TransitionImageLayout(cmd, rt_smap_depth->pZRT->image,
            smapAspect,
            rt_smap_dsv.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        rt_smap_depth->pZRT->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rt_smap_dsv.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Drain THIS cascade's smap_surf color writes so the NEXT cascade's
    // phase_smap_direct layout transition (which forces oldLayout=UNDEFINED and
    // thus severs the availability chain) is correctly ordered after them.
    if (rt_smap_surf && rt_smap_surf->pRT && rt_smap_surf->pRT->image)
    {
        vk_TransitionImageLayout(cmd, rt_smap_surf->pRT->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        // Layout unchanged; next phase_smap_direct will force it to UNDEFINED for LOAD_OP_CLEAR.
    }

    phase_accumulator();

    light* fuckingsun = (light*)RImplementation.Lights.sun_adapted._get();
    if (!fuckingsun) return; // Null guard against early level spawns

    u32 Offset;
    u32 C = color_rgba(255, 255, 255, 255);
    Fvector L_dir, L_clr;
    L_clr.set(fuckingsun->color.r, fuckingsun->color.g, fuckingsun->color.b);
    Device.mView.transform_dir(L_dir, fuckingsun->direction);
    L_dir.normalize();

    // 1. Masking Phase (SE_SUN_NEAR only)
    if (SE_SUN_NEAR == sub_phase)
    {
        RCache.set_CullMode(CULL_NONE);
        
        u32 Offset;
        float _w = float(Device.dwWidth);
        float _h = float(Device.dwHeight);
        Fvector2 p0, p1;
        p0.set(.5f / _w, .5f / _h);
        p1.set((_w + .5f) / _w, (_h + .5f) / _h);
        float d_Z = EPS_S, d_W = 1.f;
        u32 C = color_rgba(255, 255, 255, 255);

        FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y); pv++;
        pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y); pv++;
        pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y); pv++;
        pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);
        RCache.set_Geometry(g_combine);

        float intensity = 0.3f * fuckingsun->color.r + 0.48f * fuckingsun->color.g + 0.22f * fuckingsun->color.b;
        Fvector dir = L_dir;
        dir.normalize().mul(-_sqrt(intensity + EPS));
        RCache.set_Element(s_accum_mask->E[SE_MASK_DIRECT]);
        RCache.set_c("Ldynamic_dir", dir.x, dir.y, dir.z, 0);

        RCache.set_ColorWriteEnable(FALSE); // MASK PASS MUST NOT WRITE COLOR!
        RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0x01, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
        RCache.set_ColorWriteEnable(); // RESTORE FOR LIGHTING PASS
    }

    // 2. Main Scene Lighting Phase
    // phase_accumulator() removed: Do not close and reopen the Vulkan rendering pass here!
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable();

    // recalculate d_Z to perform depth-clipping
    Fvector center_pt;
    center_pt.mad(Device.vCameraPosition, Device.vCameraDirection, ps_r2_sun_near);
    Device.mFullTransform.transform(center_pt);
    float d_Z = center_pt.z;

    const float fRange = (SE_SUN_NEAR == sub_phase) ? ps_r2_sun_depth_near_scale : ps_r2_sun_depth_far_scale;
    const float fBias = (SE_SUN_NEAR == sub_phase)
                        ? (-ps_r2_sun_depth_near_bias)
                        : ps_r2_sun_depth_far_bias;
    Fmatrix m_TexelAdjust = {
        0.5f,  0.0f,  0.0f,   0.0f,
        0.0f, -0.5f,  0.0f,   0.0f,
        0.0f,  0.0f,  fRange, 0.0f,
        0.5f,  0.5f,  fBias,  1.0f
    };
    Fmatrix xf_project;
    xf_project.mul(m_TexelAdjust, fuckingsun->X.D.combine);
    Fmatrix m_shadow_adj;
    m_shadow_adj.mul(xf_project, Device.mInvView);
    
    // clouds xform
    Fmatrix m_clouds_shadow;
    {
        static float w_shift = 0;
        Fmatrix m_xform;
        Fvector direction = fuckingsun->direction;
        float w_dir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
        float w_speed = g_pGamePersistent->Environment().CurrentEnv->wind_velocity * 0.001f;
        clamp(w_speed, 0.1f, 1.0f);
        Fvector normal;
        normal.setHP(-w_dir, 0);
        w_shift -= 0.005f * w_speed * Device.fTimeDelta;
        Fvector position;
        position.set(0, 0, 0);
        m_xform.build_camera_dir(position, direction, normal);
        Fvector localnormal;
        m_xform.transform_dir(localnormal, normal);
        localnormal.normalize();
        m_clouds_shadow.mul(m_xform, Device.mInvView);
        m_xform.scale(0.002f, 0.002f, 1.f);
        m_clouds_shadow.mulA_44(m_xform);
        m_xform.translate(localnormal.mul(w_shift));
        m_clouds_shadow.mulA_44(m_xform);
    }
    
    float VertTan = -1.0f * tanf(deg2rad(Device.fFOV / 2.0f));
    float HorzTan = -VertTan / Device.fASPECT;

    RCache.set_Element(s_accum_direct->E[sub_phase]);
    
    RCache.set_c("m_shadow", m_shadow_adj);
    RCache.set_c("m_sunmask", m_clouds_shadow);
    RCache.set_c("Ldynamic_dir", L_dir.x, L_dir.y, L_dir.z, 0);
    RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, u_diffuse2s(L_clr));
    RCache.set_c("pos_decompression_params", HorzTan, VertTan, (2.0f * HorzTan) / (float)Device.dwWidth, (2.0f * VertTan) / (float)Device.dwHeight);
    RCache.set_c("pos_decompression_params2", (float)Device.dwWidth, (float)Device.dwHeight, 1.0f / (float)Device.dwWidth, 1.0f / (float)Device.dwHeight);
    RCache.set_Geometry(g_combine);

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f,  1.f, d_Z, 1.f, 0, 0.f, 1.f); pv++;
    pv->set(-1.f, -1.f, d_Z, 0.f, 0, 0.f, 0.f); pv++;
    pv->set( 1.f,  1.f, d_Z, 1.f, 0, 1.f, 1.f); pv++;
    pv->set( 1.f, -1.f, d_Z, 0.f, 0, 1.f, 0.f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID, 0xff, 0x00);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // 3. Dedicated HUD Lighting Phase (SE_SUN_NEAR only)
    if (SE_SUN_NEAR == sub_phase)
    {
        float VertTanHud = -1.0f * tanf(deg2rad((psHUD_FOV * 83.f) / 2.0f));
        float HorzTanHud = -VertTanHud / Device.fASPECT;

        RCache.set_Element(s_accum_direct->E[SE_SUN_NEAR_HUD]);
        
        RCache.set_c("m_shadow", m_shadow_adj);
        RCache.set_c("Ldynamic_dir", L_dir.x, L_dir.y, L_dir.z, 0);
        RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, u_diffuse2s(L_clr));
        RCache.set_c("pos_decompression_params_hud", HorzTanHud, VertTanHud, (2.0f * HorzTanHud) / (float)Device.dwWidth, (2.0f * VertTanHud) / (float)Device.dwHeight);
        RCache.set_c("pos_decompression_params2", (float)Device.dwWidth, (float)Device.dwHeight, 1.0f / (float)Device.dwWidth, 1.0f / (float)Device.dwHeight);
        RCache.set_Geometry(g_combine);

        pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-1.f,  1.f, d_Z, 1.f, 0, 0.f, 1.f); pv++;
        pv->set(-1.f, -1.f, d_Z, 0.f, 0, 0.f, 0.f); pv++;
        pv->set( 1.f,  1.f, d_Z, 1.f, 0, 1.f, 1.f); pv++;
        pv->set( 1.f, -1.f, d_Z, 0.f, 0, 1.f, 0.f); pv++;
        RCache.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID | 0x80, 0xff, 0x00);
        RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }
}

void CRenderTarget::accum_direct_blend()
{
    // blend-copy
    RCache.set_Element(s_accum_direct->E[SE_SUN_LUMINANCE]);
    RCache.set_Geometry(g_combine);

    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0x00);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable();

    u32 Offset;
    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f,  1.f, 0.f, 1.f, 0, 0.f, 1.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0, 0.f, 0.f); pv++;
    pv->set( 1.f,  1.f, 1.f, 1.f, 0, 1.f, 1.f); pv++;
    pv->set( 1.f, -1.f, 1.f, 0.f, 0, 1.f, 0.f); pv++;
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

void CRenderTarget::accum_direct_finalize()
{
    VkCommandBuffer cmd = GetFrameCmd();
    if (m_bRenderingPassActive) { vkCmdEndRendering(cmd); m_bRenderingPassActive = false; }

    if (rt_Accumulator && rt_Accumulator->pRT)
    {
        vk_TransitionImageLayout(
            cmd, rt_Accumulator->pRT->image, VK_IMAGE_ASPECT_COLOR_BIT,
            rt_Accumulator->pRT->currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        rt_Accumulator->pRT->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    RCache.set_RT(nullptr, 0);
    RCache.set_ZB(nullptr);
}

thread_local bool g_vkRecordingSecondaryGBuffer = false;

// ─── vk_EnsureRenderPassActive ─────────────────────────────────────────────────
// Ensures an active Dynamic Rendering pass before a draw call. X-Ray's UI
// bypasses CRenderTarget::u_setrt, so we must automatically catch it here.
void vk_EnsureRenderPassActive(VkCommandBuffer cmd)
{
    if (g_vkRecordingSecondaryGBuffer) return;

    CRender* render = (CRender*)::Render;
    if (!render || !render->Target)
        return;

    if (HW.HasPendingTransfers() || RCache.CheckAndResetRenderPassDirty())
    {
        if (render->Target->m_bRenderingPassActive)
        {
            vkCmdEndRendering(cmd);
            render->Target->m_bRenderingPassActive = false;
        }
        if (HW.HasPendingTransfers())
            HW.FlushDeferredTransfers();
    }

    if (render->Target->m_bRenderingPassActive)
        return;

    VkRenderingAttachmentInfo colorAttachments[4]{};
    uint32_t rtCount = 0;
    VkExtent2D area = HW.m_vkSCExtent;

    // Check all 4 possible RT slots in RCache
    for (int i = 0; i < 4; ++i)
    {
        VkRTVWrapper* rtv = static_cast<VkRTVWrapper*>(RCache.get_RT(i));
        if (!rtv) continue;

        if (i == 0 && rtv->width > 0 && rtv->height > 0)
        {
            area.width = rtv->width;
            area.height = rtv->height;
        }

        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = rtv->view;

        if (rtv->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            colorAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            if (render->phase == CRender::PHASE_SMAP)
                colorAttachments[i].clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
            else
                colorAttachments[i].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

            vk_TransitionImageLayout(cmd, rtv->image, VK_IMAGE_ASPECT_COLOR_BIT,
                rtv->currentLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            rtv->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            colorAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        colorAttachments[i].imageLayout = rtv->currentLayout;
        colorAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        rtCount = i + 1;
    }

    if (rtCount == 0)
    {
        // 1.3 Dynamic Rendering Swapchain Target (UI / Post-process)
        colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[0].imageView = HW.m_vkSCImageViews[HW.m_vkCurrentImageIndex];
        colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // ─── THE ANTI-ERASURE RENDERING PATCH ───
        if (Device.dwFrame == render->Target->m_lastSwapchainClearFrame)
        {
            // ─── APPEND PASS ───
            colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        else
        {
            // ─── INITIAL CLEAR PASS ───
            colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachments[0].clearValue.color = {{0.25f, 0.0f, 0.25f, 1.0f}}; // Dark Magenta Clear Color (bright magenta hurts my eyes.)
            render->Target->m_lastSwapchainClearFrame = Device.dwFrame;
        }
        colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        rtCount = 1;
    }

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false;

    if (RCache.get_ZB())
    {
        VkDSVWrapper* dsv = static_cast<VkDSVWrapper*>(RCache.get_ZB());
        if (!RCache.get_RT(0) && dsv->width > 0 && dsv->height > 0)
        {
            area.width = dsv->width;
            area.height = dsv->height;
        }

        depthAttachment.imageView = dsv->view;

        if (dsv->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.clearValue.depthStencil = {1.0f, 0};

            const bool fmtHasStencil =
                (dsv->format == VK_FORMAT_D24_UNORM_S8_UINT  ||
                 dsv->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                 dsv->format == VK_FORMAT_D16_UNORM_S8_UINT);
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (fmtHasStencil) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

            vk_TransitionImageLayout(cmd, dsv->image, aspectMask,
                dsv->currentLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            dsv->currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        else
        {
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        depthAttachment.imageLayout = dsv->currentLayout;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        hasDepth = true;
    }

    VkRenderingInfo renderInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderInfo.renderArea.extent = area;
    renderInfo.layerCount = 1;

    if (RCache.get_RT(0) || !hasDepth) // Bind color attachments if RTV exists or it's the swapchain UI pass
    {
        renderInfo.colorAttachmentCount = rtCount;
        renderInfo.pColorAttachments = colorAttachments;
    }
    else
    {
        renderInfo.colorAttachmentCount = 0;
        renderInfo.pColorAttachments = nullptr;
    }

    if (hasDepth)
    {
        VkDSVWrapper* dsv = static_cast<VkDSVWrapper*>(RCache.get_ZB());
        const bool fmtHasStencil =
            (dsv->format == VK_FORMAT_D24_UNORM_S8_UINT  ||
             dsv->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
             dsv->format == VK_FORMAT_D16_UNORM_S8_UINT);
        renderInfo.pDepthAttachment   = &depthAttachment;
        renderInfo.pStencilAttachment = fmtHasStencil ? &depthAttachment : nullptr;
    }

    vkCmdBeginRendering(cmd, &renderInfo);
    render->Target->m_bRenderingPassActive = true;
    RCache.m_ctx->m_pipelineDirty = true;   // NEW: force full re-eval on the first draw of every new pass instance

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = (float)area.height;
    viewport.width    = (float)area.width;
    viewport.height   = -(float)area.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = area;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (!RCache.get_RT(0))
    {
        // ─── THE ULTIMATE VISIBILITY PATCH ───
        // Force the dynamic state manager to completely disable face culling for the window pass.
        // This guarantees that regardless of vertex winding flips from our negative height,
        // the hardware rasterizer will paint both sides of every single UI quad!
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    }
}
