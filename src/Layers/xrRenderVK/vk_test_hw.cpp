#include "stdafx.h"

// ─── Phase 1: runtime-load vulkan-1.dll ──────────────────────────────────────
// We load dynamically so the probe is safe on machines without Vulkan installed.

static HMODULE s_vkLib = nullptr;

static bool LoadVulkanLibrary()
{
    if (s_vkLib) return true; // already loaded
    s_vkLib = LoadLibraryA("vulkan-1.dll");
    if (!s_vkLib)
        Msg("! VK: vulkan-1.dll not found");
    return s_vkLib != nullptr;
}

static void UnloadVulkanLibrary()
{
    if (s_vkLib) { FreeLibrary(s_vkLib); s_vkLib = nullptr; }
}

// ─── Phase 2: instance + optional Win32 surface probe ────────────────────────

static bool ProbeVulkan14(HWND hWnd)
{
    if (!s_vkLib) return false;

    auto vkCreateInstance =
        (PFN_vkCreateInstance)GetProcAddress(s_vkLib, "vkCreateInstance");
    auto vkDestroyInstance =
        (PFN_vkDestroyInstance)GetProcAddress(s_vkLib, "vkDestroyInstance");
    auto vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)GetProcAddress(s_vkLib, "vkGetInstanceProcAddr");

    if (!vkCreateInstance || !vkDestroyInstance || !vkGetInstanceProcAddr)
    {
        Msg("! VK: failed to resolve core Vulkan entry points");
        return false;
    }

    // Request Vulkan 1.4 — SDK 1.4.341.1 is installed
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "xray-vk-probe";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion         = VK_MAKE_API_VERSION(0, 1, 4, 0);

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = extensions;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult res = vkCreateInstance(&ci, nullptr, &inst);

    if (res != VK_SUCCESS || inst == VK_NULL_HANDLE)
    {
        Msg("! VK: vkCreateInstance failed (VkResult=%d)", (int)res);
        return false;
    }

    Msg("* VK: instance created (API 1.4)");

    // ── Surface probe against the engine HWND ────────────────────────────────
    // hWnd is nullptr when called from xrRender_test_hw() at DllMain time —
    // the engine window doesn't exist yet. Surface creation is deferred to
    // CHW::CreateDevice() via xrRender_test_surface().
    bool surfaceOK = true;

    if (hWnd)
    {
        auto vkCreateWin32SurfaceKHR =
            (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(
                inst, "vkCreateWin32SurfaceKHR");

        if (vkCreateWin32SurfaceKHR)
        {
            VkWin32SurfaceCreateInfoKHR sci{};
            sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            sci.hwnd      = hWnd;
            sci.hinstance = GetModuleHandle(nullptr);

            VkSurfaceKHR surface = VK_NULL_HANDLE;
            VkResult sres = vkCreateWin32SurfaceKHR(inst, &sci, nullptr, &surface);
            surfaceOK = (sres == VK_SUCCESS);

            if (surfaceOK)
            {
                Msg("* VK: Win32 surface created successfully (HWND=0x%p)", hWnd);

                auto vkDestroySurfaceKHR =
                    (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(
                        inst, "vkDestroySurfaceKHR");
                if (vkDestroySurfaceKHR)
                    vkDestroySurfaceKHR(inst, surface, nullptr);
            }
            else
            {
                Msg("! VK: vkCreateWin32SurfaceKHR failed (VkResult=%d)", (int)sres);
            }
        }
        else
        {
            Msg("! VK: vkCreateWin32SurfaceKHR not available — check VK_KHR_win32_surface support");
            surfaceOK = false;
        }
    }
    else
    {
        Msg("* VK: surface probe skipped — no HWND at probe time (normal for startup check)");
    }

    vkDestroyInstance(inst, nullptr);
    return surfaceOK;
}

// ─── Public API ──────────────────────────────────────────────────────────────

// Called by EngineAPI / CreateRendererList() before the window exists.
// Only verifies that Vulkan 1.4 instance creation succeeds.
BOOL xrRender_test_hw()
{
    if (!LoadVulkanLibrary()) return FALSE;
    BOOL result = ProbeVulkan14(nullptr) ? TRUE : FALSE;
    UnloadVulkanLibrary();
    return result;
}

// Called by CHW::CreateDevice() once the engine HWND is available.
// Verifies that a Win32 surface can be created against the game window.
BOOL xrRender_test_surface(HWND hWnd)
{
    if (!LoadVulkanLibrary()) return FALSE;
    BOOL result = ProbeVulkan14(hWnd) ? TRUE : FALSE;
    UnloadVulkanLibrary();
    return result;
}

// Exported capability check — called by EngineAPI::CreateRendererList()
// mirroring SupportsDX11Rendering() / SupportsDX10Rendering() pattern.
bool SupportsVKRendering()
{
    return xrRender_test_hw() == TRUE;
}
