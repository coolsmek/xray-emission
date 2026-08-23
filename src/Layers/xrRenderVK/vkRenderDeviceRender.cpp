// vkRenderDeviceRender.cpp — Vulkan implementation of IRenderDeviceRender.
// Mirrors the responsibilities of dxRenderDeviceRender.cpp for the VK path.

#include "stdafx.h"
#include "vkRenderDeviceRender.h"
#include "../xrRender/ResourceManager.h"
#include "../../Include/xrRender/UIRender.h"
#include "RenderTarget/vk_LayoutTransitions.h"
#include "Managers/vk_DescriptorManager.h"
#include "rVK.h"

vkRenderDeviceRender vkDeviceRenderImpl;

extern XRAPI_API xr_token* vid_mode_token;

// Added fill_vid_mode_list to populate the global vid_mode_token for the Vulkan renderer.
// Without this, the UI scripts (like the options menu) will fail to retrieve valid display resolutions,
// causing a silent Lua error and preventing the settings menu from opening.
void fill_vid_mode_list(CHW* _hw)
{
	if (vid_mode_token != NULL) return;

	xr_vector<xr_string> _tmp;
	DEVMODE dm;
	ZeroMemory(&dm, sizeof(dm));
	dm.dmSize = sizeof(dm);

	for (int iModeNum = 0; EnumDisplaySettings(NULL, iModeNum, &dm) != 0; iModeNum++)
	{
		if (dm.dmPelsWidth < 800) continue;
		string32 str;
		xr_sprintf(str, sizeof(str), "%dx%d", dm.dmPelsWidth, dm.dmPelsHeight);

		bool found = false;
		for (auto& v : _tmp) {
			if (v == str) { found = true; break; }
		}
		if (!found) _tmp.push_back(str);
	}

	u32 _cnt = _tmp.size() + 1;
	vid_mode_token = xr_alloc<xr_token>(_cnt);
	vid_mode_token[_cnt - 1].id = -1;
	vid_mode_token[_cnt - 1].name = NULL;

	for (u32 i = 0; i < _tmp.size(); ++i)
	{
		vid_mode_token[i].id = i;
		vid_mode_token[i].name = xr_strdup(_tmp[i].c_str());
	}
}

void free_vid_mode_list()
{
	if (!vid_mode_token) return;
	for (int i = 0; vid_mode_token[i].name; i++)
	{
		xr_free(vid_mode_token[i].name);
	}
	xr_free(vid_mode_token);
	vid_mode_token = NULL;
}

// ── Device lifetime ───────────────────────────────────────────────────────────

void vkRenderDeviceRender::Create(HWND hWnd, u32& dwWidth, u32& dwHeight,
                                   float& fWidth_2, float& fHeight_2,
                                   bool move_window)
{
    // Create the Vulkan device + swapchain tied to this HWND.
    // HW.CreateDevice initialises the VkInstance/VkDevice (if not already done),
    // creates the VkSurfaceKHR from hWnd, and builds the swapchain.
    HW.CreateD3D();
    HW.CreateDevice(hWnd, move_window);

    // Read back swapchain dimensions — used to initialise Device.dwWidth/Height.
    dwWidth   = HW.m_vkSCExtent.width;
    dwHeight  = HW.m_vkSCExtent.height;
    fWidth_2  = float(dwWidth  / 2);
    fHeight_2 = float(dwHeight / 2);

    free_vid_mode_list();
    fill_vid_mode_list(&HW);

    // Allocate the shared resource manager (shaders, textures, RTs, constants).
    Resources = xr_new<CResourceManager>();
}

void vkRenderDeviceRender::OnDeviceCreate(LPCSTR shName)
{
    Msg("* [ODC] step 1: RCache.OnDeviceCreate");
    RCache.OnDeviceCreate();

    Msg("* [ODC] step 2: Resources->OnDeviceCreate");
    Resources->OnDeviceCreate(shName);

    Msg("* [ODC] step 3: ::Render->create()");
    ::Render->create();
    Msg("* [ODC] step 3 DONE");

    Msg("* [ODC] step 4: Device.Statistic");
    if (Device.Statistic)
        Device.Statistic->OnDeviceCreate();

    Msg("* [ODC] step 5: DUImpl.OnDeviceCreate");
    if (!g_dedicated_server)
        DUImpl.OnDeviceCreate();

    Msg("* [ODC] done");
}

