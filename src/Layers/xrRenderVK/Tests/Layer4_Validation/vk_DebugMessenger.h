#pragma once

#include <vulkan/vulkan.h>

// Call once after vkCreateInstance in CHW::CreateD3D().
// The messenger is destroyed in CHW::DestroyD3D().
VkDebugUtilsMessengerEXT vk_CreateDebugMessenger(VkInstance instance);
void vk_DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger);

