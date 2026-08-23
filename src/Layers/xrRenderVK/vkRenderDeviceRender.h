#pragma once
#include "../../Include/xrRender/RenderDeviceRender.h"

class CResourceManager;

// vkRenderDeviceRender — Vulkan implementation of IRenderDeviceRender.
//
// Device.m_pRender is set to &vkDeviceRenderImpl by
// cvkRenderFactory::CreateRenderDeviceRender().  Every virtual call the engine
// makes through Device.m_pRender is routed here instead of the DX-specific
// dxRenderDeviceRender.
//
// LAYOUT NOTE: Resources must remain the FIRST non-vtable data member so that
// the dxRenderDeviceRender::Instance().Resources cast used by the DEV macro
// (dxRenderDeviceRender.h:6) still resolves correctly to our CResourceManager*.
// Both classes derive from IRenderDeviceRender (same vtable prefix), then have
// CResourceManager* as the first data field — identical relative offset.

class vkRenderDeviceRender : public IRenderDeviceRender
{
public:
    CResourceManager* Resources = nullptr;   // ← MUST stay first data member

    vkRenderDeviceRender() = default;

    void Copy(IRenderDeviceRender& _in) override {}

    // ── Gamma — no hardware gamma ramp in Vulkan; display calibration is OS/WSI ──
    void setGamma(float)      override {}
    void setBrightness(float) override {}
    void setContrast(float)   override {}
    void updateGamma()        override {}

    // ── Device lifetime ───────────────────────────────────────────────────────────
    // Create: calls HW.CreateDevice(hWnd), retrieves swapchain dimensions, allocates
    // the shared CResourceManager (shader/texture/RT tables).
    void Create(HWND hWnd, u32& dwWidth, u32& dwHeight,
                float& fWidth_2, float& fHeight_2, bool move_window) override;

    // OnDeviceCreate: initialises RCache, loads shaders.xr via ResourceManager,
    // calls CRender::create() and Device.Statistic->OnDeviceCreate().
    void OnDeviceCreate(LPCSTR shName) override;

    // Tool-mode boot (XrayModelViewer): creates Vulkan device without swapchain.
    // Spherical receives the VkInstance/VkDevice via externalVulkanContext and
    // creates the surface + swapchain itself.  Dimensions are placeholders updated
    // from Spherical::GetSwapchainExtent() before OnDeviceCreate is called.
    void CreateForTool(HWND hWnd, u32& dwWidth, u32& dwHeight,
                       float& fWidth_2, float& fHeight_2);

    void OnDeviceDestroy(BOOL bKeepTextures) override;
    void ValidateHW()  override {}
    void DestroyHW()   override;
    void Reset(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) override;
    void SetupStates() override {}
    void SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF) override;

    // ── Per-frame hooks — managed by vkRenderDeviceRender ──────────
    void Begin()       override;
    void Clear()       override;
    void End()         override;
    void ClearTarget() override {}

    // ── Overdraw visualiser — not supported under VK ─────────────────────────────
    void overdrawBegin() override {}
    void overdrawEnd()   override {}

    // ── Resource management — delegate to CResourceManager ───────────────────────
    void DeferredLoad(BOOL E)                               override;
    void ResourcesDeferredUpload()                          override;
    void ResourcesDeferredUnload()                          override;
    void ResourcesGetMemoryUsage(u32& m_base, u32& c_base,
                                 u32& m_lmaps, u32& c_lmaps) override;
    void ResourcesDestroyNecessaryTextures()                override;
    void ResourcesStoreNecessaryTextures()                  override;
    void ResourcesDumpMemoryUsage()                         override;
    void ResourcesPrefetchCreateTexture(LPCSTR name)        override;

    // ── Device state queries ──────────────────────────────────────────────────────
    DeviceState GetDeviceState()          override { return dsOK; }
    BOOL        GetForceGPU_REF()         override { return FALSE; }
    u32         GetCacheStatPolys()       override { return 0; }
    bool        HWSupportsShaderYUV2RGB() override { return false; }

    // ── Transform cache ───────────────────────────────────────────────────────────
    void SetCacheXform(Fmatrix& mView, Fmatrix& mProject)      override;
    void SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject) override;
    void OnAssetsChanged() override;
};

extern vkRenderDeviceRender vkDeviceRenderImpl;

