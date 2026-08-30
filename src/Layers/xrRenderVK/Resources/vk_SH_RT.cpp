#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"
#include "vk_TextureUtils.h"

#include "../xrRender/dxRenderDeviceRender.h"
#define CHK_VK(expr) do { VkResult res = (expr); R_ASSERT2(res == VK_SUCCESS, "Vulkan error"); } while(0)

CRT::CRT()
{
    pSurface = nullptr;
    pRT = nullptr;
    pZRT = nullptr;
    pUAView = nullptr;
    dwWidth = 0;
    dwHeight = 0;
    fmt = D3DFMT_UNKNOWN;
}

CRT::~CRT()
{
    destroy();
    DEV->_DeleteRT(this);
}

void CRT::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount, bool useUAV)
{
    if (pSurface) return;

    R_ASSERT(HW.m_vkDevice && Name && Name[0] && w && h);
    _order = CPU::GetCLK();

    dwWidth = w;
    dwHeight = h;
    fmt = f;

    u32 usage = 0;
    bool bUseAsDepth = false;

    if (D3DFMT_D24X8 == fmt || D3DFMT_D24S8 == fmt || D3DFMT_D15S1 == fmt || D3DFMT_D16 == fmt ||
        D3DFMT_D16_LOCKABLE == fmt || D3DFMT_D32F_LOCKABLE == fmt || (D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4') == fmt)
    {
        usage = D3DUSAGE_DEPTHSTENCIL;
        bUseAsDepth = true;
    }
    else
    {
        usage = D3DUSAGE_RENDERTARGET;
    }

    VkFormat vkFmt;
    if (bUseAsDepth) {
        vkFmt = vk_GetDepthFormat();
    } else {
        vkFmt = vk_GetFormat(fmt);
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = w;
    imageInfo.extent.height = h;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = vkFmt;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (bUseAsDepth) {
        imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
        // TRANSFER_SRC_BIT allows phase_combine() to blit this RT to the swapchain.
        // TRANSFER_DST_BIT allows PDA/SecondVP path to blit the swapchain back into this RT.
        imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (!bUseAsDepth && useUAV) {
        imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = (VkSampleCountFlagBits)SampleCount;

    VkImage vkImage;
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: calling vkCreateImage"); xrLogger::FlushLog(); }
#endif
    CHK_VK(vkCreateImage(HW.m_vkDevice, &imageInfo, nullptr, &vkImage));
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: vkCreateImage success"); xrLogger::FlushLog(); }
#endif

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(HW.m_vkDevice, vkImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = HW.vk_FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory vkMemory;
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: calling vkAllocateMemory"); xrLogger::FlushLog(); }
#endif
    CHK_VK(vkAllocateMemory(HW.m_vkDevice, &allocInfo, nullptr, &vkMemory));
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: vkAllocateMemory success"); xrLogger::FlushLog(); }

    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: calling vkBindImageMemory"); xrLogger::FlushLog(); }
#endif
    CHK_VK(vkBindImageMemory(HW.m_vkDevice, vkImage, vkMemory, 0));
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: vkBindImageMemory success"); xrLogger::FlushLog(); }
#endif

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFmt;

    if (bUseAsDepth) {
        // A sampled/attachment image view MUST pick a single aspect. For the shadow
        // map we sample DEPTH only — never include STENCIL here (VUID-01976).
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView vkView;
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: calling vkCreateImageView"); xrLogger::FlushLog(); }
#endif
    CHK_VK(vkCreateImageView(HW.m_vkDevice, &viewInfo, nullptr, &vkView));
#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: vkCreateImageView success"); xrLogger::FlushLog(); }
#endif

    string128 rtImgName, rtViewName;
    xr_sprintf(rtImgName, "RT Image: %s", Name);
    xr_sprintf(rtViewName, "RT View: %s", Name);
    vk_SetDebugName(HW.m_vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)vkImage, rtImgName);
    vk_SetDebugName(HW.m_vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)vkView, rtViewName);

    VkTexture2DWrapper* pTex = new VkTexture2DWrapper();
    pTex->image = vkImage;
    pTex->memory = vkMemory;
    pTex->format = vkFmt;
    pTex->width = w;
    pTex->height = h;
    pTex->mips = 1;
    pTex->imageView = vkView;

    VkSamplerCreateInfo sampCI{};
    sampCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCI.magFilter = VK_FILTER_LINEAR;
    sampCI.minFilter = VK_FILTER_LINEAR;
    sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    
    if (strstr(Name, "smap_surf") || strstr(Name, "smap_depth"))
    {
        sampCI.compareEnable = VK_TRUE;
        sampCI.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
        // PCF needs linear filtering; clamp to border = fully-lit outside the map
        sampCI.magFilter = sampCI.minFilter = VK_FILTER_LINEAR;
        sampCI.anisotropyEnable = VK_FALSE;   // comparison samplers can't be anisotropic
        sampCI.addressModeU = sampCI.addressModeV = sampCI.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }

    vkCreateSampler(HW.m_vkDevice, &sampCI, nullptr, &pTex->sampler);

    pSurface = pTex;

    if (bUseAsDepth) {
        VkDSVWrapper* pDSV = new VkDSVWrapper();
        pDSV->image         = vkImage;
        pDSV->view          = vkView;
        pDSV->format        = vkFmt;
        pDSV->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED; // will be transitioned on first u_setrt()
        pDSV->width         = w;
        pDSV->height        = h;
        pZRT = pDSV;
    } else {
        VkRTVWrapper* pRTV = new VkRTVWrapper();
        pRTV->image         = vkImage;
        pRTV->view          = vkView;
        pRTV->format        = vkFmt;
        pRTV->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED; // will be transitioned on first u_setrt()
        pRTV->width         = w;
        pRTV->height        = h;
        pRT = pRTV;

        if (useUAV) {
            VkUAVWrapper* pUAV = new VkUAVWrapper();
            pUAV->view = vkView;
            pUAView = pUAV;
        }
    }

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: calling DEV->_CreateTexture(%s)", Name); xrLogger::FlushLog(); }
#endif
    pTexture = DEV->_CreateTexture(Name);

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: returned from DEV->_CreateTexture"); xrLogger::FlushLog(); }
#endif
    pTexture->surface_set(pSurface);

#ifdef VK_ENABLE_TESTS
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CRT::create: done"); xrLogger::FlushLog(); }
#endif
}

void CRT::destroy()
{
    if (pTexture._get())
    {
        pTexture->surface_set(0);
        pTexture.destroy();
        pTexture = nullptr;
    }

    if (pZRT) {
        vkDestroyImageView(HW.m_vkDevice, pZRT->view, nullptr);
        delete pZRT;
        pZRT = nullptr;
    }

    if (pRT) {
        vkDestroyImageView(HW.m_vkDevice, pRT->view, nullptr);
        delete pRT;
        pRT = nullptr;
    }

    if (pUAView) {
        delete pUAView;
        pUAView = nullptr;
    }

    if (pSurface) {
        if (pSurface->sampler) vkDestroySampler(HW.m_vkDevice, pSurface->sampler, nullptr);
        vkDestroyImage(HW.m_vkDevice, pSurface->image, nullptr);
        vkFreeMemory(HW.m_vkDevice, pSurface->memory, nullptr);
        delete pSurface;
        pSurface = nullptr;
    }
}

void CRT::reset_begin()
{
    destroy();
}

void CRT::reset_end()
{
    create(*cName, dwWidth, dwHeight, fmt);
}

void resptrcode_crt::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount, bool useUAV)
{
    _set(DEV->_CreateRT(Name, w, h, f, SampleCount, useUAV));
}
