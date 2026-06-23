// Entry point for the Vulkan rendering backend (xrRenderVK).
// Handles DLL lifecycle events, wires up the global render/factory/UI interfaces,
// and exposes SupportsVKRendering() to query hardware Vulkan support.

// minimize touching legacy shared files:
/*#include "../xrRender/dxRenderFactory.h"
#include "../xrRender/dxUIRender.h"
#include "../xrRender/dxDebugRender.h"*/

#include "stdafx.h"
#include "rVK.h"
// minimize touching legacy shared files:
#include "vkRenderFactory.h"
#include "vkUIRender.h"
#include "vkDebugRender.h"
#include "../xrRenderPC_R1/FStaticRender.h"

// Declare the concrete global instances that X-Ray's engine uses.
// Defined as externs throughout the rest of the engine code.
CRender             RImplementation;    // Implements IRender_interface
cvkRenderFactory    RenderFactoryImpl;  // Implements IRenderFactory
cvkUIRender         UIRenderImpl;       // Implements IUIRender
cvkDebugRender      DebugRenderImpl;    // Implements IDebugRender
CDUInterface        DUImpl;             // Simple math/line drawing helper (can often share shared/pure math file)

BOOL DllMainXrRenderVK(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        ::Render        = &RImplementation;
        ::RenderFactory = &RenderFactoryImpl;
        ::DU            = &DUImpl;
        UIRender        = &UIRenderImpl;
        DRender         = &DebugRenderImpl;

        xrRender_initconsole();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C" {
    __declspec(dllexport) bool SupportsVKRendering();
};

bool SupportsVKRendering()
{
    return xrRender_test_hw() ? true : false;
}
