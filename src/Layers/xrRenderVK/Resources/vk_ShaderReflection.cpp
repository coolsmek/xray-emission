#include "stdafx.h"
#include "vk_ShaderReflection.h"
#include <map>
#include <algorithm>

#include "../../../3rd party/spirv-reflect/spirv_reflect.h"
#include "../../xrRender/r_constants_cache.h"
#include "../../xrRender/r_constants.h"

std::vector<vk_ShaderReflectionInput> vk_ShaderReflection::ReflectInputs(const uint32_t* pCode, size_t sizeInBytes)
{
    std::vector<vk_ShaderReflectionInput> inputs;
    if (!pCode || sizeInBytes < 20) return inputs;

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(sizeInBytes, pCode, &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) return inputs;

    uint32_t count = 0;
    spvReflectEnumerateInputVariables(&module, &count, nullptr);
    if (count > 0)
    {
        std::vector<SpvReflectInterfaceVariable*> input_vars(count);
        spvReflectEnumerateInputVariables(&module, &count, input_vars.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            if (input_vars[i]->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                continue;

            vk_ShaderReflectionInput in;
            in.location = input_vars[i]->location;
            in.name = input_vars[i]->name ? input_vars[i]->name : "";
            inputs.push_back(in);
        }
    }

    spvReflectDestroyShaderModule(&module);
    return inputs;
}

std::vector<vk_ShaderReflectionBinding> vk_ShaderReflection::ReflectSPIRV(const uint32_t* pCode, size_t sizeInBytes, R_constant_table* constants, uint32_t destination)
{
    std::vector<vk_ShaderReflectionBinding> bindings;
    if (!pCode || sizeInBytes < 20) return bindings;

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(sizeInBytes, pCode, &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) return bindings;

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    if (count > 0)
    {
        std::vector<SpvReflectDescriptorBinding*> desc_bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, desc_bindings.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            SpvReflectDescriptorBinding* b = desc_bindings[i];

            vk_ShaderReflectionBinding out_b;
            out_b.set = b->set;
            out_b.binding = b->binding;
            out_b.count = b->count;
            out_b.type = (VkDescriptorType)b->descriptor_type;
            const char* bn = b->name ? b->name
                           : (b->type_description ? b->type_description->type_name : nullptr);
            out_b.name = bn ? bn : "";

            // [Phase 11] Previously, we mapped this to VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC.
            // However, VK_KHR_push_descriptor does NOT support dynamic uniform buffers.
            // By leaving it as VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, we bake the dynamic offset
            // directly into the pushed VkDescriptorBufferInfo.offset per draw call.
            // [Phase 13.C] We have removed Push Descriptors. Re-enabling VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
            // for Set 0 to allow efficient descriptor caching.
            if (out_b.set == 0 && out_b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                out_b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

            uint32_t uiBufferIndex = b->binding;
            bool isVS = false;

            if (destination & RC_dest_pixel) {
                if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE && b->binding >= 56 && b->binding < 72)
                    uiBufferIndex = b->binding - 56;
                else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER && b->binding >= 72 && b->binding < 88)
                    uiBufferIndex = b->binding - 72;
            } else if (destination & RC_dest_vertex) {
                isVS = true;
                // VS textures MUST be offset by CTexture::rstVertex so CBackend::set_Textures
                // routes them to textures_vs[] instead of textures_ps[].
                if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE && b->binding >= 88 && b->binding < 92)
                    uiBufferIndex = (u32)CTexture::rstVertex + (b->binding - 88);
                else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER && b->binding >= 92 && b->binding < 96)
                    uiBufferIndex = b->binding - 92;
            }

            out_b.engineSlot = uiBufferIndex;
            out_b.isVS = isVS;

            bindings.push_back(out_b);

            if (constants && b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                const char* cb_name = b->name ? b->name : (b->type_description ? b->type_description->type_name : nullptr);

                // Safety fallback override if the string metadata is unmapped or empty
                if (!cb_name || strlen(cb_name) == 0) {
                    if (b->binding == 0) {
                        cb_name = "static_globals";
                    } else if (b->binding == 14) {
                        cb_name = "$Global";
                    } else {
                        cb_name = "$Global"; // Default fallback
                    }
                }

                if (cb_name)
                {
                    // 1. Create the Vulkan backing buffer wrapper
                    ref_cbuffer cbRef = xr_new<dx10ConstantBuffer>(cb_name, b->block.size);

                    // 2. Associate the destination stage layout index
                    // The engine distributes constants based on the bitmask of the key!
                    // CB_BufferVertexShader = 0x20, CB_BufferPixelShader = 0x10.
                    u32 cb_slot_index = 0;
                    if (b->binding < 14) {
                        cb_slot_index = (b->binding & CB_BufferIndexMask) | CB_BufferVertexShader;
                    } else if (b->binding >= 14 && b->binding < 28) {
                        cb_slot_index = ((b->binding - 14) & CB_BufferIndexMask) | CB_BufferPixelShader;
                    } else {
                        // Fallback (e.g. Geometry/Compute, unmapped for now)
                        cb_slot_index = b->binding;
                    }

                    // 3. Insert into the shader's Constant Buffer Table mapping
                    constants->m_CBTable.push_back(std::make_pair(cb_slot_index, cbRef));

                    Msg("--- [VK CB REFLECT] Registered Backing Constant Buffer: '%s' to Slot: %u | Size: %u bytes",
                        cb_name, b->binding, b->block.size);
                }

                // Process members for ALL uniform blocks that have named members.
                // The previous allowlist silently dropped skinning/bones blocks → sbones_array
                // was never in the ctable → get_c() null → zeroed bone matrices (collapsed arms).
                bool blockContainsConstants = (b->block.member_count > 0);
                if (blockContainsConstants)
                {
                    for (uint32_t m = 0; m < b->block.member_count; ++m)
                    {
                        const SpvReflectBlockVariable& member = b->block.members[m];
                        if (!member.name) continue;

                        u16 r_type = u16(-1);
                        // For arrays (e.g. float4 sbones_array[N]), SPIR-V-Reflect may report the
                        // FLOAT/VECTOR flags on the ELEMENT type, not the outer array wrapper — so
                        // `type_flags & FLOAT` can be false and the member gets silently skipped,
                        // leaving get_c("sbones_array") null → zeroed bone matrices → collapsed
                        // arms/hands/weapons. Detect arrays explicitly and treat them as RC_1x4
                        // (float4 stride); set_ca() indexes per-element at +16 bytes.
                        bool is_array  = (member.array.dims_count > 0);
                        bool is_float = (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT);
                        bool is_matrix = (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX);
                        bool is_vector = (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR);

                        if (is_array) {
                            // X-Ray array constants are always arrays of float4 (bone matrix rows).
                            r_type = RC_1x4;
                        }
                        else if (is_float) {
                            if (is_matrix) {
                                // Handle all matrix sizes used by X-Ray shaders:
                                //   float4x4 m_WVP  → RC_4x4  (64 bytes)
                                //   float4x3 m_WV   → RC_3x4  (48 bytes)  [4 rows, 3 cols]
                                //   float4x3 m_W    → RC_3x4  (48 bytes)
                                //   float4x2         → RC_2x4  (32 bytes)
                                // SPIR-V row_count/col_count for HLSL float4x3:
                                //   DXC emits it as col_count=3, row_count=4 (column-major mat3x4).
                                uint32_t rows = member.type_description->traits.numeric.matrix.row_count;
                                uint32_t cols = member.type_description->traits.numeric.matrix.column_count;
                                if (rows == 4 && cols == 4) {
                                    r_type = RC_4x4;
                                } else if ((rows == 4 && cols == 3) || (rows == 3 && cols == 4)) {
                                    r_type = RC_3x4; // float4x3 / float3x4
                                } else if ((rows == 4 && cols == 2) || (rows == 2 && cols == 4)) {
                                    r_type = RC_2x4; // float4x2 / float2x4
                                } else {
                                    r_type = RC_1x4; // float4, float3, float2, float1 (scalar/vector)
                                }
                            } else {
                                r_type = RC_1x4;
                            }
                        }

                        if (r_type != u16(-1))
                        {
                            if (is_array)
                                Msg("* [SPIRV-Reflect] Mapped ARRAY Uniform '%s' | Offset: %u | dims=%u dim0=%u | Type: RC_1x4",
                                    member.name, member.offset,
                                    member.array.dims_count, member.array.dims_count ? member.array.dims[0] : 0);

                            ref_constant C = xr_new<R_constant>();
                            C->name = member.name;
                            C->destination = destination;
                            C->type = RC_float;

                            u32 uiBufferIndex = b->binding;
                            if (destination & RC_dest_pixel) {
                                uiBufferIndex = (b->binding >= 14 && b->binding < 28) ? (b->binding - 14) : b->binding;
                                C->destination |= (uiBufferIndex << RC_dest_pixel_cb_index_shift);
                                C->ps.index = u16(member.offset);
                                C->ps.cls = r_type;
                            } else if (destination & RC_dest_vertex) {
                                // VS bindings are already 0-13
                                C->destination |= (uiBufferIndex << RC_dest_vertex_cb_index_shift);
                                C->vs.index = u16(member.offset);
                                C->vs.cls = r_type;
                            } else if (destination & RC_dest_geometry) {
                                uiBufferIndex = (b->binding >= 28 && b->binding < 42) ? (b->binding - 28) : b->binding;
                                C->destination |= (uiBufferIndex << RC_dest_geometry_cb_index_shift);
                                C->gs.index = u16(member.offset);
                                C->gs.cls = r_type;
                            }

                            constants->table.push_back(C);
                            Msg("* [SPIRV-Reflect] Mapped Uniform '%s' | Offset: %u | Size: %u | Type: %u", member.name, member.offset, member.size, (uint32_t)r_type);
                        }
                    }
                }
            }
            else if (constants && (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                                   b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER))
            {
                ref_constant C = xr_new<R_constant>();
                C->name = b->name ? b->name : "";
                C->destination = destination;

                if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                    C->type = RC_dx10texture;
                else
                    C->type = RC_sampler;

                C->samp.index = (u16)uiBufferIndex;
                C->samp.cls   = C->type;

                constants->table.push_back(C);
                Msg("* [SPIRV-Reflect] Mapped Resource '%s' | Slot: %u | Type: %s", C->name.c_str(), uiBufferIndex, C->type == RC_sampler ? "SAMPLER" : "TEXTURE");
            }
        }
    }

    // Sort by shared_str content (lexicographical order) to match p_sort_constants.
    // This is required because R_constant_table::equal() does an element-by-element 
    // index comparison for deduplication. Pointer-based sorting is non-deterministic 
    // across shader compilations and breaks cross-table deduplication.
    if (constants && !constants->table.empty())
    {
        std::sort(constants->table.begin(), constants->table.end(), [](const ref_constant& a, const ref_constant& b) {
            return xr_strcmp(a->name.c_str(), b->name.c_str()) < 0;
        });
    }

    spvReflectDestroyShaderModule(&module);
    return bindings;
}

