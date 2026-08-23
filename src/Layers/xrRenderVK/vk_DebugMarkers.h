#pragma once

#include <vulkan/vulkan.h>

// ─── Vulkan Debug Utils Function Pointers ──────────────────────────────────────
extern PFN_vkCmdBeginDebugUtilsLabelEXT  g_vkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT    g_vkCmdEndDebugUtilsLabelEXT;
extern PFN_vkCmdInsertDebugUtilsLabelEXT g_vkCmdInsertDebugUtilsLabelEXT;
extern PFN_vkSetDebugUtilsObjectNameEXT  g_vkSetDebugUtilsObjectNameEXT;

// ─── Semantic RGBA Color Palettes for RenderDoc Hierarchy ──────────────────────
namespace vk_colors
{
    constexpr float Frame[4]       = { 0.90f, 0.22f, 0.21f, 1.0f }; // Coral Red
    constexpr float Visibility[4]  = { 0.38f, 0.49f, 0.55f, 1.0f }; // Slate Gray
    constexpr float GBuffer[4]     = { 0.12f, 0.53f, 0.90f, 1.0f }; // Material Blue
    constexpr float Shadows[4]     = { 0.95f, 0.55f, 0.10f, 1.0f }; // Dark Amber
    constexpr float Lighting[4]    = { 0.98f, 0.78f, 0.10f, 1.0f }; // Warm Gold
    constexpr float Forward[4]     = { 0.00f, 0.70f, 0.60f, 1.0f }; // Teal
    constexpr float PostProcess[4] = { 0.61f, 0.15f, 0.69f, 1.0f }; // Purple
    constexpr float UI[4]          = { 0.30f, 0.69f, 0.31f, 1.0f }; // Emerald Green
}

// ─── RAII Scoped Pass Marker ──────────────────────────────────────────────────
// Automatically opens a debug label scope in RenderDoc and closes it on scope exit.
struct vk_ScopedPass
{
    VkCommandBuffer cmd    = VK_NULL_HANDLE;
    bool            active = false;

    inline vk_ScopedPass(VkCommandBuffer inCmd, const char* name, const float color[4] = vk_colors::GBuffer)
    {
        if (g_vkCmdBeginDebugUtilsLabelEXT && inCmd && name)
        {
            cmd = inCmd;
            active = true;
            VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
            label.pLabelName = name;
            label.color[0] = color[0];
            label.color[1] = color[1];
            label.color[2] = color[2];
            label.color[3] = color[3];
            g_vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
        }
    }

    inline ~vk_ScopedPass()
    {
        if (active && g_vkCmdEndDebugUtilsLabelEXT && cmd)
        {
            g_vkCmdEndDebugUtilsLabelEXT(cmd);
        }
    }
};

// ─── Single-Point Debug Marker ────────────────────────────────────────────────
inline void vk_InsertMarker(VkCommandBuffer cmd, const char* name, const float color[4] = vk_colors::GBuffer)
{
    if (g_vkCmdInsertDebugUtilsLabelEXT && cmd && name)
    {
        VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        label.pLabelName = name;
        label.color[0] = color[0];
        label.color[1] = color[1];
        label.color[2] = color[2];
        label.color[3] = color[3];
        g_vkCmdInsertDebugUtilsLabelEXT(cmd, &label);
    }
}

// ─── Vulkan Object Naming Helper ──────────────────────────────────────────────
// Labels images, views, buffers, samplers, pipelines, and command buffers for RenderDoc.
inline void vk_SetDebugName(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char* name)
{
    if (g_vkSetDebugUtilsObjectNameEXT && device && objectHandle && name && name[0])
    {
        VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        info.objectType   = objectType;
        info.objectHandle = objectHandle;
        info.pObjectName  = name;
        g_vkSetDebugUtilsObjectNameEXT(device, &info);
    }
}
