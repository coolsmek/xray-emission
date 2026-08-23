#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct vk_ShaderReflectionBinding
{
    uint32_t set;
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    std::string name;
    uint32_t engineSlot;
    bool isVS;
};

struct vk_ShaderReflectionInput
{
    uint32_t location;
    std::string name;
};

class R_constant_table;

class vk_ShaderReflection
{
public:
    // Parses a SPIR-V byte code stream to find all Descriptor bindings.
    // If 'constants' is provided, also extracts Uniform Buffer members (like $Global)
    // and populates the R_constant_table (equivalent to ID3D11ShaderReflection parsing).
    static std::vector<vk_ShaderReflectionBinding> ReflectSPIRV(
        const uint32_t* pCode, size_t sizeInBytes,
        R_constant_table* constants = nullptr, uint32_t destination = 0);

    // Parses a SPIR-V byte code stream to find all Vertex Input attributes
    static std::vector<vk_ShaderReflectionInput> ReflectInputs(const uint32_t* pCode, size_t sizeInBytes);

    // Creates a pipeline layout merging bindings from multiple shader stages.
    // outSetLayouts is populated with every VkDescriptorSetLayout created (in set-index order).
    // The caller is responsible for destroying them with vkDestroyDescriptorSetLayout when done.
    static VkPipelineLayout CreatePipelineLayout(
        VkDevice device,
        const std::vector<vk_ShaderReflectionBinding>& vsBindings,
        const std::vector<vk_ShaderReflectionBinding>& psBindings,
        std::vector<VkDescriptorSetLayout>& outSetLayouts,
        const std::vector<vk_ShaderReflectionBinding>& gsBindings = {},
        const std::vector<vk_ShaderReflectionBinding>& csBindings = {});
};
