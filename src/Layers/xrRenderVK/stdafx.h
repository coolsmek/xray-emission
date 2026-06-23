/*
The ID3DState::Apply() Trick:
By explicitly defining struct ID3DState { void Apply() {} }; inside the USE_VK block right here, you satisfy
the compiler for line 150 of R_Backend_Runtime.h. When it reads TargetState->Apply(), it evaluates it as a
standard inline no-op without you needing to edit a single byte of that file.

The typedef struct Injection Layer:
When the compiler opens ../xrRender/HW.h, it expects variables like ID3DDevice* pDevice;. Instead of including
massive DirectX SDK structures, we forward-declare them as basic opaque structs. The compiler accepts the pointer
notation (*) cleanly because it only needs to know that a pointer takes up 8 bytes of space, allowing compilation to proceed.

Cleaner Local Iterations:
With this file configured, any new Vulkan .cpp file you add to xrRenderVK will immediately inherit full access to
the vulkan.h headers, engine rendering context types, and core variables automatically just by keeping
#include "stdafx.h" at its absolute top!
*/

#pragma once

#pragma warning(disable:4995)
#include "../../xrEngine/stdafx.h"
#pragma warning(disable:4995)
#pragma warning(default:4995)

// 1. Establish Master Vulkan Guards & Platform Hooks
#ifndef USE_VK
#define USE_VK  // The master preprocessor flag for the entire compilation unit
#endif

#define R_R1 1
#define R_R2 2
#define R_R3 3
#define R_R4 4
#ifndef R_VK
#define R_VK 5
#define RENDER R_VK
#endif

// Vulkan — use the KHR_surface + Win32_surface WSI
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

// 2. STUB INJECTION LAYER (Bypasses DirectX structural dependencies safely)
// We define these types BEFORE xrRender headers are read so they don't break.
#ifdef USE_VK
    // Provide a light implementation for ID3DState so R_Backend_Runtime.h line 150 compiles safely
    struct ID3DState {
        void Apply() { /* Safe Opaque Vulkan No-Op */ }
    };

    // Forward-declare minimal opaque types for fields inside HW.h / R_Backend.h
    // This allows pointers to remain intact without needing the DirectX SDK headers.
    typedef struct ID3D11Device             ID3DDevice;
    typedef struct ID3D11DeviceContext      ID3DDeviceContext;
    typedef struct IDXGISwapChain           IDXGISwapChain;
    typedef struct ID3D11RenderTargetView   ID3DRenderTargetView;
    typedef struct ID3D11DepthStencilView   ID3DDepthStencilView;
    typedef struct ID3D11BaseShader         ID3DBaseShader;
#endif

// 3. Engine Base Layout Definitions
#include "../xrRender/xrD3DDefs.h"
#include "../../xrParticles/psystem.h"

// 4. Legacy Shared Render Headers (Safely enclosed via our stub injection layer)
#include "../xrRender/HW.h"
#include "../xrRender/Shader.h"
#include "../xrRender/R_Backend.h"
#include "../xrRender/R_Backend_Runtime.h"
#include "../xrRender/resourcemanager.h"

// 5. Core Engine Systems
#include "../../xrEngine/vis_common.h"
#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_level.h"
#include "../xrRender/blenders/blender.h"
#include "../xrRender/blenders/blender_clsid.h"
#include "../xrRender/xrRender_console.h"
#include "../../xrCore/profiler.h"

// 6. Native Vulkan Utilities Interface
// Include any foundational Vulkan helpers that all your future .cpp files will need.
// #include "vk_HW.h" // Your device initializer declaration

// #include "vk_render.h" // TODO: add when CRender is implemented


