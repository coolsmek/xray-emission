// vk_dxRenderFactory_compat.cpp
// Compiled WITHOUT the VK precompiled header so vkRenderFactory.h (which also
// declares RenderFactoryImpl) is never visible here.  Only dxRenderFactory.h is
// included, which brings just the interface forward-declarations needed to define
// the stub methods and the dxRenderFactory RenderFactoryImpl object.
//
// xrGame.lib (and any other lib compiled without USE_VK) references:
//   dxRenderFactory::Create*/Destroy*   — satisfied by the stubs below
//   dxRenderFactory RenderFactoryImpl   — satisfied by the global object below

#include "..\xrRender\dxRenderFactory.h"

// Satisfies ?RenderFactoryImpl@@3VdxRenderFactory@@A
// (different mangled name from cvkRenderFactory RenderFactoryImpl — no ODR conflict)
dxRenderFactory RenderFactoryImpl;

// ── Method stubs ────────────────────────────────────────────────────────────────
#ifndef _EDITOR
IUISequenceVideoItem*      dxRenderFactory::CreateUISequenceVideoItem()                          { return nullptr; }
void                       dxRenderFactory::DestroyUISequenceVideoItem(IUISequenceVideoItem*)    {}
IUIShader*                 dxRenderFactory::CreateUIShader()                                     { return nullptr; }
void                       dxRenderFactory::DestroyUIShader(IUIShader*)                          {}
IStatGraphRender*          dxRenderFactory::CreateStatGraphRender()                              { return nullptr; }
void                       dxRenderFactory::DestroyStatGraphRender(IStatGraphRender*)            {}
IConsoleRender*            dxRenderFactory::CreateConsoleRender()                                { return nullptr; }
void                       dxRenderFactory::DestroyConsoleRender(IConsoleRender*)                {}
IRenderDeviceRender*       dxRenderFactory::CreateRenderDeviceRender()                           { return nullptr; }
void                       dxRenderFactory::DestroyRenderDeviceRender(IRenderDeviceRender*)      {}
IApplicationRender*        dxRenderFactory::CreateApplicationRender()                            { return nullptr; }
void                       dxRenderFactory::DestroyApplicationRender(IApplicationRender*)        {}
IWallMarkArray*            dxRenderFactory::CreateWallMarkArray()                                { return nullptr; }
void                       dxRenderFactory::DestroyWallMarkArray(IWallMarkArray*)                {}
IStatsRender*              dxRenderFactory::CreateStatsRender()                                  { return nullptr; }
void                       dxRenderFactory::DestroyStatsRender(IStatsRender*)                    {}
IFlareRender*              dxRenderFactory::CreateFlareRender()                                  { return nullptr; }
void                       dxRenderFactory::DestroyFlareRender(IFlareRender*)                    {}
IThunderboltRender*        dxRenderFactory::CreateThunderboltRender()                            { return nullptr; }
void                       dxRenderFactory::DestroyThunderboltRender(IThunderboltRender*)        {}
IThunderboltDescRender*    dxRenderFactory::CreateThunderboltDescRender()                        { return nullptr; }
void                       dxRenderFactory::DestroyThunderboltDescRender(IThunderboltDescRender*){}
IRainRender*               dxRenderFactory::CreateRainRender()                                   { return nullptr; }
void                       dxRenderFactory::DestroyRainRender(IRainRender*)                      {}
ILensFlareRender*          dxRenderFactory::CreateLensFlareRender()                              { return nullptr; }
void                       dxRenderFactory::DestroyLensFlareRender(ILensFlareRender*)            {}
IImGuiRender*              dxRenderFactory::CreateImGuiRender()                                  { return nullptr; }
void                       dxRenderFactory::DestroyImGuiRender(IImGuiRender*)                    {}
IEnvironmentRender*        dxRenderFactory::CreateEnvironmentRender()                            { return nullptr; }
void                       dxRenderFactory::DestroyEnvironmentRender(IEnvironmentRender*)        {}
IEnvDescriptorMixerRender* dxRenderFactory::CreateEnvDescriptorMixerRender()                    { return nullptr; }
void                       dxRenderFactory::DestroyEnvDescriptorMixerRender(IEnvDescriptorMixerRender*){}
IEnvDescriptorRender*      dxRenderFactory::CreateEnvDescriptorRender()                         { return nullptr; }
void                       dxRenderFactory::DestroyEnvDescriptorRender(IEnvDescriptorRender*)   {}
#endif // _EDITOR
IFontRender*               dxRenderFactory::CreateFontRender()                                   { return nullptr; }
void                       dxRenderFactory::DestroyFontRender(IFontRender*)                      {}
