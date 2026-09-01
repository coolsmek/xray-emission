#include "stdafx.h"
#include "Managers/vk_MemoryManager.h"
#include "Managers/vk_PipelineCache.h"
#include "Managers/vk_DescriptorManager.h"
#include "Resources/vk_BufferUtils.h"
#include "Resources/vk_TextureUtils.h"
#ifdef VK_ENABLE_TESTS
#include "Tests/Layer4_Validation/vk_DebugMessenger.h"
#include "Tests/vk_TestRunner.h"
#endif

#ifndef CHK_VK
#define CHK_VK(expr) do { VkResult res = (expr); R_ASSERT3(res == VK_SUCCESS, "Vulkan error in vk_HW.cpp", #expr); } while(0)
#endif

// Forward declared in vk_test_hw.cpp
extern BOOL xrRender_test_surface(HWND hWnd);

bool g_bVulkanDebugLog = false;

// [Phase 11] Global function pointer for VK_KHR_push_descriptor
PFN_vkCmdPushDescriptorSetKHR g_vkCmdPushDescriptorSetKHR = nullptr;

// Vulkan 1.3 Extended Dynamic States
PFN_vkCmdBindVertexBuffers2     g_vkCmdBindVertexBuffers2   = nullptr;
PFN_vkCmdSetFrontFace           g_vkCmdSetFrontFace         = nullptr;
PFN_vkCmdSetCullMode            g_vkCmdSetCullMode          = nullptr;
PFN_vkCmdSetDepthTestEnable     g_vkCmdSetDepthTestEnable   = nullptr;
PFN_vkCmdSetStencilTestEnable   g_vkCmdSetStencilTestEnable = nullptr;
PFN_vkCmdSetStencilOp           g_vkCmdSetStencilOp         = nullptr;
PFN_vkCmdSetStencilCompareMask  g_vkCmdSetStencilCompareMask = nullptr;
PFN_vkCmdSetStencilWriteMask    g_vkCmdSetStencilWriteMask   = nullptr;
PFN_vkCmdSetStencilReference    g_vkCmdSetStencilReference   = nullptr;
PFN_vkCmdSetDepthWriteEnable    g_vkCmdSetDepthWriteEnable  = nullptr;
PFN_vkCmdSetDepthCompareOp      g_vkCmdSetDepthCompareOp    = nullptr;
PFN_vkCmdSetPrimitiveTopology   g_vkCmdSetPrimitiveTopology = nullptr;

// Vulkan Debug Utils
PFN_vkCmdBeginDebugUtilsLabelEXT  g_vkCmdBeginDebugUtilsLabelEXT  = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT    g_vkCmdEndDebugUtilsLabelEXT    = nullptr;
PFN_vkCmdInsertDebugUtilsLabelEXT g_vkCmdInsertDebugUtilsLabelEXT = nullptr;
PFN_vkSetDebugUtilsObjectNameEXT  g_vkSetDebugUtilsObjectNameEXT  = nullptr;

// ─── CHW constructor / destructor ────────────────────────────────────────────

CHW::CHW()
    : m_move_window(true)
    , maxRefreshRate(0)
{
}

CHW::~CHW()
{
    DestroyDevice();
}

// ─── CreateD3D — create VkInstance (equivalent to D3D object creation) ───────

void CHW::CreateD3D()
{
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "S.T.A.L.K.E.R. X-Ray VK";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion         = VK_MAKE_API_VERSION(0, 1, 4, 0);

    xr_vector<const char*> instanceExtensions;
    instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

    // Query supported instance extensions to enable VK_EXT_debug_utils whenever available
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    xr_vector<VkExtensionProperties> availableExts(extCount);
    if (extCount > 0)
        vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availableExts.data());

    bool hasDebugUtils = false;
    for (const auto& ext : availableExts)
    {
        if (xr_strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
        {
            hasDebugUtils = true;
            break;
        }
    }

    if (hasDebugUtils)
    {
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    xr_vector<const char*> instanceLayers;

    VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
    };
    VkValidationFeaturesEXT vf{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    vf.enabledValidationFeatureCount = _countof(enables);
    vf.pEnabledValidationFeatures    = enables;

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug"))
    {
        xrLogger::SetImmediateMode(true);
        instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
        instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        g_bVulkanDebugLog = true;
    }
#endif

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)instanceExtensions.size();
    ci.ppEnabledExtensionNames = instanceExtensions.data();
    ci.enabledLayerCount       = (uint32_t)instanceLayers.size();
    ci.ppEnabledLayerNames     = instanceLayers.data();

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug"))
    {
        ci.pNext = &vf;
    }
#endif

    VkResult res = vkCreateInstance(&ci, nullptr, &m_vkInstance);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateD3D — vkCreateInstance failed");

    Msg("* VK: instance created");

    if (hasDebugUtils)
    {
        g_vkCmdBeginDebugUtilsLabelEXT  = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_vkInstance, "vkCmdBeginDebugUtilsLabelEXT");
        g_vkCmdEndDebugUtilsLabelEXT    = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_vkInstance, "vkCmdEndDebugUtilsLabelEXT");
        g_vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_vkInstance, "vkCmdInsertDebugUtilsLabelEXT");
        g_vkSetDebugUtilsObjectNameEXT  = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(m_vkInstance, "vkSetDebugUtilsObjectNameEXT");
    }

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug"))
    {
        m_vkDebugMessenger = vk_CreateDebugMessenger(m_vkInstance);
    }
#endif
}

// ─── DestroyD3D ──────────────────────────────────────────────────────────────

void CHW::DestroyD3D()
{
#ifdef VK_ENABLE_TESTS
    if (m_vkDebugMessenger != VK_NULL_HANDLE)
    {
        vk_DestroyDebugMessenger(m_vkInstance, m_vkDebugMessenger);
        m_vkDebugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_vkInstance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_vkInstance, nullptr);
        m_vkInstance = VK_NULL_HANDLE;
    }
}

