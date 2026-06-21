#include "stdafx.h"

// Forward declared in vk_test_hw.cpp
extern BOOL xrRender_test_surface(HWND hWnd);

// ─── CHW constructor / destructor ────────────────────────────────────────────

CHW::CHW()
    : m_move_window(false)
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

    const char* instanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)std::size(instanceExtensions);
    ci.ppEnabledExtensionNames = instanceExtensions;

    VkResult res = vkCreateInstance(&ci, nullptr, &m_vkInstance);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateD3D — vkCreateInstance failed");

    Msg("* VK: instance created");
}

// ─── DestroyD3D ──────────────────────────────────────────────────────────────

void CHW::DestroyD3D()
{
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
    };

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = (uint32_t)queueCIs.size();
    dci.pQueueCreateInfos       = queueCIs.data();
    dci.enabledExtensionCount   = (uint32_t)std::size(deviceExtensions);
    dci.ppEnabledExtensionNames = deviceExtensions;
    dci.pEnabledFeatures        = &m_vkDevFeatures;

    res = vkCreateDevice(m_vkPhysDevice, &dci, nullptr, &m_vkDevice);
    R_ASSERT2(res == VK_SUCCESS, "CHW::CreateDevice — vkCreateDevice failed");

    vkGetDeviceQueue(m_vkDevice, m_vkGraphicsQF, 0, &m_vkGraphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_vkPresentQF,  0, &m_vkPresentQueue);

    // Pipeline cache
    VkPipelineCacheCreateInfo pcci{};
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(m_vkDevice, &pcci, nullptr, &m_vkPipelineCache);

    // Command pool
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = m_vkGraphicsQF;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_vkDevice, &cpci, nullptr, &m_vkCmdPool);

    // Swapchain + depth + framebuffers + sync
    vk_CreateSwapchain();
    vk_CreateDepthBuffer();
    vk_CreateFramebuffers();
    vk_CreateCommandBuffers();
    vk_CreateSyncObjects();

    updateWindowProps(hw);
    Msg("* VK: device created successfully");
}

// ─── DestroyDevice ───────────────────────────────────────────────────────────

void CHW::DestroyDevice()
{
    if (m_vkDevice == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_vkDevice);

    vk_DestroySyncObjects();
    vk_DestroyFramebuffers();
    vk_DestroyDepthBuffer();
    vk_DestroySwapchain();

    if (m_vkPipelineCache != VK_NULL_HANDLE)
    { vkDestroyPipelineCache(m_vkDevice, m_vkPipelineCache, nullptr); m_vkPipelineCache = VK_NULL_HANDLE; }
    if (m_vkCmdPool != VK_NULL_HANDLE)
    { vkDestroyCommandPool(m_vkDevice, m_vkCmdPool, nullptr); m_vkCmdPool = VK_NULL_HANDLE; }

    vkDestroyDevice(m_vkDevice, nullptr);  m_vkDevice = VK_NULL_HANDLE;

    if (m_vkSurface != VK_NULL_HANDLE)
    { vkDestroySurfaceKHR(m_vkInstance, m_vkSurface, nullptr); m_vkSurface = VK_NULL_HANDLE; }

    DestroyD3D();
}

// ─── Reset — recreate swapchain on resolution/fullscreen change ───────────────

void CHW::Reset(HWND hw)
{
    m_hWnd = hw;
    vk_RecreateSwapchain();
    updateWindowProps(hw);
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

void CHW::updateWindowProps(HWND hw)
{
    if (!m_move_window || !hw) return;
    // Resize/reposition handled by swapchain extent
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

VkPresentModeKHR CHW::vk_SelectPresentMode() const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysDevice, m_vkSurface, &count, nullptr);
    xr_vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysDevice, m_vkSurface, &count, modes.data());

    for (auto& m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m; // triple-buffer if available

    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be present
}

VkExtent2D CHW::vk_SelectSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    RECT r{};
    GetClientRect(m_hWnd, &r);
    VkExtent2D ext { (uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top) };
    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}

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
    scci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
    }
}

void CHW::vk_DestroySwapchain()
{
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
}

// ─── Depth buffer ─────────────────────────────────────────────────────────────

void CHW::vk_CreateDepthBuffer()
{
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
    vkCreateImage(m_vkDevice, &ici, nullptr, &m_vkDepthImage);

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(m_vkDevice, m_vkDepthImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = vk_FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_vkDevice, &mai, nullptr, &m_vkDepthMemory);
    vkBindImageMemory(m_vkDevice, m_vkDepthImage, m_vkDepthMemory, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image                           = m_vkDepthImage;
    ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format                          = m_vkDepthFormat;
    ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.layerCount     = 1;
    vkCreateImageView(m_vkDevice, &ivci, nullptr, &m_vkDepthView);
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
    m_vkCmdBuffers.resize(m_vkSCImageCount);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_vkCmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = m_vkSCImageCount;
    vkAllocateCommandBuffers(m_vkDevice, &ai, m_vkCmdBuffers.data());
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
        vkCreateSemaphore(m_vkDevice, &sci, nullptr, &m_vkRenderFinished[i]);
        vkCreateFence(m_vkDevice,     &fci, nullptr, &m_vkInFlightFences[i]);
    }
}

void CHW::vk_DestroySyncObjects()
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_vkImageAvailable[i] != VK_NULL_HANDLE) { vkDestroySemaphore(m_vkDevice, m_vkImageAvailable[i], nullptr); m_vkImageAvailable[i] = VK_NULL_HANDLE; }
        if (m_vkRenderFinished[i] != VK_NULL_HANDLE) { vkDestroySemaphore(m_vkDevice, m_vkRenderFinished[i], nullptr); m_vkRenderFinished[i] = VK_NULL_HANDLE; }
        if (m_vkInFlightFences[i] != VK_NULL_HANDLE) { vkDestroyFence(m_vkDevice,     m_vkInFlightFences[i], nullptr); m_vkInFlightFences[i] = VK_NULL_HANDLE; }
    }
}

// ─── Present ──────────────────────────────────────────────────────────────────

VkResult CHW::vk_Present()
{
    uint32_t imageIndex = m_vkCurrentFrame % m_vkSCImageCount;
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_vkRenderFinished[m_vkCurrentFrame % MAX_FRAMES_IN_FLIGHT];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_vkSwapchain;
    pi.pImageIndices      = &imageIndex;

    VkResult res = vkQueuePresentKHR(m_vkPresentQueue, &pi);
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

CHW HW;
