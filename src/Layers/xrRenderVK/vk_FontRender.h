#pragma once
// vk_FontRender.h — Vulkan implementation of IFontRender

#include "../../Include/xrRender/FontRender.h"
#include "../../xrRender/SH_Texture.h"

class vkFontRender : public IFontRender
{
public:
    vkFontRender();
    virtual ~vkFontRender();

    virtual void Initialize(LPCSTR cShader, LPCSTR cTexture) override;
    virtual void OnRender(CGameFont& owner) override;

private:
    ref_shader pShader;
    ref_geom   pGeom;
    ref_texture pFontTexture;
    bool        m_texVerified = false;
    xr_string   m_initTextureName;
};

