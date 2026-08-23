#pragma once
#include "../../Include/xrRender/UIRender.h"
#include "../../Include/xrRender/UIShader.h"
#include "vk_UIShader.h"

class cvkUIRender : public IUIRender
{
public:
    virtual ~cvkUIRender() {}

    // ── Geometry-stream methods ──────────────────────────────────────────────
    virtual void CreateUIGeom();
    virtual void DestroyUIGeom();
    virtual void StartPrimitive(u32 iMaxVerts, ePrimitiveType pt, ePointType ptt);
    virtual void FlushPrimitive();
    
    virtual void PushPoint(float x, float y, float z, u32 C, float u, float v);
    virtual void PushPoint(int x, int y, u32 C, float u, float v) {
        PushPoint((float)x, (float)y, 0.0f, C, u, v);
    }
    virtual void PushPoint(float x, float y, u32 C, float u, float v) {
        PushPoint(x, y, 0.0f, C, u, v);
    }

    // ── Shader binding ────────────────────────────────────────────────────────────
    // IUIShader is an abstract interface; the concrete implementation (vkUIShader)
    // stores a ref_shader.
    virtual void SetShader(IUIShader& shader)
    {
        auto* pShader = static_cast<vkUIShader*>(&shader);
        RCache.set_Shader(pShader->hShader);
    }

    // ── Rasteriser state ──────────────────────────────────────────────────────
    virtual void SetAlphaRef(int aref)
    {
        RCache.set_AlphaRef(aref);
    }

    virtual void SetScissor(Irect* rect)
    {
        VkCommandBuffer cmd = RCache.GetActiveCommandBuffer();
        if (!cmd) return;

        if (!rect)
        {
            VkRect2D full;
            full.offset        = { 0, 0 };
            full.extent        = HW.m_vkSCExtent;
            vkCmdSetScissor(cmd, 0, 1, &full);
        }
        else
        {
            VkRect2D sc;
            sc.offset.x = std::max(0, rect->x1);
            sc.offset.y = std::max(0, rect->y1);
            
            int right = std::min((int)HW.m_vkSCExtent.width, rect->x2);
            int bottom = std::min((int)HW.m_vkSCExtent.height, rect->y2);
            
            sc.extent.width = (uint32_t)std::max(0, right - sc.offset.x);
            sc.extent.height = (uint32_t)std::max(0, bottom - sc.offset.y);
            
            vkCmdSetScissor(cmd, 0, 1, &sc);
        }
    }

    virtual void GetActiveTextureResolution(Fvector2& res)
    {
        CTexture* T = RCache.get_ActiveTexture(0);
        if (T)
            res.set(float(T->get_Width()), float(T->get_Height()));
        else
            res.set(float(HW.m_vkSCExtent.width), float(HW.m_vkSCExtent.height));
    }

    virtual LPCSTR UpdateShaderName(LPCSTR /*tex_name*/, LPCSTR sh_name) { return sh_name; }

    virtual void CacheSetXformWorld(const Fmatrix& M)
    {
        RCache.set_xform_world(M);
    }

    virtual void CacheSetCullMode(CullMode mode)
    {
        if      (mode == cmNONE) RCache.set_CullMode(CULL_NONE);
        else if (mode == cmCW)   RCache.set_CullMode(CULL_CW);
        else if (mode == cmCCW)  RCache.set_CullMode(CULL_CCW);
    }

private:
    ref_geom hGeom_TL;
    ref_geom hGeom_LIT;

    ePrimitiveType PrimitiveType = ptNone;
    ePointType m_PointType = pttNone;

    u32 m_iMaxVerts = 0;
    u32 vOffset = 0;

    FVF::TL* TL_start_pv = nullptr;
    FVF::TL* TL_pv = nullptr;

    FVF::LIT* LIT_start_pv = nullptr;
    FVF::LIT* LIT_pv = nullptr;
};

extern cvkUIRender UIRenderImpl;

