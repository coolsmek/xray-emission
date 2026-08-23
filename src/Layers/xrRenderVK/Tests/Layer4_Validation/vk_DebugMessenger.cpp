#include "stdafx.h"
#include "vk_DebugMessenger.h"

// Severity → X-Ray log prefix mapping
static const char* SeverityPrefix(VkDebugUtilsMessageSeverityFlagBitsEXT sev)
{
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   return "! VK_ERROR";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return "~ VK_WARN ";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)    return "* VK_INFO ";
    return "* VK_VERBOSE";
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*                                       pUserData)
{
    // Always route to engine log synchronously
    xrLogger::SetImmediateMode(true);
    Msg("%s [%s]: %s",
        SeverityPrefix(severity),
        pData->pMessageIdName ? pData->pMessageIdName : "?",
        pData->pMessage);

    // On ERROR severity in VerifiedVK builds: trigger an explicit engine assert.
    // This makes validation errors fatal — the same treatment as any other
    // hard assertion failure in the codebase.
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
#ifdef VK_VALIDATION_FATAL
        // VK_VALIDATION_FATAL is defined only in VerifiedVK and VerifiedVKASAN.
        // This fires R_ASSERT2 which — unlike ASSERT — writes a callstack, flushes
        // the log, and terminates cleanly instead of silently continuing.
        R_ASSERT2(false, pData->pMessage);
#endif
    }

    return VK_FALSE; // Do not abort the Vulkan call itself
}

VkDebugUtilsMessengerEXT vk_CreateDebugMessenger(VkInstance instance)
{
    auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!createFn)
    {
        Msg("! vk_DebugMessenger: vkCreateDebugUtilsMessengerEXT not available");
        return VK_NULL_HANDLE;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    // Capture ERRORS and WARNINGS always.
    // Capture VERBOSE only in full-debug builds to avoid log spam.
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT   |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
#ifdef VK_VALIDATION_VERBOSE
    ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
#endif

    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    ci.pfnUserCallback = vk_DebugCallback;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkResult res = createFn(instance, &ci, nullptr, &messenger);
    if (res != VK_SUCCESS)
        Msg("! vk_DebugMessenger: creation failed (%d)", (int)res);
    else
        Msg("* vk_DebugMessenger: VK_LAYER_KHRONOS_validation active");
    return messenger;
}

void vk_DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger == VK_NULL_HANDLE) return;
    auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (destroyFn) destroyFn(instance, messenger, nullptr);
}
