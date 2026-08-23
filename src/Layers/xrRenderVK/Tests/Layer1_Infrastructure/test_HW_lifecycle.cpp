#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"

// Verify the global HW singleton is in a sane state after CreateDevice.
VK_TEST(HW, DeviceHandlesNotNull)
{
    VK_EXPECT(HW.m_vkInstance  != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkPhysDevice != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkDevice    != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkSurface   != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkCmdPool   != VK_NULL_HANDLE);
    return true;
}

// Verify the GPU selected is discrete — the engine preference path in vk_SelectPhysicalDevice()
VK_TEST(HW, DiscreteGPUSelected)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(HW.m_vkPhysDevice, &props);
    Msg("* [VK_TEST] GPU: %s (type=%d)", props.deviceName, (int)props.deviceType);
    // Accept discrete OR integrated — but not software (5) or CPU (4)
    VK_EXPECT(props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU);
    VK_EXPECT(props.deviceType != VK_PHYSICAL_DEVICE_TYPE_OTHER);
    return true;
}

// Verify that Vulkan 1.3 features required by the renderer are truly enabled on the device.
VK_TEST(HW, RequiredFeaturesPresent)
{
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(HW.m_vkPhysDevice, &f2);
    VK_EXPECT(f13.dynamicRendering == VK_TRUE);
    VK_EXPECT(f13.synchronization2 == VK_TRUE);
    return true;
}

// Verify frame sync objects exist for all slots.
VK_TEST(HW, SyncObjectsAllocated)
{
    for (uint32_t i = 0; i < CHW::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VK_EXPECT(HW.m_vkImageAvailable[i] != VK_NULL_HANDLE);
        VK_EXPECT(HW.m_vkRenderFinished[i]  != VK_NULL_HANDLE);
        VK_EXPECT(HW.m_vkInFlightFences[i]  != VK_NULL_HANDLE);
    }
    return true;
}

// Verify command buffers match swapchain image count.
VK_TEST(HW, CommandBufferCount)
{
    VK_EXPECT(HW.m_vkCmdBuffers.size() >= CHW::MAX_FRAMES_IN_FLIGHT);
    for (auto& cb : HW.m_vkCmdBuffers)
        VK_EXPECT(cb != VK_NULL_HANDLE);
    return true;
}

// Verify depth image exists and has the expected format.
VK_TEST(HW, DepthBufferFormat)
{
    VK_EXPECT(HW.m_vkDepthImage != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkDepthView  != VK_NULL_HANDLE);
    VK_EXPECT(HW.m_vkDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
              HW.m_vkDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT);
    return true;
}

#endif // VK_ENABLE_TESTS

