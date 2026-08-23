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

// 1. Establish Master Vulkan Guards & Platform Hooks
//    MUST be defined before xrEngine/stdafx.h so that every engine header
//    that includes RenderFactory.h / FactoryPtr.h / xrAPI.h sees USE_VK.
#ifndef USE_VK
#define USE_VK
#endif

#define R_R1 1
#define R_R2 2
#define R_R3 3
#define R_R4 4
#ifndef R_VK
#define R_VK 5
#define RENDER R_VK
#endif

#pragma warning(disable:4995)
#include "../../xrEngine/stdafx.h"
#pragma warning(disable:4995)
#pragma warning(default:4995)

// Vulkan — use the KHR_surface + Win32_surface WSI
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>


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
#include "vk_DebugMarkers.h"

// 7. Render-specific types and pipelines
#include "r2_types.h"
#include "rVK.h"

IC void jitter(CBlender_Compile& C)
{
	C.r_dx10Texture("jitter0", JITTER(0));
	C.r_dx10Texture("jitter1", JITTER(1));
	C.r_dx10Texture("jitter2", JITTER(2));
	C.r_dx10Texture("jitter3", JITTER(3));
	C.r_dx10Texture("jitter4", JITTER(4));
	C.r_dx10Texture("jitterMipped", r2_jitter_mipped);
	C.r_dx10Sampler("smp_jitter");
}

// ── Vulkan Debugging Macros ──────────────────────────────────────────────────
extern bool g_bVulkanDebugLog;
#define VK_DBG(fmt, ...) do { if (g_bVulkanDebugLog) Msg("* VK: " fmt, __VA_ARGS__); } while(0)