VkPipelineLayout vk_ShaderReflection::CreatePipelineLayout(
    VkDevice device,
    const std::vector<vk_ShaderReflectionBinding>& vsBindings,
    const std::vector<vk_ShaderReflectionBinding>& psBindings,
    std::vector<VkDescriptorSetLayout>& outSetLayouts,
    const std::vector<vk_ShaderReflectionBinding>& gsBindings,
    const std::vector<vk_ShaderReflectionBinding>& csBindings)
{
    outSetLayouts.clear();

    // Aggregate bindings across all shader stages, keyed by (set, binding)
    std::map<uint32_t, std::map<uint32_t, VkDescriptorSetLayoutBinding>> setMap;

    auto addBindings = [&](const std::vector<vk_ShaderReflectionBinding>& bindings, VkShaderStageFlags stage) {
        for (const auto& b : bindings)
        {
            auto& lb             = setMap[b.set][b.binding];
            lb.binding           = b.binding;
            lb.descriptorType    = b.type;
            lb.descriptorCount   = b.count;
            lb.stageFlags       |= stage;
            lb.pImmutableSamplers = nullptr;
        }
    };

    addBindings(vsBindings, VK_SHADER_STAGE_VERTEX_BIT);
    addBindings(psBindings, VK_SHADER_STAGE_FRAGMENT_BIT);
    addBindings(gsBindings, VK_SHADER_STAGE_GEOMETRY_BIT);
    addBindings(csBindings, VK_SHADER_STAGE_COMPUTE_BIT);

    // Build one VkDescriptorSetLayout per set index.
    // outSetLayouts[i] corresponds to set i in set-index order (may have gaps if sets are non-contiguous).
    for (const auto& [setIndex, bindings] : setMap)
    {
        std::vector<VkDescriptorSetLayoutBinding> flatBindings;
        flatBindings.reserve(bindings.size());
        for (const auto& [bindingIndex, b] : bindings)
            flatBindings.push_back(b);

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        // [Phase 11] Enable Push Descriptors for Set 0 to completely eliminate vkUpdateDescriptorSets overhead
        // Removed: Now using cached allocated sets instead of push descriptors for performance
        // if (setIndex == 0)
        //    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        layoutInfo.bindingCount = (uint32_t)flatBindings.size();
        layoutInfo.pBindings    = flatBindings.data();

        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) == VK_SUCCESS)
            outSetLayouts.push_back(setLayout);   // FIX 1.1: now returned to caller, not leaked
    }

    // If no bindings were found (VS/PS have no descriptors yet), create an empty layout
    // so the pipeline layout is still valid.
    if (outSetLayouts.empty())
    {
        VkDescriptorSetLayoutCreateInfo emptyInfo{};
        emptyInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        // [Phase 11] If the layout is empty, it's still Set 0, so it must support Push Descriptors
        // Removed: Now using cached allocated sets instead of push descriptors
        // emptyInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        emptyInfo.bindingCount = 0;
        emptyInfo.pBindings    = nullptr;
        VkDescriptorSetLayout emptyLayout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device, &emptyInfo, nullptr, &emptyLayout) == VK_SUCCESS)
            outSetLayouts.push_back(emptyLayout);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = (uint32_t)outSetLayouts.size();
    pipelineLayoutInfo.pSetLayouts    = outSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult res = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (res != VK_SUCCESS)
        Msg("! vk_ShaderReflection::CreatePipelineLayout — vkCreatePipelineLayout failed (%d)", (int)res);

    return pipelineLayout;
}
