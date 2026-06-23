// Implements the core CRender class inheriting from the abstract engine interface IRender_interface
// rVK.cpp — Implements CRender for the Vulkan backend.
// Drives the per-frame acquire → record → submit → present pipeline.

#include "stdafx.h"
#include "rVK.h"

CRender RImplementation;

CRender::CRender()  {}
CRender::~CRender() {}

void CRender::Render()
{
    const uint32_t frameSlot = HW.m_vkCurrentFrame % CHW::MAX_FRAMES_IN_FLIGHT;

    // ── 1. Wait for the previous use of this frame slot to finish ─────────
    vkWaitForFences(HW.m_vkDevice, 1, &HW.m_vkInFlightFences[frameSlot],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(HW.m_vkDevice, 1, &HW.m_vkInFlightFences[frameSlot]);

    // ── 2. Acquire the next swapchain image ───────────────────────────────
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(
        HW.m_vkDevice,
        HW.m_vkSwapchain,
        UINT64_MAX,
        HW.m_vkImageAvailable[frameSlot],
        VK_NULL_HANDLE,
        &imageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Window was resized — rebuild swapchain and retry next frame
        HW.vk_RecreateSwapchain();
        return;
    }
    R_ASSERT2(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR,
              "CRender::Render — vkAcquireNextImageKHR failed");

    // ── 3. Record command buffer for imageIndex ───────────────────────────
    // TODO: record scene geometry, lighting, post-processing passes here.

    // ── 4. Submit ─────────────────────────────────────────────────────────
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &HW.m_vkImageAvailable[frameSlot];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &HW.m_vkCmdBuffers[imageIndex];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &HW.m_vkRenderFinished[frameSlot];

    res = vkQueueSubmit(HW.m_vkGraphicsQueue, 1, &si,
                        HW.m_vkInFlightFences[frameSlot]);
    R_ASSERT2(res == VK_SUCCESS, "CRender::Render — vkQueueSubmit failed");

    // ── 5. Present ────────────────────────────────────────────────────────
    res = HW.vk_Present(imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        HW.vk_RecreateSwapchain();
}