// ─── CreateDevice — physical device, logical device, surface, swapchain ──────

void CHW::CreateDevice(HWND hw, bool move_window)
{
    m_hWnd        = hw;
    m_move_window = move_window;

    // Surface
    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hwnd      = m_hWnd;
    sci.hinstance = GetModuleHandle(nullptr);
    VkResult res  = vkCreateWin32SurfaceKHR(m_vkInstance, &sci, nullptr, &m_vkSurface);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateDevice — vkCreateWin32SurfaceKHR failed");

    // Physical device
    R_ASSERT2(vk_SelectPhysicalDevice(), "CHW::CreateDevice — no suitable GPU found");
    R_ASSERT2(vk_FindQueueFamilies(),    "CHW::CreateDevice — no suitable queue families");

    // Cache device properties
    vkGetPhysicalDeviceProperties(m_vkPhysDevice,       &m_vkDevProps);
    vkGetPhysicalDeviceFeatures(m_vkPhysDevice,         &m_vkDevFeatures);
    vkGetPhysicalDeviceMemoryProperties(m_vkPhysDevice, &m_vkMemProps);
    Msg("* VK: GPU selected: %s", m_vkDevProps.deviceName);

    // Populate engine capability profile from Vulkan device properties
    Caps.Update();

    // Logical device
    float queuePriority = 1.0f;
    xr_vector<VkDeviceQueueCreateInfo> queueCIs;

    VkDeviceQueueCreateInfo gfxQCI{};
    gfxQCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    gfxQCI.queueFamilyIndex = m_vkGraphicsQF;
    gfxQCI.queueCount       = 1;
    gfxQCI.pQueuePriorities = &queuePriority;
    queueCIs.push_back(gfxQCI);

    if (m_vkPresentQF != m_vkGraphicsQF)
    {
        VkDeviceQueueCreateInfo preQCI = gfxQCI;
        preQCI.queueFamilyIndex = m_vkPresentQF;
        queueCIs.push_back(preQCI);
    }

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, // [Phase 11] Added for optimized UI descriptor binding
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, // Enables dynamic vertex stride and front face
    };

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeatures{};
    extDynStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extDynStateFeatures.extendedDynamicState = VK_TRUE;
    extDynStateFeatures.pNext = &features13;

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &extDynStateFeatures;
    dci.queueCreateInfoCount    = (uint32_t)queueCIs.size();
    dci.pQueueCreateInfos       = queueCIs.data();
    dci.enabledExtensionCount   = (uint32_t)std::size(deviceExtensions);
    dci.ppEnabledExtensionNames = deviceExtensions;
    dci.pEnabledFeatures        = &m_vkDevFeatures;

    res = vkCreateDevice(m_vkPhysDevice, &dci, nullptr, &m_vkDevice);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateDevice — vkCreateDevice failed");

    vkGetDeviceQueue(m_vkDevice, m_vkGraphicsQF, 0, &m_vkGraphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_vkPresentQF,  0, &m_vkPresentQueue);

    // Initialize VMA Memory Allocator
    MemoryManager.Initialize(m_vkInstance, m_vkPhysDevice, m_vkDevice);

    // Pipeline cache
    VkPipelineCacheCreateInfo pcci{};
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(m_vkDevice, &pcci, nullptr, &m_vkPipelineCache);

    // Initialize our Stateful Pipeline Cache Manager
    PipelineCache.Initialize(m_vkDevice, m_vkPipelineCache);

    // Initialize our Descriptor Set Ring Buffer
    DescriptorManager.Initialize(m_vkDevice, m_vkPhysDevice);

    // Command pool
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = m_vkGraphicsQF;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_vkDevice, &cpci, nullptr, &m_vkCmdPool);

    // Transfer pool (for deferred uploads on main thread)
    VkCommandPoolCreateInfo tpci{};
    tpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    tpci.queueFamilyIndex = m_vkGraphicsQF;
    tpci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_vkDevice, &tpci, nullptr, &m_vkTransferPool);

    // Swapchain + depth + framebuffers + sync
    vk_CreateSwapchain();
    vk_CreateDepthBuffer();
    vk_CreateFramebuffers();
    vk_CreateCommandBuffers();
    vk_CreateSyncObjects();

    updateWindowProps(hw);

    // [Phase 11] Load Push Descriptor extension function
    g_vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(m_vkDevice, "vkCmdPushDescriptorSetKHR");
    R_ASSERT2(g_vkCmdPushDescriptorSetKHR, "Failed to load vkCmdPushDescriptorSetKHR!");

    // Load Vulkan 1.3 Extended Dynamic States functions
    g_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)vkGetDeviceProcAddr(m_vkDevice, "vkCmdBindVertexBuffers2");
    if (!g_vkCmdBindVertexBuffers2) g_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)vkGetDeviceProcAddr(m_vkDevice, "vkCmdBindVertexBuffers2EXT");
    R_ASSERT2(g_vkCmdBindVertexBuffers2, "Failed to load vkCmdBindVertexBuffers2");

    g_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetFrontFace");
    if (!g_vkCmdSetFrontFace) g_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetFrontFaceEXT");
    R_ASSERT2(g_vkCmdSetFrontFace, "Failed to load vkCmdSetFrontFace");

    g_vkCmdSetCullMode = (PFN_vkCmdSetCullMode)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetCullMode");
    if (!g_vkCmdSetCullMode) g_vkCmdSetCullMode = (PFN_vkCmdSetCullMode)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetCullModeEXT");
    R_ASSERT2(g_vkCmdSetCullMode, "Failed to load vkCmdSetCullMode");

    g_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthTestEnable");
    if (!g_vkCmdSetDepthTestEnable) g_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthTestEnableEXT");
    R_ASSERT2(g_vkCmdSetDepthTestEnable, "Failed to load vkCmdSetDepthTestEnable");

    g_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilTestEnable");
    if (!g_vkCmdSetStencilTestEnable) g_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilTestEnableEXT");
    R_ASSERT2(g_vkCmdSetStencilTestEnable, "Failed to load vkCmdSetStencilTestEnable");

    g_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilOp");
    if (!g_vkCmdSetStencilOp) g_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilOpEXT");
    R_ASSERT2(g_vkCmdSetStencilOp, "Failed to load vkCmdSetStencilOp");

    g_vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilCompareMask");
    R_ASSERT2(g_vkCmdSetStencilCompareMask, "Failed to load vkCmdSetStencilCompareMask");

    g_vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilWriteMask");
    R_ASSERT2(g_vkCmdSetStencilWriteMask, "Failed to load vkCmdSetStencilWriteMask");

    g_vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilReference");
    R_ASSERT2(g_vkCmdSetStencilReference, "Failed to load vkCmdSetStencilReference");

    g_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthWriteEnable");
    if (!g_vkCmdSetDepthWriteEnable) g_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthWriteEnableEXT");
    R_ASSERT2(g_vkCmdSetDepthWriteEnable, "Failed to load vkCmdSetDepthWriteEnable");

    g_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthCompareOp");
    if (!g_vkCmdSetDepthCompareOp) g_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthCompareOpEXT");
    R_ASSERT2(g_vkCmdSetDepthCompareOp, "Failed to load vkCmdSetDepthCompareOp");

    g_vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetPrimitiveTopology");
    if (!g_vkCmdSetPrimitiveTopology) g_vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetPrimitiveTopologyEXT");
    R_ASSERT2(g_vkCmdSetPrimitiveTopology, "Failed to load vkCmdSetPrimitiveTopology");

