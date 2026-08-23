#include "stdafx.h"
#include "../../xrRender/SH_Texture.h"
#include "vk_Texture.h"

// VK implementation of CTexture::desc_update().
// pSurface is a VkTexture2DWrapper* cast to ID3DBaseTexture* — no D3D API calls allowed.
void CTexture::desc_update()
{
    while (flags.bLoading)
        SwitchToThread();

    desc_cache = pSurface;

    if (pSurface)
    {
        VkTexture2DWrapper* wrapper = reinterpret_cast<VkTexture2DWrapper*>(pSurface);
        ZeroMemory(&desc, sizeof(desc));
        desc.Width     = wrapper->width;
        desc.Height    = wrapper->height;
        desc.MipLevels = wrapper->mips;
        desc.ArraySize = 1;
    }
}
