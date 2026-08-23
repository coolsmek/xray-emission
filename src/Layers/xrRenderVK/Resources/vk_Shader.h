// SPIR-V shader module helpers
#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// Creates a VkShaderModule from raw SPIR-V bytes.
// Returns VK_NULL_HANDLE on failure (logs a warning).
VkShaderModule vk_CreateShaderModule(VkDevice device, const void* pCode, size_t sizeInBytes);

// Destroys a VkShaderModule. Safe to call with VK_NULL_HANDLE (no-op).
void vk_DestroyShaderModule(VkDevice device, VkShaderModule module);

struct D3D_SHADER_MACRO;

// Compiles HLSL source to SPIR-V using the shaderc runtime library.
// stageName : two-char stage prefix — "vs", "ps", "gs", "hs", "ds", "cs"
// entryPoint: HLSL function name, usually "main"
// shaderName: human-readable label used in log messages and as the disk-cache key
// macros    : optional null-terminated array of macro definitions
// Returns the SPIR-V word stream; empty on failure (error already logged).
std::vector<uint32_t> vk_CompileHlslToSpirv(
    const char* hlslSource,
    size_t      hlslLen,
    const char* stageName,
    const char* entryPoint,
    const char* shaderName,
    const D3D_SHADER_MACRO* macros = nullptr
);