#ifdef VK_ENABLE_TESTS
    int fails = vk_TestRegistry::Get().RunAll();
    if (fails > 0)
        Msg("! [VK_TEST] %d test(s) failed!", fails);
    else
        Msg("* [VK_TEST] All tests passed.");
#endif

    Msg("* VK: device created successfully");
}

// ─── CreateDevice_NoSwapchain — tool-mode boot (no swapchain / sync objects) ─
//
// Mirrors CreateDevice() exactly up to the swapchain block. Stops before:
//   vk_CreateSwapchain / vk_CreateDepthBuffer / vk_CreateFramebuffers /
//   vk_CreateCommandBuffers / vk_CreateSyncObjects
//
// Sets m_vkSCExtent to the requested dimensions so CRenderTarget allocates
// offscreen images at the correct size.  Creates a real depth buffer at that
// size via vk_CreateDepthBuffer() so CRenderTarget::pZB is valid.
// Spherical owns the swapchain; command buffers come from Spherical's frame loop.

void CHW::CreateDevice_NoSwapchain(HWND hw, bool move_window, u32 width, u32 height)
{
    m_hWnd        = hw;
    m_move_window = move_window;
    m_bToolMode   = true;

    // Surface — needed for queue-family present-support query
    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hwnd      = m_hWnd;
    sci.hinstance = GetModuleHandle(nullptr);
    VkResult res  = vkCreateWin32SurfaceKHR(m_vkInstance, &sci, nullptr, &m_vkSurface);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateDevice_NoSwapchain — vkCreateWin32SurfaceKHR failed");

    // Physical device
    R_ASSERT2(vk_SelectPhysicalDevice(), "CHW::CreateDevice_NoSwapchain — no suitable GPU found");
    R_ASSERT2(vk_FindQueueFamilies(),    "CHW::CreateDevice_NoSwapchain — no suitable queue families");

    // Cache device properties
    vkGetPhysicalDeviceProperties(m_vkPhysDevice,       &m_vkDevProps);
    vkGetPhysicalDeviceFeatures(m_vkPhysDevice,         &m_vkDevFeatures);
    vkGetPhysicalDeviceMemoryProperties(m_vkPhysDevice, &m_vkMemProps);
    Msg("* VK (tool): GPU selected: %s", m_vkDevProps.deviceName);

    Caps.Update();

    // Logical device
    float queuePriority = 1.0f;
    xr_vector<VkDeviceQueueCreateInfo> queueCIs;

    VkDeviceQueueCreateInfo gfxQCI{};
    gfxQCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    gfxQCI.queueFamilyIndex = m_vkGraphicsQF;
    gfxQCI.queueCount       = 1;
    gfxQCI.pQueuePriorities = &queuePriority;
    queueCIs.push_back(gfxQCI);

    if (m_vkPresentQF != m_vkGraphicsQF)
    {
        VkDeviceQueueCreateInfo preQCI = gfxQCI;
        preQCI.queueFamilyIndex = m_vkPresentQF;
        queueCIs.push_back(preQCI);
    }

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeatures{};
    extDynStateFeatures.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extDynStateFeatures.extendedDynamicState = VK_TRUE;
    extDynStateFeatures.pNext                = &features13;

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &extDynStateFeatures;
    dci.queueCreateInfoCount    = (uint32_t)queueCIs.size();
    dci.pQueueCreateInfos       = queueCIs.data();
    dci.enabledExtensionCount   = (uint32_t)std::size(deviceExtensions);
    dci.ppEnabledExtensionNames = deviceExtensions;
    dci.pEnabledFeatures        = &m_vkDevFeatures;

    res = vkCreateDevice(m_vkPhysDevice, &dci, nullptr, &m_vkDevice);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateDevice_NoSwapchain — vkCreateDevice failed");

    vkGetDeviceQueue(m_vkDevice, m_vkGraphicsQF, 0, &m_vkGraphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_vkPresentQF,  0, &m_vkPresentQueue);

    // VMA
    MemoryManager.Initialize(m_vkInstance, m_vkPhysDevice, m_vkDevice);

    // Pipeline cache
    VkPipelineCacheCreateInfo pcci{};
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(m_vkDevice, &pcci, nullptr, &m_vkPipelineCache);
    PipelineCache.Initialize(m_vkDevice, m_vkPipelineCache);

    // Descriptor manager
    DescriptorManager.Initialize(m_vkDevice, m_vkPhysDevice);

    // Command pool (used by one-shot transfer commands inside the resource manager)
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = m_vkGraphicsQF;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_vkDevice, &cpci, nullptr, &m_vkCmdPool);

    // Transfer pool (for deferred uploads on main thread)
    VkCommandPoolCreateInfo tpci{};
    tpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    tpci.queueFamilyIndex = m_vkGraphicsQF;
    tpci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_vkDevice, &tpci, nullptr, &m_vkTransferPool);

    // Set swapchain extent to the requested tool-mode dimensions.
    // CRenderTarget uses m_vkSCExtent to size its offscreen images.
    m_vkSCExtent = { width, height };

    // Allocate a real depth buffer at the requested dimensions so CRenderTarget::pZB
    // is valid. vk_CreateDepthBuffer() reads m_vkSCExtent internally.
    vk_CreateDepthBuffer();

    // NOTE: the following are intentionally skipped in tool mode —
    //   vk_CreateSwapchain()       — Spherical owns the swapchain
    //   vk_CreateFramebuffers()    — not needed without a swapchain render pass
    //   vk_CreateCommandBuffers()  — command buffers come from Spherical's frame loop
    //   vk_CreateSyncObjects()     — acquire/present semaphores not used in tool mode

    // Load device-level function pointers (same as CreateDevice)
    g_vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(m_vkDevice, "vkCmdPushDescriptorSetKHR");
    R_ASSERT2(g_vkCmdPushDescriptorSetKHR, "Failed to load vkCmdPushDescriptorSetKHR!");

    g_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)vkGetDeviceProcAddr(m_vkDevice, "vkCmdBindVertexBuffers2");
    if (!g_vkCmdBindVertexBuffers2) g_vkCmdBindVertexBuffers2 = (PFN_vkCmdBindVertexBuffers2)vkGetDeviceProcAddr(m_vkDevice, "vkCmdBindVertexBuffers2EXT");
    R_ASSERT2(g_vkCmdBindVertexBuffers2, "Failed to load vkCmdBindVertexBuffers2");

    g_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetFrontFace");
    if (!g_vkCmdSetFrontFace) g_vkCmdSetFrontFace = (PFN_vkCmdSetFrontFace)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetFrontFaceEXT");
    R_ASSERT2(g_vkCmdSetFrontFace, "Failed to load vkCmdSetFrontFace");

    g_vkCmdSetCullMode = (PFN_vkCmdSetCullMode)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetCullMode");
    if (!g_vkCmdSetCullMode) g_vkCmdSetCullMode = (PFN_vkCmdSetCullMode)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetCullModeEXT");
    R_ASSERT2(g_vkCmdSetCullMode, "Failed to load vkCmdSetCullMode");

    g_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthTestEnable");
    if (!g_vkCmdSetDepthTestEnable) g_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthTestEnableEXT");
    R_ASSERT2(g_vkCmdSetDepthTestEnable, "Failed to load vkCmdSetDepthTestEnable");

    g_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilTestEnable");
    if (!g_vkCmdSetStencilTestEnable) g_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilTestEnableEXT");
    R_ASSERT2(g_vkCmdSetStencilTestEnable, "Failed to load vkCmdSetStencilTestEnable");

    g_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilOp");
    if (!g_vkCmdSetStencilOp) g_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilOpEXT");
    R_ASSERT2(g_vkCmdSetStencilOp, "Failed to load vkCmdSetStencilOp");

    g_vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilCompareMask");
    R_ASSERT2(g_vkCmdSetStencilCompareMask, "Failed to load vkCmdSetStencilCompareMask");

    g_vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilWriteMask");
    R_ASSERT2(g_vkCmdSetStencilWriteMask, "Failed to load vkCmdSetStencilWriteMask");

    g_vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetStencilReference");
    R_ASSERT2(g_vkCmdSetStencilReference, "Failed to load vkCmdSetStencilReference");

    g_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthWriteEnable");
    if (!g_vkCmdSetDepthWriteEnable) g_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthWriteEnableEXT");
    R_ASSERT2(g_vkCmdSetDepthWriteEnable, "Failed to load vkCmdSetDepthWriteEnable");

    g_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthCompareOp");
    if (!g_vkCmdSetDepthCompareOp) g_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetDepthCompareOpEXT");
    R_ASSERT2(g_vkCmdSetDepthCompareOp, "Failed to load vkCmdSetDepthCompareOp");

    g_vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetPrimitiveTopology");
    if (!g_vkCmdSetPrimitiveTopology) g_vkCmdSetPrimitiveTopology = (PFN_vkCmdSetPrimitiveTopology)vkGetDeviceProcAddr(m_vkDevice, "vkCmdSetPrimitiveTopologyEXT");
    R_ASSERT2(g_vkCmdSetPrimitiveTopology, "Failed to load vkCmdSetPrimitiveTopology");

