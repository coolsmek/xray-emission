#pragma once
#include "../../Include/xrRender/LensFlareRender.h"

// IFlareRender — stub for per-flare element shader slots (gradient, source, flares[i])
class vkFlareRender : public IFlareRender {
public:
    virtual ~vkFlareRender() override = default;
    virtual void Copy(IFlareRender& _in)                       override {}
    virtual void CreateShader(LPCSTR sh, LPCSTR tex)           override {}
    virtual void DestroyShader()                               override {}
};

// ILensFlareRender — stub for the top-level lens flare render object
class vkLensFlareRender : public ILensFlareRender {
public:
    virtual ~vkLensFlareRender() override = default;
    virtual void Copy(ILensFlareRender& _in)                                  override {}
    virtual void Render(CLensFlare& owner, BOOL bSun, BOOL bFlares, BOOL bGradient) override {}
    virtual void OnDeviceCreate()                                              override {}
    virtual void OnDeviceDestroy()                                             override {}
};
