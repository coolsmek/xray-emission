#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
#include "../../Resources/vk_ShaderReflection.h"

// Empty binding list → CreatePipelineLayout must produce a valid empty layout.
VK_TEST(PipelineLayout, EmptyBindingsStillValid)
{
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = vk_ShaderReflection::CreatePipelineLayout(
        HW.m_vkDevice, {}, {}, setLayouts);
    VK_EXPECT(layout != VK_NULL_HANDLE);
    VK_EXPECT(setLayouts.size() == 1);  // empty layout sentinel from the implementation
    for (auto sl : setLayouts)
        vkDestroyDescriptorSetLayout(HW.m_vkDevice, sl, nullptr);
    vkDestroyPipelineLayout(HW.m_vkDevice, layout, nullptr);
    return true;
}

// Simulate a UBO at set=0, binding=0 in the VS.
VK_TEST(PipelineLayout, SingleUBOBinding)
{
    vk_ShaderReflectionBinding ubo{ 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = vk_ShaderReflection::CreatePipelineLayout(
        HW.m_vkDevice, {ubo}, {}, setLayouts);
    VK_EXPECT(layout != VK_NULL_HANDLE);
    VK_EXPECT(setLayouts.size() == 1);
    for (auto sl : setLayouts)
        vkDestroyDescriptorSetLayout(HW.m_vkDevice, sl, nullptr);
    vkDestroyPipelineLayout(HW.m_vkDevice, layout, nullptr);
    return true;
}

// Simulate UBO in VS (set 0) + COMBINED_IMAGE_SAMPLER in PS (set 1).
// Should produce 2 descriptor set layouts.
VK_TEST(PipelineLayout, TwoSetLayout_UBOPlusSampler)
{
    vk_ShaderReflectionBinding ubo  { 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    vk_ShaderReflectionBinding samp { 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = vk_ShaderReflection::CreatePipelineLayout(
        HW.m_vkDevice, {ubo}, {samp}, setLayouts);
    VK_EXPECT(layout != VK_NULL_HANDLE);
    VK_EXPECT(setLayouts.size() == 2);
    for (auto sl : setLayouts)
        vkDestroyDescriptorSetLayout(HW.m_vkDevice, sl, nullptr);
    vkDestroyPipelineLayout(HW.m_vkDevice, layout, nullptr);
    return true;
}

// Same binding appears in both VS and PS: stageFlags must be ORed together.
// We verify this indirectly: the layout must still be valid (Validation layer catches it).
VK_TEST(PipelineLayout, SharedBindingMergesStages)
{
    vk_ShaderReflectionBinding b { 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout layout = vk_ShaderReflection::CreatePipelineLayout(
        HW.m_vkDevice, {b}, {b}, setLayouts);
    VK_EXPECT(layout != VK_NULL_HANDLE);
    for (auto sl : setLayouts)
        vkDestroyDescriptorSetLayout(HW.m_vkDevice, sl, nullptr);
    vkDestroyPipelineLayout(HW.m_vkDevice, layout, nullptr);
    return true;
}

#endif // VK_ENABLE_TESTS