#ifdef VK_ENABLE_TESTS
    int fails = vk_TestRegistry::Get().RunAll();
    if (fails > 0)
        Msg("! [VK_TEST] %d test(s) failed!", fails);
    else
        Msg("* [VK_TEST] All tests passed.");
#endif

    Msg("* VK (tool): device created successfully (no swapchain)");
}

// ─── DestroyDevice ───────────────────────────────────────────────────────────

void CHW::DestroyDevice()
{
    if (m_vkDevice == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_vkDevice);

    // Drain ALL deferred transfers (and their cleanup lambdas) before we tear
    // down the transfer pool / VMA allocator. Loop because some cleanups
    // (vk_DestroyBuffer) re-enqueue into m_pendingTransfers; keep flushing
    // until the queue is genuinely empty, or those buffers leak (VUID-05137).
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(m_TransferMutex);
            if (m_pendingTransfers.empty()) break;
        }
        FlushDeferredTransfers();   // must run while m_vkTransferPool is still alive
    }
    // Destroy command pools to release references to images BEFORE the images are destroyed
    if (m_vkCmdPool != VK_NULL_HANDLE)
    { vkDestroyCommandPool(m_vkDevice, m_vkCmdPool, nullptr); m_vkCmdPool = VK_NULL_HANDLE; }
    if (m_vkTransferPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_vkDevice, m_vkTransferPool, nullptr);
        m_vkTransferPool = VK_NULL_HANDLE;
    }

    void vk_Texture_Cleanup();
    vk_Texture_Cleanup();

    vk_DestroySyncObjects();
    vk_DestroyFramebuffers();
    vk_DestroyDepthBuffer();
    vk_DestroySwapchain();

    // Clean up Pipeline Cache Manager
    PipelineCache.Destroy();

    // Clean up Descriptor Manager
    DescriptorManager.Destroy();

    if (m_vkPipelineCache != VK_NULL_HANDLE)
    { vkDestroyPipelineCache(m_vkDevice, m_vkPipelineCache, nullptr); m_vkPipelineCache = VK_NULL_HANDLE; }

    // Clean up VMA Allocator before device destruction
    MemoryManager.Destroy();

    vkDestroyDevice(m_vkDevice, nullptr);  m_vkDevice = VK_NULL_HANDLE;

    if (m_vkSurface != VK_NULL_HANDLE)
    { vkDestroySurfaceKHR(m_vkInstance, m_vkSurface, nullptr); m_vkSurface = VK_NULL_HANDLE; }

    DestroyD3D();
}

