#pragma once

#pragma warning(disable:4995)
#include "../../xrEngine/stdafx.h"
#pragma warning(disable:4995)
#pragma warning(default:4995)

// Vulkan — use the KHR_surface + Win32_surface WSI
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "../xrRender/xrD3DDefs.h"    //keep type aliases to enxtend for Vulkan

#define R_R1 1
#define R_R2 2
#define R_R3 3
#define R_R4 4
#define R_VK 5
#define RENDER R_VK

#define USE_VK  // the master guard (mirrors USE_DX11)

#include "../../xrParticles/psystem.h"
#include "../xrRender/HW.h"
#include "../xrRender/Shader.h"
#include "../xrRender/R_Backend.h"
#include "../xrRender/R_Backend_Runtime.h"
#include "../xrRender/resourcemanager.h"
#include "../../xrEngine/vis_common.h"
#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_level.h"
#include "../xrRender/blenders/blender.h"
#include "../xrRender/blenders/blender_clsid.h"
#include "../xrRender/xrRender_console.h"
// #include "vk_render.h" // TODO: add when CRender is implemented
#include "../../xrCore/profiler.h"


