// vk_vma_impl.cpp
// VMA (Vulkan Memory Allocator) implementation unit.
//
// IMPORTANT: This file must NOT use the precompiled header.
// VMA_IMPLEMENTATION must be defined *before* vk_mem_alloc.h is first seen
// by the compiler. MSVC PCH injection happens before any code in a /Yu file,
// so placing #define VMA_IMPLEMENTATION inside a PCH-using TU is unreliable.
// This dedicated file compiles without PCH, guaranteeing correct ordering.

// Pull in the Vulkan platform header so VMA has VkInstance etc.
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

// Configure VMA to use static Vulkan function pointers from vulkan-1.lib.
// This avoids any dynamic function-pointer loading overhead.
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#endif

// Compile the full VMA implementation into this translation unit.
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

