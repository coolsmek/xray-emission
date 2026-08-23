#include "stdafx.h"
#include "vk_TextureUtils.h"

VkFormat vk_GetFormat(D3DFORMAT fmt)
{
    switch (fmt)
    {
    case D3DFMT_UNKNOWN: return VK_FORMAT_UNDEFINED;
    case D3DFMT_A8R8G8B8: return VK_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_R5G6B5: return VK_FORMAT_B8G8R8A8_UNORM; // Not directly supported, map to 32-bit
    case D3DFMT_A8B8G8R8: return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_G16R16: return VK_FORMAT_R16G16_UNORM;
    case D3DFMT_A16B16G16R16: return VK_FORMAT_R16G16B16A16_UNORM;
    case D3DFMT_L8: return VK_FORMAT_R8_UNORM;
    case D3DFMT_V8U8: return VK_FORMAT_R8G8_SNORM;
    case D3DFMT_Q8W8V8U8: return VK_FORMAT_R8G8B8A8_SNORM;
    case D3DFMT_V16U16: return VK_FORMAT_B10G11R11_UFLOAT_PACK32; // Used for HDR hack, same as DX10 R11G11B10_FLOAT
    case D3DFMT_D24X8: return VK_FORMAT_D24_UNORM_S8_UINT; // Fallback or typeless mapped
    case D3DFMT_D32F_LOCKABLE: return VK_FORMAT_D32_SFLOAT;
    case D3DFMT_G16R16F: return VK_FORMAT_R16G16_SFLOAT;
    case D3DFMT_A16B16G16R16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case D3DFMT_R32F: return VK_FORMAT_R32_SFLOAT;
    case D3DFMT_R16F: return VK_FORMAT_R16_SFLOAT;
    case D3DFMT_A32B32G32R32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case D3DFMT_A2R10G10B10: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    default:
        VERIFY(!"vk_GetFormat didn't find appropriate Vulkan texture format!");
        return VK_FORMAT_UNDEFINED;
    }
}

VkFormat vk_GetDepthFormat()
{
    // Try D24_UNORM_S8 first
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(HW.m_vkPhysDevice, VK_FORMAT_D24_UNORM_S8_UINT, &props);
    if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        return VK_FORMAT_D24_UNORM_S8_UINT;
    }

    // Fallback to D32_SFLOAT_S8
    vkGetPhysicalDeviceFormatProperties(HW.m_vkPhysDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &props);
    if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    VERIFY(!"No suitable depth format found!");
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
}
