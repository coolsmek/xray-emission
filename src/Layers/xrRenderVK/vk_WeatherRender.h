#pragma once
#include "../../Include/xrRender/RainRender.h"
#include "../../Include/xrRender/ThunderboltRender.h"
#include "../../Include/xrRender/ThunderboltDescRender.h"

class vkRainRender : public IRainRender {
    Fsphere m_bounds{};
public:
    virtual ~vkRainRender() override = default;
    virtual void Copy(IRainRender& _in)          override {}
    virtual void Render(CEffect_Rain& owner)     override {}
    virtual const Fsphere& GetDropBounds() const override { return m_bounds; }
};

class vkThunderboltRender : public IThunderboltRender {
public:
    virtual ~vkThunderboltRender() override = default;
    virtual void Copy(IThunderboltRender& _in)       override {}
    virtual void Render(CEffect_Thunderbolt& owner)  override {}
};

class vkThunderboltDescRender : public IThunderboltDescRender {
public:
    virtual ~vkThunderboltDescRender() override = default;
    virtual void Copy(IThunderboltDescRender& _in)  override {}
    virtual void CreateModel(LPCSTR m_name)         override {}
    virtual void DestroyModel()                     override {}
};
