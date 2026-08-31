#include "stdafx.h"
#include "vk_UIRender.h"

void cvkUIRender::CreateUIGeom()
{
    hGeom_TL.create(FVF::F_TL, RCache.Vertex.Buffer(), 0);
    hGeom_LIT.create(FVF::F_LIT, RCache.Vertex.Buffer(), 0);
}

void cvkUIRender::DestroyUIGeom()
{
    hGeom_TL = NULL;
    hGeom_LIT = NULL;
}

void cvkUIRender::PushPoint(float x, float y, float z, u32 C, float u, float v)
{
    switch (m_PointType)
    {
    case pttLIT:
        LIT_pv->set(x, y, z, C, u, v);
        ++LIT_pv;
        break;
    case pttTL:
        TL_pv->set(x, y, C, u, v);
        ++TL_pv;
        break;
    }
}

void cvkUIRender::StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType)
{
    VERIFY(PrimitiveType == ptNone);
    VERIFY(m_PointType == pttNone);

    m_iMaxVerts = iMaxVerts;
    PrimitiveType = primType;
    m_PointType = pointType;

    switch (m_PointType)
    {
    case pttLIT:
        LIT_start_pv = (FVF::LIT*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_LIT.stride(), vOffset);
        LIT_pv = LIT_start_pv;
        break;
    case pttTL:
        TL_start_pv = (FVF::TL*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_TL.stride(), vOffset);
        TL_pv = TL_start_pv;
        break;
    }
}

void cvkUIRender::FlushPrimitive()
{
    u32 primCount = 0;
    _D3DPRIMITIVETYPE d3dPrimType = D3DPT_FORCE_DWORD;
    std::ptrdiff_t p_cnt = 0;

    switch (m_PointType)
    {
    case pttLIT:
        if (!hGeom_LIT || !hGeom_LIT->vb)
            CreateUIGeom();
        p_cnt = LIT_pv - LIT_start_pv;
        VERIFY(u32(p_cnt) <= m_iMaxVerts);
        RCache.Vertex.Unlock(u32(p_cnt), hGeom_LIT.stride());
        RCache.set_Geometry(hGeom_LIT);
        break;
    case pttTL:
        if (!hGeom_TL || !hGeom_TL->vb)
            CreateUIGeom();
        p_cnt = TL_pv - TL_start_pv;
        VERIFY(u32(p_cnt) <= m_iMaxVerts);
        RCache.Vertex.Unlock(u32(p_cnt), hGeom_TL.stride());
        RCache.set_Geometry(hGeom_TL);
        break;
    default:
        NODEFAULT;
    }

    switch (PrimitiveType)
    {
    case ptTriStrip:
        primCount = (u32)(p_cnt - 2);
        d3dPrimType = D3DPT_TRIANGLESTRIP;
        break;
    case ptTriList:
        primCount = (u32)(p_cnt / 3);
        d3dPrimType = D3DPT_TRIANGLELIST;
        break;
    case ptLineStrip:
        primCount = (u32)(p_cnt - 1);
        d3dPrimType = D3DPT_LINESTRIP;
        break;
    case ptLineList:
        primCount = (u32)(p_cnt / 2);
        d3dPrimType = D3DPT_LINELIST;
        break;
    default:
        NODEFAULT;
    }

    if (primCount > 0)
    {
        // UI 2D primitives are alpha-blended; shader-cache UI blenders don't record
        // blend into SimulatorStates in the VK path. Force it (after set_Shader so
        // set_States can't override it).
        RCache.set_Blend(TRUE);
        RCache.Render(d3dPrimType, vOffset, primCount);
    }

    PrimitiveType = ptNone;
    m_PointType = pttNone;
}
