#pragma once
#include "../../Include/xrRender/RenderFactory.h"
#include "vkRenderDeviceRender.h"
#include "vk_LensFlareRender.h"
#include "vk_WeatherRender.h"

#define RENDER_FACTORY_DECLARE(Class) \
    virtual I##Class* Create##Class() { return nullptr; } \
    virtual void Destroy##Class(I##Class *pObject) {}

class cvkRenderFactory : public IRenderFactory
{
public:
    // RenderDeviceRender — must return a real object; all others are NYI stubs.
    virtual IRenderDeviceRender* CreateRenderDeviceRender()            { return &vkDeviceRenderImpl; }
    virtual void DestroyRenderDeviceRender(IRenderDeviceRender* p);

#ifndef _EDITOR
    RENDER_FACTORY_DECLARE(UISequenceVideoItem)
    virtual IUIShader* CreateUIShader();
    virtual void       DestroyUIShader(IUIShader* pObject);
    RENDER_FACTORY_DECLARE(StatGraphRender)
    RENDER_FACTORY_DECLARE(ConsoleRender)
    // RenderDeviceRender deliberately omitted — explicitly overridden above
#    ifdef DEBUG
        RENDER_FACTORY_DECLARE(ObjectSpaceRender)
#    endif // DEBUG
    virtual IApplicationRender* CreateApplicationRender();
    virtual void                DestroyApplicationRender(IApplicationRender* pObject);
    virtual IWallMarkArray* CreateWallMarkArray();
    virtual void          DestroyWallMarkArray(IWallMarkArray* pObject);
    RENDER_FACTORY_DECLARE(StatsRender)
#endif // _EDITOR

#ifndef _EDITOR
    virtual IFlareRender*             CreateFlareRender()              { return xr_new<vkFlareRender>(); }
    virtual void                      DestroyFlareRender(IFlareRender* p)  { xr_delete(p); }
    virtual IThunderboltRender*       CreateThunderboltRender()        { return xr_new<vkThunderboltRender>(); }
    virtual void                      DestroyThunderboltRender(IThunderboltRender* p) { xr_delete(p); }
    virtual IThunderboltDescRender*   CreateThunderboltDescRender()    { return xr_new<vkThunderboltDescRender>(); }
    virtual void                      DestroyThunderboltDescRender(IThunderboltDescRender* p) { xr_delete(p); }
    virtual IRainRender*              CreateRainRender()               { return xr_new<vkRainRender>(); }
    virtual void                      DestroyRainRender(IRainRender* p) { xr_delete(p); }
    virtual ILensFlareRender*         CreateLensFlareRender()          { return xr_new<vkLensFlareRender>(); }
    virtual void                      DestroyLensFlareRender(ILensFlareRender* p) { xr_delete(p); }
    virtual IImGuiRender* CreateImGuiRender();
    virtual void          DestroyImGuiRender(IImGuiRender* pObject);
    virtual IEnvironmentRender* CreateEnvironmentRender();
    virtual void          DestroyEnvironmentRender(IEnvironmentRender* pObject);
    virtual IEnvDescriptorMixerRender* CreateEnvDescriptorMixerRender();
    virtual void                      DestroyEnvDescriptorMixerRender(IEnvDescriptorMixerRender* p);
    virtual IEnvDescriptorRender*     CreateEnvDescriptorRender();
    virtual void                      DestroyEnvDescriptorRender(IEnvDescriptorRender* p);
#endif
    // FontRender — must return a real (non-null) object; engine calls Initialize() immediately.
    virtual IFontRender* CreateFontRender();
    virtual void         DestroyFontRender(IFontRender* pObject);
};

extern cvkRenderFactory RenderFactoryImpl;
