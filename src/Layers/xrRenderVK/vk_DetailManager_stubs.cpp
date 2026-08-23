// vk_DetailManager_stubs.cpp
// Phase 8 — Component 5: VK stubs for CDetailManager hardware methods.
//
// DetailManager_VS.cpp is excluded from the VK build because it contains
// DX9/DX10 vertex-shader based detail rendering. However, DetailManager.cpp
// (which IS compiled) calls hw_Load, hw_Unload, and hw_Render via UseVS().
// These stubs satisfy the linker; the real implementations are deferred until
// the detail-geometry pipeline is ported to Vulkan.

#include "stdafx.h"
#include "../xrRender/DetailManager.h"

// ── hw_Load / hw_Load_Geom / hw_Load_Shaders ─────────────────────────────────
// Called from CDetailManager::Load() when UseVS() returns true.
// Under VK, UseVS() currently returns false so these are dead code paths,
// but the linker still needs a definition.

void CDetailManager::hw_Load()
{
    Msg("~ [VK] CDetailManager::hw_Load: NYI (detail VS not ported)");
}

void CDetailManager::hw_Load_Geom()
{
    // No-op stub — called by hw_Load above.
}

void CDetailManager::hw_Load_Shaders()
{
    // No-op stub — called by hw_Load above.
}

// ── hw_Unload ─────────────────────────────────────────────────────────────────
void CDetailManager::hw_Unload()
{
    Msg("~ [VK] CDetailManager::hw_Unload: NYI");
}

// ── hw_Render ─────────────────────────────────────────────────────────────────
void CDetailManager::hw_Render(light* L)
{
    // No-op stub — detail geometry not yet rendered in VK.
    (void)L;
}

// ── hw_Render_dump ────────────────────────────────────────────────────────────
// Under VK, USE_DX10/USE_DX11 are not defined so the DX9 signature applies.
// Only called internally from hw_Render (in DetailManager_VS.cpp, excluded),
// so this stub exists solely to prevent any future link issues.
void CDetailManager::hw_Render_dump(ref_constant array, u32 var_id, u32 lod_id, u32 c_base, light* L)
{
    (void)array; (void)var_id; (void)lod_id; (void)c_base; (void)L;
}

