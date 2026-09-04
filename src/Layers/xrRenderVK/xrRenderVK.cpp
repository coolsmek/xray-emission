// Entry point for the Vulkan rendering backend (xrRenderVK).
// Handles DLL lifecycle events, wires up the global render/factory/UI interfaces,
// and exposes SupportsVKRendering() to query hardware Vulkan support.

// minimize touching legacy shared files:
/*#include "../xrRender/dxRenderFactory.h"
#include "../xrRender/dxUIRender.h"
#include "../xrRender/dxDebugRender.h"*/

#include "stdafx.h"
#include "rVK.h"
#include "Managers/vk_PipelineCache.h"
#include "Backend/vkR_Backend_Runtime.h"

VkRecordContext g_vkPrimaryContext;
VkRecordContext g_vkWorkerContexts[CHW::VK_GBUFFER_WORKERS];

// Vulkan-specific global instances (defined in vkRenderFactory.cpp)
#include "vkRenderFactory.h"
#include "vk_UIRender.h"
#include "vkDebugRender.h"
#include "../xrRender/D3DUtils.h"
#include "Tools/vk_ToolSceneRenderer.h"
#include "RenderTarget/vk_LayoutTransitions.h"

// Declare the concrete global instances that X-Ray's engine uses.
// Defined as externs throughout the rest of the engine code.

BOOL DllMainXrRenderVK(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        ::Render        = &RImplementation;
        ::RenderFactory = &RenderFactoryImpl;
        ::DU            = &DUImpl;
        UIRender        = &UIRenderImpl;
        DRender         = &DebugRenderImpl;

        xrRender_initconsole();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C" {
    __declspec(dllexport) bool SupportsVKRendering();
};

bool SupportsVKRendering()
{
    return xrRender_test_hw() ? true : false;
}

// ─── Tool-mode scene render entry point ──────────────────────────────────────
// Called by Spherical's preRenderCallback each frame (inside Spherical's command
// buffer, before UI rendering).  Clears rt_Color into a solid background colour
// and leaves it in SHADER_READ_ONLY_OPTIMAL so Spherical can sample it.
//
// Deliberately avoids touching RCache / phase functions so that no dynamic state
// (stencil, cull mode, front face, …) bleeds into Spherical's UI command buffer.
void XrRenderVK_RenderSceneTool(VkCommandBuffer cmd,
                                 uint32_t        /*imageIndex*/,
                                 VkExtent2D      extent,
                                 void*           /*userData*/)
{
    if (::Render == nullptr) return;

    CRender*       pRender = static_cast<CRender*>(::Render);
    CRenderTarget* pTarget = pRender->Target;
    if (pTarget == nullptr)                      return;
    if (!pTarget->rt_Color || !pTarget->rt_Color->pRT) return;

    // Track swapchain resize so Device dimensions stay consistent.
    if (Device.dwWidth != extent.width || Device.dwHeight != extent.height)
    {
        Device.dwWidth   = extent.width;
        Device.dwHeight  = extent.height;
        Device.fWidth_2  = float(extent.width)  / 2.0f;
        Device.fHeight_2 = float(extent.height) / 2.0f;
        HW.m_vkSCExtent  = extent;
        // TODO: resize offscreen render targets to match the new extent
    }

    auto* pRTV = pTarget->rt_Color->pRT;

    // ── 1. Transition rt_Color to COLOR_ATTACHMENT_OPTIMAL ───────────────────
    VkPipelineStageFlags2 srcStage  =
        (pRTV->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_2_NONE
        : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    VkAccessFlags2 srcAccess =
        (pRTV->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_ACCESS_2_NONE
        : VK_ACCESS_2_SHADER_READ_BIT;

    vk_TransitionImageLayout(
        cmd, pRTV->image, VK_IMAGE_ASPECT_COLOR_BIT,
        pRTV->currentLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        srcStage,             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        srcAccess,            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    pRTV->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // ── 2. Transition HW depth buffer to DEPTH_STENCIL_ATTACHMENT_OPTIMAL ────
    // Track layout in the pZB wrapper (currentLayout starts at UNDEFINED).
    VkPipelineStageFlags2 depthSrcStage =
        (pTarget->pZB.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_2_NONE
        : VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    VkAccessFlags2 depthSrcAccess =
        (pTarget->pZB.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_ACCESS_2_NONE
        : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vk_TransitionImageLayout(
        cmd, pTarget->pZB.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        pTarget->pZB.currentLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        depthSrcStage, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        depthSrcAccess, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    pTarget->pZB.currentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ── 3. Begin rendering: clear colour + depth ──────────────────────────────
    VkRenderingAttachmentInfo ca{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    ca.imageView        = pRTV->view;
    ca.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color = {{ 0.08f, 0.09f, 0.12f, 1.0f }};

    VkRenderingAttachmentInfo da{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    da.imageView                    = pTarget->pZB.view;
    da.imageLayout                  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp                       = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp                      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    da.clearValue.depthStencil      = { 1.0f, 0 };

    VkExtent2D renderExtent = { pTarget->get_width(), pTarget->get_height() };
    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.extent       = renderExtent;
    ri.layerCount              = 1;
    ri.colorAttachmentCount    = 1;
    ri.pColorAttachments       = &ca;
    ri.pDepthAttachment        = &da;
    ri.pStencilAttachment      = &da;

    vkCmdBeginRendering(cmd, &ri);

    // ── 4. Draw scene (if model is loaded and renderer is ready) ─────────────
    using ToolScene::g_ToolSceneRenderer;
    if (g_ToolSceneRenderer.IsReady() && g_ToolSceneRenderer.HasModel())
        g_ToolSceneRenderer.Render(cmd, renderExtent);

    vkCmdEndRendering(cmd);

    // ── 5. Transition rt_Color → SHADER_READ_ONLY_OPTIMAL ────────────────────
    vk_TransitionImageLayout(
        cmd, pRTV->image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,          VK_ACCESS_2_SHADER_READ_BIT);
    pRTV->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