// ── Tool-mode device creation (XrayModelViewer) ───────────────────────────────

void vkRenderDeviceRender::CreateForTool(HWND hWnd, u32& dwWidth, u32& dwHeight,
                                          float& fWidth_2, float& fHeight_2)
{
    // Boot xrRenderVK without a swapchain.  Spherical will create the surface +
    // swapchain itself via externalVulkanContext after this call.
    HW.CreateD3D();
    HW.CreateDevice_NoSwapchain(hWnd, /*move_window=*/true, 1280, 720);

    // Placeholder dimensions — overwritten from Spherical::GetSwapchainExtent()
    // in main.cpp before OnDeviceCreate() is called.
    dwWidth   = HW.m_vkSCExtent.width;
    dwHeight  = HW.m_vkSCExtent.height;
    fWidth_2  = float(dwWidth)  / 2.0f;
    fHeight_2 = float(dwHeight) / 2.0f;

    Resources = xr_new<CResourceManager>();
}

void vkRenderDeviceRender::OnDeviceDestroy(BOOL bKeepTextures)
{
    Resources->OnDeviceDestroy(bKeepTextures);
    RCache.OnDeviceDestroy();
}

void vkRenderDeviceRender::DestroyHW()
{
    xr_delete(Resources);
}

void vkRenderDeviceRender::Reset(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2)
{
    Resources->reset_begin();
    Memory.mem_compact();

    HW.Reset(hWnd);

    dwWidth = HW.m_vkSCExtent.width;
    dwHeight = HW.m_vkSCExtent.height;
    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);

    Resources->reset_end();
}

void vkRenderDeviceRender::SetupGPU(BOOL /*bForceGPU_SW*/,
                                     BOOL /*bForceGPU_NonPure*/,
                                     BOOL /*bForceGPU_REF*/)
{
    // GPU selection is handled during Vulkan device enumeration in HW.CreateDevice.
    // Nothing to do here.
}

// ── Resource management ───────────────────────────────────────────────────────

void vkRenderDeviceRender::DeferredLoad(BOOL E)
{
    Resources->DeferredLoad(E);
}

void vkRenderDeviceRender::ResourcesDeferredUpload()
{
    HW.FlushDeferredTransfers();
    Resources->DeferredUpload();
}

void vkRenderDeviceRender::ResourcesDeferredUnload()
{
    Resources->DeferredUnload();
}

void vkRenderDeviceRender::ResourcesGetMemoryUsage(u32& m_base, u32& c_base,
                                                    u32& m_lmaps, u32& c_lmaps)
{
    if (Resources)
        Resources->_GetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
}

void vkRenderDeviceRender::ResourcesDestroyNecessaryTextures()
{
    Resources->DestroyNecessaryTextures();
}

void vkRenderDeviceRender::ResourcesStoreNecessaryTextures()
{
    Resources->StoreNecessaryTextures();
}

void vkRenderDeviceRender::ResourcesDumpMemoryUsage()
{
    if (Resources)
        Resources->_DumpMemoryUsage();
}

void vkRenderDeviceRender::ResourcesPrefetchCreateTexture(LPCSTR name)
{
    Resources->_CreateTexture(name);
}

// ── Transform cache ───────────────────────────────────────────────────────────

void vkRenderDeviceRender::SetCacheXform(Fmatrix& mView, Fmatrix& mProject)
{
    RCache.set_xform_view(mView);
    RCache.set_xform_project(mProject);
}

void vkRenderDeviceRender::SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject)
{
    RCache.set_xform_view_prev(mView);
    RCache.set_xform_project_prev(mProject);
}

void vkRenderDeviceRender::OnAssetsChanged()
{
    if (Resources)
    {
        Resources->m_textures_description.UnLoad();
        Resources->m_textures_description.Load();
    }
}

// ── Per-frame hooks ───────────────────────────────────────────────────────────

