#pragma once
#include "stdafx.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <vector>

// Forward declarations
struct SDeclaration;

// Represents the full pipeline state required by Vulkan
struct vk_PipelineStateDesc
{
    // Shaders
    void* vs;
    void* ps;
    void* gs;
    void* cs;

    // Vertex Input Layout
    SDeclaration* decl;

    // Color & Blend State
    uint32_t colorWriteMask;
    uint32_t alphaRef;
    
    // Phase A.3 Alpha blending tracking
    uint32_t blendEnable;
    uint32_t srcBlend;
    uint32_t dstBlend;
    uint32_t blendOp;
    uint32_t srcBlendAlpha;
    uint32_t dstBlendAlpha;
    uint32_t blendOpAlpha;

    // FIX 1.2: Dynamic rendering attachment formats — driven by the bound RTs at compile time.
    // colorFormatCount = number of non-null pRT[] slots; colorFormats[] = their VkFormat values.
    uint32_t colorFormatCount;
    VkFormat colorFormats[4];
    VkFormat depthFormat;

    bool operator==(const vk_PipelineStateDesc& other) const
    {
        return memcmp(this, &other, sizeof(vk_PipelineStateDesc)) == 0;
    }
};

namespace std
{
    template<> struct hash<vk_PipelineStateDesc>
    {
        size_t operator()(const vk_PipelineStateDesc& s) const
        {
            const uint32_t fnv_prime = 16777619;
            uint32_t hash = 2166136261;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
            for (size_t i = 0; i < sizeof(vk_PipelineStateDesc); ++i)
            {
                hash ^= p[i];
                hash *= fnv_prime;
            }
            return hash;
        }
    };
}

struct VkRecordContext;

class vk_PipelineCacheManager
{

public:
    // FIX 1.1: Bundle pipeline + layout + set layouts so all are destroyed together.
    struct PipelineEntry
    {
        VkPipeline                        pipeline   = VK_NULL_HANDLE;
        VkPipelineLayout                  layout     = VK_NULL_HANDLE;
        std::vector<VkDescriptorSetLayout> setLayouts; // one per descriptor set index
        std::vector<uint32_t>             dynamicUniformBindings; // sorted list of uniform bindings expected
        bool                              validBindings[128] = {false}; // fast lookup for active shader bindings
        std::vector<std::pair<uint32_t, std::string>> samplerBindings; // binding -> named sampler to bind

        struct TextureBinding { uint32_t binding; uint32_t engineSlot; bool isVS; };
        std::vector<TextureBinding>       textureBindings; // explicit texture mapping from reflection
    };

private:
    VkDevice m_device;
    VkPipelineCache m_pipelineCache;

    std::unordered_map<vk_PipelineStateDesc, PipelineEntry> m_pipelines;
    std::mutex m_mutex;

    // FIX 2.3: vkCreateGraphicsPipelines() requires external host synchronization on the
    // shared m_pipelineCache handle per the Vulkan spec. m_mutex alone is NOT sufficient
    // because BindCurrentState() releases it before calling CompilePipeline() (so that a
    // slow compile on one thread doesn't block warm-cache lookups on other threads).
    // m_compileMutex guards ONLY the actual vkCreateGraphicsPipelines() call.
    std::mutex m_compileMutex;

public:
    const PipelineEntry& GetLastBoundEntry(VkRecordContext* ctx) const;
    vk_PipelineCacheManager();
    ~vk_PipelineCacheManager();

    void Initialize(VkDevice device, VkPipelineCache cache = VK_NULL_HANDLE);
    void Destroy();

    void SetRenderPass(VkRenderPass) {} // no-op — dynamic rendering requires no VkRenderPass

    // Must be called once per command-buffer begin (before any BindCurrentState),
    // because dynamic state does not persist across command buffers.
    void ResetDynamicStateCache(VkRecordContext* ctx);

    // Computes hash from RCache, looks up pipeline, creates if missing, binds, and returns VkPipeline handle
    VkPipeline BindCurrentState(VkCommandBuffer cmdBuffer, VkRecordContext* ctx);

    // Returns the VkPipelineLayout from the last BindCurrentState call
    VkPipelineLayout GetLastBoundLayout(VkRecordContext* ctx) const;

    // Returns the VkDescriptorSetLayout for the given set index from the last BindCurrentState call.
    // Returns VK_NULL_HANDLE if set index is out of range.
    VkDescriptorSetLayout GetLastBoundSetLayout(uint32_t set, VkRecordContext* ctx) const;

private:
    PipelineEntry CompilePipeline(const vk_PipelineStateDesc& desc);

    // FIX 2.3: releases a duplicate PipelineEntry's Vulkan resources when two threads
    // raced to compile the same vk_PipelineStateDesc and lost the double-checked insert.
    void DestroyPipelineEntry(PipelineEntry& entry);
};

extern vk_PipelineCacheManager PipelineCache;