// ─── Reset — recreate swapchain on resolution/fullscreen change ───────────────

void CHW::Reset(HWND hw)
{
    m_hWnd = hw;
    updateWindowProps(hw);
    vk_RecreateSwapchain();
}

// ─── selectResolution / selectDepthStencil / etc. ────────────────────────────
// These are called by the engine's resolution-selection UI.
// VK equivalents query VkSurfaceCapabilitiesKHR instead of D3D formats.

void CHW::selectResolution(u32& dwWidth, u32& dwHeight, BOOL bWindowed)
{
    if (m_vkPhysDevice == VK_NULL_HANDLE) { dwWidth = 1920; dwHeight = 1080; return; }
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vkPhysDevice, m_vkSurface, &caps);
    dwWidth  = caps.currentExtent.width;
    dwHeight = caps.currentExtent.height;
}

D3DFORMAT CHW::selectDepthStencil(D3DFORMAT) { return D3DFMT_D24S8; } // stub — VK uses VkFormat
u32       CHW::selectPresentInterval()        { return 0; }
u32       CHW::selectGPU()                    { return 0; }
u32       CHW::selectRefresh(u32, u32, D3DFORMAT) { return 0; }
BOOL      CHW::support(D3DFORMAT, DWORD, DWORD)   { return TRUE; }

extern u32 g_screenmode;
extern void GetMonitorResolution(u32& horizontal, u32& vertical);
extern void GetMonitorPosition(int& x, int& y);

void CHW::updateWindowProps(HWND hw)
{
    if (!m_move_window || !hw) return;

    BOOL bWindowed = (g_screenmode != 2);

    if (bWindowed)
    {
        u32 dwWindowStyle = 0;
        if (g_screenmode == 1)
        {
            dwWindowStyle |= WS_POPUP;
        }
        else
        {
            dwWindowStyle |= WS_BORDER | WS_OVERLAPPEDWINDOW;
            if (!Core.ParamsData.test(ECoreParams::no_dialog_header))
                dwWindowStyle |= WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX;
        }

        SetWindowLongPtr(hw, GWL_STYLE, dwWindowStyle);

        u32 monW, monH;
        GetMonitorResolution(monW, monH);
        int monX, monY;
        GetMonitorPosition(monX, monY);

        if (psCurrentVidMode[0] == 0 || psCurrentVidMode[1] == 0)
            GetMonitorResolution(psCurrentVidMode[0], psCurrentVidMode[1]);

        LONG res_width = g_screenmode == 0 ? psCurrentVidMode[0] : monW;
        LONG res_height = g_screenmode == 0 ? psCurrentVidMode[1] : monH;

        RECT m_rcWindowBounds = { 0, 0, res_width, res_height };
        AdjustWindowRect(&m_rcWindowBounds, dwWindowStyle, FALSE);

        LONG w = m_rcWindowBounds.right - m_rcWindowBounds.left;
        LONG h = m_rcWindowBounds.bottom - m_rcWindowBounds.top;

        if (strstr(Core.Params, "-verify_settings_menu"))
            Msg("VERIFY_SETTINGS_MENU: CHW::updateWindowProps - Windowed. psCurrentVidMode=%dx%d, res_width=%d, res_height=%d, SetWindowPos(w=%d, h=%d)",
                psCurrentVidMode[0], psCurrentVidMode[1], res_width, res_height, w, h);

        SetWindowPos(hw,
            HWND_NOTOPMOST,
            monX + (LONG(monW) - w) / 2,
            monY + (LONG(monH) - h) / 2,
            w,
            h,
            SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
    }
    else
    {
        // Fullscreen
        SetWindowLongPtr(hw, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtr(hw, GWL_EXSTYLE, WS_EX_TOPMOST);

        u32 monW, monH;
        GetMonitorResolution(monW, monH);
        int monX, monY;
        GetMonitorPosition(monX, monY);

        SetWindowPos(hw, HWND_TOPMOST,
            monX, monY, monW, monH,
            SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
    }

    ShowCursor(FALSE);
    SetForegroundWindow(hw);

    RECT winRect;
    GetClientRect(hw, &winRect);
    MapWindowPoints(hw, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
    ClipCursor(&winRect);
}

// ─── Private helpers ──────────────────────────────────────────────────────────

bool CHW::vk_SelectPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
    if (count == 0) return false;

    xr_vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_vkInstance, &count, devices.data());

    // Prefer discrete GPU
    for (auto& d : devices)
    {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        { m_vkPhysDevice = d; return true; }
    }
    // Fallback: first available
    m_vkPhysDevice = devices[0];
    return true;
}

bool CHW::vk_FindQueueFamilies()
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysDevice, &count, nullptr);
    xr_vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysDevice, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            m_vkGraphicsQF = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_vkPhysDevice, i, m_vkSurface, &presentSupport);
        if (presentSupport)
            m_vkPresentQF = i;

        if (m_vkGraphicsQF != UINT32_MAX && m_vkPresentQF != UINT32_MAX)
            return true;
    }
    return false;
}