void vkRenderDeviceRender::Begin()
{
    // In tool mode the command buffer is owned by Spherical's frame loop.
    // xrRenderVK records into it via the preRenderCallback — no acquire/submit here.
    if (HW.m_bToolMode) return;

    HW.m_bRenderingFrame = false;

    // Clear the active command buffer from the previous frame.
    // If vkAcquireNextImageKHR fails, this prevents rendering code from using a stale command buffer.
    RCache.OnFrameBegin(VK_NULL_HANDLE, 0);

    const uint32_t frameSlot = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;
    const bool perf = !!strstr(Core.Params, "-vk_frame_perf");

    // 1. Wait for the previous use of this frame slot to finish
    CTimer t; if (perf) t.Start();
    vkWaitForFences(HW.m_vkDevice, 1, &HW.m_vkInFlightFences[frameSlot], VK_TRUE, UINT64_MAX);
    const float msFence = perf ? t.GetElapsed_sec()*1000.f : 0.f;

    // 1b. Flush any asynchronous resource uploads BEFORE acquiring the image and starting the frame
    if (perf) t.Start();
    HW.FlushDeferredTransfers();
    const float msXfer = perf ? t.GetElapsed_sec()*1000.f : 0.f;

    // 2. Acquire the next swapchain image
    if (perf) t.Start();
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(
        HW.m_vkDevice, HW.m_vkSwapchain, UINT64_MAX,
        HW.m_vkImageAvailable[frameSlot], VK_NULL_HANDLE, &imageIndex);
    const float msAcquire = perf ? t.GetElapsed_sec()*1000.f : 0.f;

    if (perf)
        Msg("  VK-FRAME  f%u fence=%.3f xfer=%.3f acquire=%.3f",
            Device.dwFrame, msFence, msXfer, msAcquire);

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        HW.vk_RecreateSwapchain();
        return; // Will try again next frame
    }
    // If VK_SUBOPTIMAL_KHR, we still acquired the image successfully and MUST submit a frame to unsignal the semaphore!
    // We will render this frame and it will be presented suboptimally, then next frame we can recreate if needed.
    R_ASSERT2(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR, "vkRenderDeviceRender::Begin — vkAcquireNextImageKHR failed");

    HW.m_vkCurrentImageIndex = imageIndex;

    vkResetFences(HW.m_vkDevice, 1, &HW.m_vkInFlightFences[frameSlot]);

    // 3. Advance Descriptor Ring Buffer
    DescriptorManager.BeginFrame(frameSlot);

    // 4. Begin command buffer
    VkCommandBuffer cmd = HW.m_vkCmdBuffers[frameSlot];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    res = vkBeginCommandBuffer(cmd, &beginInfo);
    R_ASSERT2(res == VK_SUCCESS, "vkRenderDeviceRender::Begin — vkBeginCommandBuffer failed");

    if (g_vkCmdBeginDebugUtilsLabelEXT)
    {
        VkDebugUtilsLabelEXT frameLabel{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        string64 name;
        xr_sprintf(name, "Frame #%u", Device.dwFrame);
        frameLabel.pLabelName = name;
        frameLabel.color[0] = vk_colors::Frame[0];
        frameLabel.color[1] = vk_colors::Frame[1];
        frameLabel.color[2] = vk_colors::Frame[2];
        frameLabel.color[3] = vk_colors::Frame[3];
        g_vkCmdBeginDebugUtilsLabelEXT(cmd, &frameLabel);
    }

    PipelineCache.ResetDynamicStateCache(RCache.m_ctx);

    CRenderTarget* RT = ((CRender*)Render)->Target;
    if (RT && RT->pZB.image)
    {
        vk_TransitionImageLayout(cmd, RT->pZB.image,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    }

    if (RT && RT->rt_smap_surf && RT->rt_smap_surf->pRT && RT->rt_smap_surf->pRT->image)
    {
        // Drain previous frame's Sun-Cascade color writes before this frame reuses smap_surf.
        // Same-image WAW across frames-in-flight; layout will be reset to UNDEFINED by
        // phase_smap_direct, so we only need the execution+memory dependency, not a layout change.
        vk_TransitionImageLayout(cmd, RT->rt_smap_surf->pRT->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,   // where last frame's cascade left it
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,   // no real change; clear will discard via UNDEFINED
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    if (RT && RT->rt_Generic_1 && RT->rt_Generic_1->pRT && RT->rt_Generic_1->pRT->image)
    {
        // Drain previous frame's combine pass color writes before this frame reuses rt_Generic_1.
        // Same-image WAW across frames-in-flight (CmdBuffer #0 <-> #1).
        vk_TransitionImageLayout(cmd, RT->rt_Generic_1->pRT->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    // Tell the backend which command buffer is active this frame.
    RCache.OnFrameBegin(cmd, imageIndex);

    HW.m_bRenderingFrame = true;

    // 5. Set viewport & scissor (dynamic state)
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = (float)HW.m_vkSCExtent.height;
    viewport.width    = (float)HW.m_vkSCExtent.width;
    viewport.height   = -(float)HW.m_vkSCExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = HW.m_vkSCExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 6. Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL so UI can draw to it
    // Note: If the 3D scene is drawn (via CRender::Render), it will overwrite the swapchain image in phase_combine.
    // We transition from UNDEFINED because we don't care about previous frame's content.
    vk_TransitionImageLayout(
        cmd, HW.m_vkSCImages[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void vkRenderDeviceRender::Clear()
{
    // The engine calls this if rsClearBB is set.
    // We clear the swapchain image to a bright blue using Dynamic Rendering to ensure
    // we have a valid presentation background and can distinguish it from a black screen.
    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = HW.m_vkSCImageViews[HW.m_vkCurrentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.0f, 0.0f, 1.0f, 1.0f}}; // Solid Blue

    VkRenderingInfo renderInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderInfo.renderArea.extent = HW.m_vkSCExtent;
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    vkCmdEndRendering(cmd);
}

void vkRenderDeviceRender::End()
{
    // In tool mode Spherical owns submit + present — nothing to do here.
    if (HW.m_bToolMode) return;

    HW.m_bRenderingFrame = false;

    VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
    if (!cmd) return;

    // End any active rendering pass (e.g. if UI was drawing)
    CRender* render = (CRender*)::Render;
    if (render->Target && render->Target->m_bRenderingPassActive)
    {
        vkCmdEndRendering(cmd);
        render->Target->m_bRenderingPassActive = false;
    }

    RCache.OnFrameEnd();

    const uint32_t frameSlot = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;
    const uint32_t imageIndex = HW.m_vkCurrentImageIndex;

    // 1. Transition Swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    // 1. Transition Swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    vk_TransitionImageLayout(
        cmd, HW.m_vkSCImages[imageIndex], VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE);

    // End top-level Frame label scope
    if (g_vkCmdEndDebugUtilsLabelEXT)
    {
        g_vkCmdEndDebugUtilsLabelEXT(cmd);
    }

    // 2. End command buffer
    VkResult res = vkEndCommandBuffer(cmd);
    R_ASSERT2(res == VK_SUCCESS, "vkRenderDeviceRender::End — vkEndCommandBuffer failed");

    // 3. Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &HW.m_vkImageAvailable[frameSlot];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &HW.m_vkRenderFinished[imageIndex];

    const bool perf = !!strstr(Core.Params, "-vk_frame_perf");
    CTimer t; if (perf) t.Start();
    res = HW.SubmitQueue(1, &si, HW.m_vkInFlightFences[frameSlot]);
    const float msSubmit = perf ? t.GetElapsed_sec()*1000.f : 0.f;
    if (res != VK_SUCCESS)
    {
        xrLogger::SetImmediateMode(true);
        Msg("! ERROR: HW.SubmitQueue failed with VkResult = %d", res);
        xrLogger::FlushLog();
    }
    R_ASSERT2(res == VK_SUCCESS, "vkRenderDeviceRender::End - HW.SubmitQueue failed");

    // 4. Present
    if (perf) t.Start();
    res = HW.vk_Present(imageIndex);
    const float msPresent = perf ? t.GetElapsed_sec()*1000.f : 0.f;
    if (perf)
        Msg("  VK-FRAME-END  f%u submit=%.3f present=%.3f",
            Device.dwFrame, msSubmit, msPresent);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        HW.vk_RecreateSwapchain();
}
