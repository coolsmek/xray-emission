// vkRenderFactory.cpp
// Phase 8 — Component 1: Global instance definitions for Vulkan render factory, UI render, debug render,
// draw utilities, and the rain-particle counter that normally lives in the excluded dxRainRender.cpp.
//
// All factory method bodies are inline stubs in vkRenderFactory.h (RENDER_FACTORY_DECLARE macro).
// All UIRender/DebugRender method bodies are inline stubs in vk_UIRender.h / vkDebugRender.h.
// This .cpp exists purely to satisfy the linker's need for one ODR definition of each global.

#include "stdafx.h"
#include "vkRenderFactory.h"
#include "vk_UIRender.h"
#include "vkDebugRender.h"
#include "../xrRender/D3DUtils.h"   // CDrawUtilities class definition
#include "rVK.h"

void cvkRenderFactory::DestroyRenderDeviceRender(IRenderDeviceRender* /*p*/)
{
    HW.DestroyDevice();
}

// ── Factory global ─────────────────────────────────────────────────────────────
// Engine accesses this through the IRenderFactory* RenderFactory pointer, which
// xrRenderVK.cpp wires up on DLL_PROCESS_ATTACH.
cvkRenderFactory RenderFactoryImpl;

#include "vk_FontRender.h"
#include "vk_ImGuiRender.h"
#include "vk_EnvironmentRender.h"
#include "vk_WallMarkArray.h"
#include "vk_UIShader.h"
#include "../xrRender/dxApplicationRender.h"

IUIShader* cvkRenderFactory::CreateUIShader() { return xr_new<vkUIShader>(); }
void cvkRenderFactory::DestroyUIShader(IUIShader* pObject) { xr_delete((vkUIShader*&)pObject); }

IWallMarkArray* cvkRenderFactory::CreateWallMarkArray() { return xr_new<vkWallMarkArray>(); }
void cvkRenderFactory::DestroyWallMarkArray(IWallMarkArray* pObject) { xr_delete((vkWallMarkArray*&)pObject); }

IImGuiRender* cvkRenderFactory::CreateImGuiRender() { return xr_new<vkImGuiRender>(); }
void cvkRenderFactory::DestroyImGuiRender(IImGuiRender* pObject) { xr_delete((vkImGuiRender*&)pObject); }

#include "vk_EnvDescriptorRender.h"
IEnvironmentRender* cvkRenderFactory::CreateEnvironmentRender() { return xr_new<vkEnvironmentRender>(); }
void cvkRenderFactory::DestroyEnvironmentRender(IEnvironmentRender* pObject) { xr_delete((vkEnvironmentRender*&)pObject); }

IEnvDescriptorMixerRender* cvkRenderFactory::CreateEnvDescriptorMixerRender() { return xr_new<vkEnvDescriptorMixerRender>(); }
void cvkRenderFactory::DestroyEnvDescriptorMixerRender(IEnvDescriptorMixerRender* p) { xr_delete(p); }
IEnvDescriptorRender* cvkRenderFactory::CreateEnvDescriptorRender() { return xr_new<vkEnvDescriptorRender>(); }
void cvkRenderFactory::DestroyEnvDescriptorRender(IEnvDescriptorRender* p) { xr_delete(p); }

IFontRender* cvkRenderFactory::CreateFontRender() { return xr_new<vkFontRender>(); }
void cvkRenderFactory::DestroyFontRender(IFontRender* pObject) { xr_delete((vkFontRender*&)pObject); }

IApplicationRender* cvkRenderFactory::CreateApplicationRender() { return xr_new<dxApplicationRender>(); }
void cvkRenderFactory::DestroyApplicationRender(IApplicationRender* pObject) { xr_delete((dxApplicationRender*&)pObject); }
// ── UI / Debug render globals ──────────────────────────────────────────────────
// xrRenderVK.cpp DLL_PROCESS_ATTACH sets:
//   UIRender = &UIRenderImpl;
//   DRender  = &DebugRenderImpl;
cvkUIRender    UIRenderImpl;
cvkDebugRender DebugRenderImpl;

// ── Draw utilities global ──────────────────────────────────────────────────────
// D3DUtils.cpp is excluded from the VK build (it includes d3dx9.h).
// CDrawUtilities has a trivial default constructor (zeroes all members) so it is
// safe to construct before the device exists.  Methods that actually draw are
// never called until after device creation.
CDrawUtilities DUImpl;

// ── Rain particle counter ──────────────────────────────────────────────────────
// Normally defined in dxRainRender.cpp which is excluded from the VK build.
// xrEngine/Rain.cpp declares it as 'extern xr_atomic_u32 current_items;' and uses
// it for rain-drop bookkeeping independent of any renderer.
xr_atomic_u32 current_items;

// The IRenderFactory* typed global that xrEngine (compiled with USE_VK) resolves.
// Set to &RenderFactoryImpl by CRender::OnDeviceCreate() in xrRenderVK.cpp.
#include "../../Include/xrAPI/xrAPI.h"
XRAPI_API IRenderFactory* RenderFactory = nullptr;