bool CHW::vk_SelectSurfaceFormat(VkSurfaceFormatKHR& outFmt) const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysDevice, m_vkSurface, &count, nullptr);
    xr_vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysDevice, m_vkSurface, &count, formats.data());

    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        { outFmt = f; return true; }

    outFmt = formats[0];
    return true;
}

VkResult CHW::PresentQueue(const VkPresentInfoKHR* pPresentInfo)
{
    std::lock_guard<std::mutex> lock(m_QueueLock);
    return vkQueuePresentKHR(m_vkPresentQueue, pPresentInfo);
}

VkPresentModeKHR CHW::vk_SelectPresentMode() const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysDevice, m_vkSurface, &count, nullptr);
    xr_vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysDevice, m_vkSurface, &count, modes.data());

    Msg("  VK present: vsync=%d, supported modes:", psDeviceFlags.test(rsVSync) ? 1 : 0);
    for (auto& m : modes) Msg("    mode %d", (int)m);

    // If V-Sync is disabled in the game options, prefer IMMEDIATE for uncapped framerate
    if (!psDeviceFlags.test(rsVSync))
    {
        for (auto& m : modes)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) { Msg("  VK present: IMMEDIATE"); return m; }
    }

    for (auto& m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { Msg("  VK present: MAILBOX"); return m; } // triple-buffer if available

    Msg("  VK present: FIFO (vsync)");
    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be present
}

VkExtent2D CHW::vk_SelectSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const
{
    if (caps.currentExtent.width != UINT32_MAX)
    {
        if (strstr(Core.Params, "-verify_settings_menu"))
            Msg("VERIFY_SETTINGS_MENU: CHW::vk_SelectSwapExtent - caps.currentExtent != UINT32_MAX, returning %dx%d", caps.currentExtent.width, caps.currentExtent.height);
        return caps.currentExtent;
    }

    RECT r{};
    GetClientRect(m_hWnd, &r);

    if (strstr(Core.Params, "-verify_settings_menu"))
        Msg("VERIFY_SETTINGS_MENU: CHW::vk_SelectSwapExtent - caps.currentExtent == UINT32_MAX, GetClientRect returned %dx%d", r.right - r.left, r.bottom - r.top);
    VkExtent2D ext { (uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top) };
    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}
/*
// Inside CHW::CreateDevice(HWND hWnd, ...)

VkWin32SurfaceCreateInfoKHR surfaceInfo{};
surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
surfaceInfo.hwnd  = hWnd;
surfaceInfo.hinstance = GetModuleHandle(nullptr);

VkResult res = vkCreateWin32SurfaceKHR(m_vkInstance, &surfaceInfo, nullptr, &m_vkSurface);
R_ASSERT2(res == VK_SUCCESS, "Vulkan: Failed to attach native Win32 window surface.");

*/

// ─── Swapchain ────────────────────────────────────────────────────────────────

void CHW::vk_CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vkPhysDevice, m_vkSurface, &caps);

    VkSurfaceFormatKHR fmt{};
    vk_SelectSurfaceFormat(fmt);
    m_vkSCFormat     = fmt.format;
    m_vkSCColorSpace = fmt.colorSpace;
    m_vkPresentMode  = vk_SelectPresentMode();
    m_vkSCExtent     = vk_SelectSwapExtent(caps);
    m_vkSCImageCount = std::clamp(caps.minImageCount + 1,
                                  caps.minImageCount,
                                  caps.maxImageCount > 0 ? caps.maxImageCount : UINT32_MAX);

    VkSwapchainCreateInfoKHR scci{};
    scci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scci.surface          = m_vkSurface;
    scci.minImageCount    = m_vkSCImageCount;
    scci.imageFormat      = m_vkSCFormat;
    scci.imageColorSpace  = m_vkSCColorSpace;
    scci.imageExtent      = m_vkSCExtent;
    scci.imageArrayLayers = 1;
    scci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    scci.preTransform     = caps.currentTransform;
    scci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scci.presentMode      = m_vkPresentMode;
    scci.clipped          = VK_TRUE;

    uint32_t qfIndices[] = { m_vkGraphicsQF, m_vkPresentQF };
    if (m_vkGraphicsQF != m_vkPresentQF)
    {
        scci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        scci.queueFamilyIndexCount = 2;
        scci.pQueueFamilyIndices   = qfIndices;
    }
    else
    {
        scci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult res = vkCreateSwapchainKHR(m_vkDevice, &scci, nullptr, &m_vkSwapchain);
    R_ASSERT2(res == VK_SUCCESS, "vk_CreateSwapchain failed");

    // Retrieve images
    vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapchain, &m_vkSCImageCount, nullptr);
    m_vkSCImages.resize(m_vkSCImageCount);
    vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapchain, &m_vkSCImageCount, m_vkSCImages.data());

    // Create image views
    m_vkSCImageViews.resize(m_vkSCImageCount);
    for (uint32_t i = 0; i < m_vkSCImageCount; ++i)
    {
        VkImageViewCreateInfo ivci{};
        ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                           = m_vkSCImages[i];
        ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                          = m_vkSCFormat;
        ivci.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY };
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;
        vkCreateImageView(m_vkDevice, &ivci, nullptr, &m_vkSCImageViews[i]);

        string64 imgName;
        xr_sprintf(imgName, "Swapchain Image #%u", i);
        vk_SetDebugName(m_vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)m_vkSCImages[i], imgName);
        string64 viewName;
        xr_sprintf(viewName, "Swapchain ImageView #%u", i);
        vk_SetDebugName(m_vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)m_vkSCImageViews[i], viewName);
    }

    // Create render finished semaphores (one per swapchain image)
    m_vkRenderFinished.resize(m_vkSCImageCount);
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < m_vkSCImageCount; ++i)
    {
        vkCreateSemaphore(m_vkDevice, &sci, nullptr, &m_vkRenderFinished[i]);
    }
}

void CHW::vk_DestroySwapchain()
{
    for (auto& sem : m_vkRenderFinished)
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(m_vkDevice, sem, nullptr);
    m_vkRenderFinished.clear();

    for (auto& iv : m_vkSCImageViews)
        if (iv != VK_NULL_HANDLE) vkDestroyImageView(m_vkDevice, iv, nullptr);
    m_vkSCImageViews.clear();
    m_vkSCImages.clear();

    if (m_vkSwapchain != VK_NULL_HANDLE)
    { vkDestroySwapchainKHR(m_vkDevice, m_vkSwapchain, nullptr); m_vkSwapchain = VK_NULL_HANDLE; }
}

void CHW::vk_RecreateSwapchain()
{
    vkDeviceWaitIdle(m_vkDevice);
    vk_DestroyFramebuffers();
    vk_DestroyDepthBuffer();
    vk_DestroySwapchain();
    vk_CreateSwapchain();
    vk_CreateDepthBuffer();
    if (m_vkRenderPass != VK_NULL_HANDLE) // only rebuild if CRender has created the render pass
        vk_CreateFramebuffers();

    // Ensure CRenderTarget refreshes its depth buffer handles (pZB) which were just destroyed and recreated.
    CRender* render = (CRender*)::Render;
    if (render && render->Target)
        render->Target->reset_end();
}

// ─── Depth buffer ─────────────────────────────────────────────────────────────

void CHW::vk_CreateDepthBuffer()
{
    m_vkDepthFormat = vk_GetDepthFormat();

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = m_vkDepthFormat;
    ici.extent        = { m_vkSCExtent.width, m_vkSCExtent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    CHK_VK(vkCreateImage(m_vkDevice, &ici, nullptr, &m_vkDepthImage));

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(m_vkDevice, m_vkDepthImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = vk_FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHK_VK(vkAllocateMemory(m_vkDevice, &mai, nullptr, &m_vkDepthMemory));
    CHK_VK(vkBindImageMemory(m_vkDevice, m_vkDepthImage, m_vkDepthMemory, 0));

    VkImageViewCreateInfo ivci{};
    ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image                           = m_vkDepthImage;
    ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format                          = m_vkDepthFormat;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.layerCount     = 1;
    CHK_VK(vkCreateImageView(m_vkDevice, &ivci, nullptr, &m_vkDepthView));

    vk_SetDebugName(m_vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)m_vkDepthImage, "Primary Depth Buffer (pZB)");
    vk_SetDebugName(m_vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)m_vkDepthView, "Primary Depth View (pZB)");
}

void CHW::vk_DestroyDepthBuffer()
{
    if (m_vkDepthView   != VK_NULL_HANDLE) { vkDestroyImageView(m_vkDevice,  m_vkDepthView,   nullptr); m_vkDepthView   = VK_NULL_HANDLE; }
    if (m_vkDepthImage  != VK_NULL_HANDLE) { vkDestroyImage(m_vkDevice,      m_vkDepthImage,  nullptr); m_vkDepthImage  = VK_NULL_HANDLE; }
    if (m_vkDepthMemory != VK_NULL_HANDLE) { vkFreeMemory(m_vkDevice,        m_vkDepthMemory, nullptr); m_vkDepthMemory = VK_NULL_HANDLE; }
}

// ─── Framebuffers (stubs — need render pass to be created first by CRender) ──

void CHW::vk_CreateFramebuffers()  { /* populated by CRender after render pass creation */ }
void CHW::vk_DestroyFramebuffers()
{
    for (auto& fb : m_vkFramebuffers)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(m_vkDevice, fb, nullptr);
    m_vkFramebuffers.clear();
}

// ─── Command buffers ──────────────────────────────────────────────────────────

void CHW::vk_CreateCommandBuffers()
{
    m_vkCmdBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_vkCmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    vkAllocateCommandBuffers(m_vkDevice, &ai, m_vkCmdBuffers.data());

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        string64 cmdName;
        xr_sprintf(cmdName, "Frame CommandBuffer #%u", i);
        vk_SetDebugName(m_vkDevice, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)m_vkCmdBuffers[i], cmdName);
    }
}

// ─── Sync objects ─────────────────────────────────────────────────────────────

void CHW::vk_CreateSyncObjects()
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkCreateSemaphore(m_vkDevice, &sci, nullptr, &m_vkImageAvailable[i]);
        vkCreateFence(m_vkDevice,     &fci, nullptr, &m_vkInFlightFences[i]);
    }
}

void CHW::vk_DestroySyncObjects()
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_vkImageAvailable[i] != VK_NULL_HANDLE) { vkDestroySemaphore(m_vkDevice, m_vkImageAvailable[i], nullptr); m_vkImageAvailable[i] = VK_NULL_HANDLE; }
        if (m_vkInFlightFences[i] != VK_NULL_HANDLE) { vkDestroyFence(m_vkDevice,     m_vkInFlightFences[i], nullptr); m_vkInFlightFences[i] = VK_NULL_HANDLE; }
    }
}

// ─── Present ──────────────────────────────────────────────────────────────────

// imageIndex must be the value returned by vkAcquireNextImageKHR immediately before command
// recording for this frame. Swapchain image ordering is driver-defined and never guaranteed
// to match the frame counter, so deriving it from m_vkCurrentFrame % m_vkSCImageCount is wrong.
VkResult CHW::vk_Present(uint32_t imageIndex)
{
    const uint32_t frameSlot = m_vkCurrentFrame % MAX_FRAMES_IN_FLIGHT;

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_vkRenderFinished[imageIndex];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_vkSwapchain;
    pi.pImageIndices      = &imageIndex;

    VkResult res = PresentQueue(&pi);
    m_vkCurrentFrame++;
    return res;
}

// ─── Memory helper ────────────────────────────────────────────────────────────

uint32_t CHW::vk_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const
{
    for (uint32_t i = 0; i < m_vkMemProps.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) &&
            (m_vkMemProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    R_ASSERT2(false, "vk_FindMemoryType — no suitable memory type found");
    return UINT32_MAX;
}

// ─── Validate (debug build only) ─────────────────────────────────────────────

#ifdef DEBUG
// Nothing to validate yet — add VK_EXT_debug_utils messenger here later
#endif

// -----------------------------------------------------------------------------
// Queue Synchronization Wrappers
// -----------------------------------------------------------------------------
VkResult CHW::SubmitQueue(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_QueueLock);
    return vkQueueSubmit(m_vkGraphicsQueue, submitCount, pSubmits, fence);
}

VkResult CHW::SubmitQueueAndWait(uint32_t submitCount, const VkSubmitInfo* pSubmits)
{
    std::lock_guard<std::mutex> lock(m_QueueLock);
    VkResult res = vkQueueSubmit(m_vkGraphicsQueue, submitCount, pSubmits, VK_NULL_HANDLE);
    if (res == VK_SUCCESS)
        res = vkQueueWaitIdle(m_vkGraphicsQueue);
    return res;
}

VkResult CHW::WaitQueueIdle()
{
    std::lock_guard<std::mutex> lock(m_QueueLock);
    return vkQueueWaitIdle(m_vkGraphicsQueue);
}

// -----------------------------------------------------------------------------
// Deferred Transfers (Fix 1)
// -----------------------------------------------------------------------------
void CHW::QueueTransfer(std::function<void(VkCommandBuffer)> recordCmds, std::function<void()> cleanup)
{
    std::lock_guard<std::mutex> lock(m_TransferMutex);
    m_pendingTransfers.push_back({ recordCmds, cleanup });
}

void CHW::FlushDeferredTransfers()
{
    xr_vector<DeferredTransfer> transfersToProcess;
    {
        std::lock_guard<std::mutex> lock(m_TransferMutex);
        if (m_pendingTransfers.empty()) return;
        transfersToProcess.swap(m_pendingTransfers);
    }

    // [PERF] gate + per-flush timing
    const bool perfLog = !!strstr(Core.Params, "-vk_xfer_perf");
    CTimer     tFlush;              // total flush time
    if (perfLog) tFlush.Start();
    const size_t xferCount = transfersToProcess.size();

    std::lock_guard<std::mutex> poolLock(m_TransferPoolMutex);

    if (m_vkTransferPool == VK_NULL_HANDLE)
    {
        Msg("! CHW::FlushDeferredTransfers - m_vkTransferPool is null!");
        return;
    }

    // Allocate a one-shot command buffer for all transfers
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_vkTransferPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_vkDevice, &ai, &cmd) != VK_SUCCESS)
    {
        Msg("! CHW::FlushDeferredTransfers - Failed to allocate command buffer");
        return;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    for (auto& t : transfersToProcess)
    {
        t.recordCmds(cmd);
    }

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;

    CTimer tWait;                   // just the drain
    if (perfLog) tWait.Start();
    SubmitQueueAndWait(1, &si);

    if (perfLog)
    {
        const float msWait  = tWait.GetElapsed_sec()  * 1000.0f;
        const float msTotal = tFlush.GetElapsed_sec() * 1000.0f;

        static u32   s_frame      = 0xffffffff;
        static u32   s_flushes    = 0;
        static u32   s_xfers      = 0;
        static float s_msWaitSum  = 0.f;
        static float s_msTotalSum = 0.f;

        if (Device.dwFrame != s_frame)
        {
            if (s_frame != 0xffffffff && s_flushes)
                Msg("  VK-XFER  frame %u: flushes=%u xfers=%u waitIdle=%.3fms total=%.3fms",
                    s_frame, s_flushes, s_xfers, s_msWaitSum, s_msTotalSum);
            s_frame = Device.dwFrame; s_flushes = 0; s_xfers = 0;
            s_msWaitSum = 0.f; s_msTotalSum = 0.f;
        }
        s_flushes++; s_xfers += (u32)xferCount;
        s_msWaitSum += msWait; s_msTotalSum += msTotal;
    }

    vkFreeCommandBuffers(m_vkDevice, m_vkTransferPool, 1, &cmd);

    // Cleanup resources (e.g. destroy staging buffers)
    for (auto& t : transfersToProcess)
    {
        t.cleanup();
    }
}

CHW HW;
