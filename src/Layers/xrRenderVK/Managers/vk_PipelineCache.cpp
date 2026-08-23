#include "stdafx.h"
#include "vk_PipelineCache.h"
#include "../xrRender/xrD3DDefs.h"
#include "../xrRender/SH_Atomic.h"
#include "../xrRender/R_Backend.h"
#include "../Resources/vk_ShaderReflection.h"
#include "../Resources/vk_Shader.h"
#include "../Resources/vk_BufferUtils.h"   // D3DDeclTypeSize (shared with loader)
#include <vector>
#include <algorithm>
#include <unordered_set>

// X-Ray's global RCache
extern ECORE_API CBackend RCache;

static inline VkCullModeFlags MapCullMode(uint32_t d3dCull)
{
    // D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3
    if (d3dCull == 1) return VK_CULL_MODE_NONE;
    if (d3dCull == 2) return VK_CULL_MODE_FRONT_BIT; // X-Ray typically treats CW as front cull
    return VK_CULL_MODE_BACK_BIT;
}

static inline VkCompareOp MapCompareOp(uint32_t d3dCmp)
{
    // D3DCMP_NEVER=1, LESS=2, EQUAL=3, LESSEQUAL=4, GREATER=5, NOTEQUAL=6, GREATEREQUAL=7, ALWAYS=8
    switch (d3dCmp) {
        case 1: return VK_COMPARE_OP_NEVER;
        case 2: return VK_COMPARE_OP_LESS;
        case 3: return VK_COMPARE_OP_EQUAL;
        case 4: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 5: return VK_COMPARE_OP_GREATER;
        case 6: return VK_COMPARE_OP_NOT_EQUAL;
        case 7: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 8: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

// ==============================================================================
// State Translation Helpers (Direct3D 9 -> Vulkan)
// ==============================================================================
// The X-Ray Engine core and its shader cache (`SimulatorStates`) were originally 
// built around DirectX 9. As a result, the engine internally tracks render states 
// (blending, stencil operations, depth comparison, etc.) using legacy D3D9 
// enumeration integers (e.g., D3DBLEND_SRCALPHA, D3DSTENCILOP_KEEP). 
// 
// The following mapping functions translate these legacy D3D9 integer values 
// into their direct Vulkan equivalents (VkBlendFactor, VkStencilOp, etc.) 
// so they can be consumed by Vulkan pipeline states and dynamic command buffers.
// ==============================================================================

static inline VkStencilOp MapStencilOp(uint32_t d3dOp)
{
    // D3DSTENCILOP_KEEP=1, ZERO=2, REPLACE=3, INCRSAT=4, DECRSAT=5, INVERT=6, INCR=7, DECR=8
    switch (d3dOp) {
        case 1: return VK_STENCIL_OP_KEEP;
        case 2: return VK_STENCIL_OP_ZERO;
        case 3: return VK_STENCIL_OP_REPLACE;
        case 4: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 5: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 6: return VK_STENCIL_OP_INVERT;
        case 7: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 8: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_KEEP;
    }
}

static inline VkBlendFactor MapBlendFactor(uint32_t d3dBlend)
{
    // D3DBLEND_ZERO=1, ONE=2, SRCCOLOR=3, INVSRCCOLOR=4, SRCALPHA=5, INVSRCALPHA=6, DESTALPHA=7, INVDESTALPHA=8, DESTCOLOR=9, INVDESTCOLOR=10, SRCALPHASAT=11
    switch (d3dBlend) {
        case 1: return VK_BLEND_FACTOR_ZERO;
        case 2: return VK_BLEND_FACTOR_ONE;
        case 3: return VK_BLEND_FACTOR_SRC_COLOR;
        case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 7: return VK_BLEND_FACTOR_DST_ALPHA;
        case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 9: return VK_BLEND_FACTOR_DST_COLOR;
        case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

static inline VkBlendOp MapBlendOp(uint32_t d3dBlendOp)
{
    // D3DBLENDOP_ADD=1, SUBTRACT=2, REVSUBTRACT=3, MIN=4, MAX=5
    switch (d3dBlendOp) {
        case 1: return VK_BLEND_OP_ADD;
        case 2: return VK_BLEND_OP_SUBTRACT;
        case 3: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 4: return VK_BLEND_OP_MIN;
        case 5: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

static inline VkPrimitiveTopology MapTopology(uint32_t d3dTopo)
{
    // Matches D3D_PRIMITIVE_TOPOLOGY
    switch (d3dTopo) {
        case 1: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case 3: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case 4: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

vk_PipelineCacheManager PipelineCache;

vk_PipelineCacheManager::vk_PipelineCacheManager()
    : m_device(VK_NULL_HANDLE)
    , m_pipelineCache(VK_NULL_HANDLE)
{
}

vk_PipelineCacheManager::~vk_PipelineCacheManager()
{
    Destroy();
}

void vk_PipelineCacheManager::Initialize(VkDevice device, VkPipelineCache cache)
{
    m_device = device;
    m_pipelineCache = cache;
}

void vk_PipelineCacheManager::Destroy()
{
    if (m_device != VK_NULL_HANDLE)
    {
        for (auto& pair : m_pipelines)
        {
            // FIX 1.1: Destroy set layouts before the pipeline layout and pipeline
            for (VkDescriptorSetLayout sl : pair.second.setLayouts)
                if (sl != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(m_device, sl, nullptr);

            if (pair.second.layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(m_device, pair.second.layout, nullptr);
            if (pair.second.pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(m_device, pair.second.pipeline, nullptr);
        }
        m_pipelines.clear();
    }
    m_device        = VK_NULL_HANDLE;
    m_pipelineCache = VK_NULL_HANDLE;
}

void vk_PipelineCacheManager::ResetDynamicStateCache(VkRecordContext* ctx)
{
    ctx->m_hasLastDesc = false;
    ctx->m_dsCull = ctx->m_dsDepthTest = ctx->m_dsDepthWrite = ctx->m_dsDepthCmp = 0xFFFFFFFF;
    ctx->m_dsTopology = 0xFFFFFFFF;
    ctx->m_dsStencilEn = ctx->m_dsStencilFunc = ctx->m_dsStencilFail = ctx->m_dsStencilPass = 0xFFFFFFFF;
    ctx->m_dsStencilZFail = ctx->m_dsStencilCmpMask = ctx->m_dsStencilWrMask = ctx->m_dsStencilRef = 0xFFFFFFFF;
    ctx->m_dsLastPipeline = VK_NULL_HANDLE;
}

VkPipeline vk_PipelineCacheManager::BindCurrentState(VkCommandBuffer cmdBuffer, VkRecordContext* ctx)
{
    static u32 s_lastFrame = 0;
    static u32 s_pipes_issued = 0, s_pipes_skipped = 0;
    static bool s_pipeStats = !!strstr(Core.Params, "-vk_pipe_stats");
    if (s_pipeStats && s_lastFrame != Device.dwFrame) {
        if (s_lastFrame != 0)
            Msg("VK PIPE STATS f%u: %u issued, %u skipped", s_lastFrame, s_pipes_issued, s_pipes_skipped);
        s_pipes_issued = 0;
        s_pipes_skipped = 0;
        s_lastFrame = Device.dwFrame;
    }

    auto ApplyDynamicStates = [&](VkPipeline pipeline) {
        if (cmdBuffer != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE)
        {
            // Pipeline itself: only rebind when it actually changes.
            if (pipeline != ctx->m_dsLastPipeline)
            {
                vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                ctx->m_dsLastPipeline = pipeline;
            }

            // ── Cull / Depth ─────────────────────────────────────────────
            const uint32_t curCull = (uint32_t)MapCullMode(ctx->cull_mode);
            if (curCull != ctx->m_dsCull) { g_vkCmdSetCullMode(cmdBuffer, (VkCullModeFlags)curCull); ctx->m_dsCull = curCull; }

            const uint32_t curDT = ctx->z_enable ? 1u : 0u;
            if (curDT != ctx->m_dsDepthTest) { g_vkCmdSetDepthTestEnable(cmdBuffer, curDT ? VK_TRUE : VK_FALSE); ctx->m_dsDepthTest = curDT; }

            const uint32_t curDW = ctx->z_write_enable ? 1u : 0u;
            if (curDW != ctx->m_dsDepthWrite) { g_vkCmdSetDepthWriteEnable(cmdBuffer, curDW ? VK_TRUE : VK_FALSE); ctx->m_dsDepthWrite = curDW; }

            const uint32_t curDCmp = (uint32_t)MapCompareOp(ctx->z_func);
            if (curDCmp != ctx->m_dsDepthCmp) { g_vkCmdSetDepthCompareOp(cmdBuffer, (VkCompareOp)curDCmp); ctx->m_dsDepthCmp = curDCmp; }

            // ── Stencil ──────────────────────────────────────────────────
            const uint32_t curStEn = (ctx->stencil_enable != u32(-1) && ctx->stencil_enable) ? 1u : 0u;
            if (curStEn != ctx->m_dsStencilEn) { g_vkCmdSetStencilTestEnable(cmdBuffer, curStEn ? VK_TRUE : VK_FALSE); ctx->m_dsStencilEn = curStEn; }

            if (curStEn) // only push op/masks/ref when the test is actually enabled
            {
                const uint32_t sFail  = (uint32_t)MapStencilOp(ctx->stencil_fail);
                const uint32_t sPass  = (uint32_t)MapStencilOp(ctx->stencil_pass);
                const uint32_t sZFail = (uint32_t)MapStencilOp(ctx->stencil_zfail);
                const uint32_t sFunc  = (uint32_t)MapCompareOp(ctx->stencil_func);
                if (sFail != ctx->m_dsStencilFail || sPass != ctx->m_dsStencilPass ||
                    sZFail != ctx->m_dsStencilZFail || sFunc != ctx->m_dsStencilFunc)
                {
                    g_vkCmdSetStencilOp(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK,
                        (VkStencilOp)sFail, (VkStencilOp)sPass, (VkStencilOp)sZFail, (VkCompareOp)sFunc);
                    ctx->m_dsStencilFail = sFail; ctx->m_dsStencilPass = sPass;
                    ctx->m_dsStencilZFail = sZFail; ctx->m_dsStencilFunc = sFunc;
                }

                if (ctx->stencil_mask != ctx->m_dsStencilCmpMask)
                { g_vkCmdSetStencilCompareMask(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, ctx->stencil_mask); ctx->m_dsStencilCmpMask = ctx->stencil_mask; }

                if (ctx->stencil_writemask != ctx->m_dsStencilWrMask)
                { g_vkCmdSetStencilWriteMask(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, ctx->stencil_writemask); ctx->m_dsStencilWrMask = ctx->stencil_writemask; }

                if (ctx->stencil_ref != ctx->m_dsStencilRef)
                { g_vkCmdSetStencilReference(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, ctx->stencil_ref); ctx->m_dsStencilRef = ctx->stencil_ref; }
            }

            // ── Topology ─────────────────────────────────────────────────
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
            const uint32_t curTopo = (uint32_t)MapTopology(ctx->m_PrimitiveTopology);
            if (curTopo != ctx->m_dsTopology) { g_vkCmdSetPrimitiveTopology(cmdBuffer, (VkPrimitiveTopology)curTopo); ctx->m_dsTopology = curTopo; }
#endif
        }
    };

    if (!ctx->m_pipelineDirty && ctx->m_hasLastDesc)
    {
        s_pipes_skipped++;
#ifdef DEBUG
        vk_PipelineStateDesc debugDesc = {};
        debugDesc.vs = ctx->vs;
        debugDesc.ps = ctx->ps;
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
        debugDesc.gs = ctx->gs;
        debugDesc.decl = ctx->decl;
#else
        debugDesc.decl = nullptr; // Not used in DX9 wrapper
#endif
        
        debugDesc.blendEnable = ctx->blend_enable;
        debugDesc.srcBlend = ctx->blend_src;
        debugDesc.dstBlend = ctx->blend_dst;
        debugDesc.blendOp = ctx->blend_op;
        debugDesc.srcBlendAlpha = ctx->blend_src_alpha;
        debugDesc.dstBlendAlpha = ctx->blend_dst_alpha;
        debugDesc.blendOpAlpha = ctx->blend_op_alpha;
        debugDesc.colorWriteMask = ctx->colorwrite_mask;

        VERIFY(memcmp(&debugDesc, &ctx->m_lastDesc, sizeof(vk_PipelineStateDesc)) == 0);
#endif
        
        if (ctx->m_lastBoundEntry)
            ApplyDynamicStates(ctx->m_lastBoundEntry->pipeline);

        return ctx->m_lastBoundEntry ? ctx->m_lastBoundEntry->pipeline : VK_NULL_HANDLE;
    }

    s_pipes_issued++;

    vk_PipelineStateDesc desc = {};
    desc.vs = ctx->vs;
    desc.ps = ctx->ps;
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
    desc.gs = ctx->gs;
    desc.decl = ctx->decl;
#else
    desc.decl = nullptr;
#endif
    
    desc.blendEnable = ctx->blend_enable;
    desc.srcBlend = ctx->blend_src;
    desc.dstBlend = ctx->blend_dst;
    desc.blendOp = ctx->blend_op;
    desc.srcBlendAlpha = ctx->blend_src_alpha;
    desc.dstBlendAlpha = ctx->blend_dst_alpha;
    desc.blendOpAlpha = ctx->blend_op_alpha;
    desc.colorWriteMask = ctx->colorwrite_mask;

    // FIX 1.2: Populate RT formats from currently-bound render targets.
    // This ensures G-Buffer pipelines (3 RTs) and final-pass pipelines (1 RT) hash separately
    // and compile with the correct VkPipelineRenderingCreateInfo attachment count/formats.
    desc.colorFormatCount = 0;
    for (int rtIdx = 0; rtIdx < 4; ++rtIdx)
    {
        auto* rtv = static_cast<VkRTVWrapper*>(ctx->pRT[rtIdx]);
        if (rtv)
        {
            desc.colorFormats[desc.colorFormatCount] = rtv->format;
            ++desc.colorFormatCount;
        }
    }

    // Compute depthFormat FIRST so the swapchain fallback can distinguish
    // a true UI draw (no depth) from a depth-only shadow draw (has depth).
    desc.depthFormat = static_cast<VkDSVWrapper*>(ctx->pZB)
                       ? HW.m_vkDepthFormat
                       : VK_FORMAT_UNDEFINED;

    // FIX 1.2: If no RTs are bound AND no depth is bound, X-Ray assumes rendering to the swapchain backbuffer.
    // Ensure the pipeline is created with the swapchain format and 1 attachment.
    // Depth-only shadow passes keep colorFormatCount = 0 so their pipeline matches colorAttachmentCount = 0.
    if (desc.colorFormatCount == 0 && desc.depthFormat == VK_FORMAT_UNDEFINED && HW.m_vkSCFormat != VK_FORMAT_UNDEFINED)
    {
        desc.colorFormats[0] = HW.m_vkSCFormat;
        desc.colorFormatCount = 1;
    }

    // Fill remaining slots to keep memcmp deterministic
    for (uint32_t rtIdx = desc.colorFormatCount; rtIdx < 4; ++rtIdx)
        desc.colorFormats[rtIdx] = VK_FORMAT_UNDEFINED;

#ifdef DEBUG
    // Temporary log for forward-pass / particle draws (1 RT + Depth Bound)
    static int iParticleLogCounter = 0;
    if (iParticleLogCounter < 100 && desc.colorFormatCount == 1 && desc.depthFormat != VK_FORMAT_UNDEFINED && desc.ps != VK_NULL_HANDLE)
    {
        Msg("[VK PBLEND] ps=%p enable=%u src=%u dst=%u z_en=%u z_write=%u",
            desc.ps, ctx->blend_enable, ctx->blend_src, ctx->blend_dst, ctx->z_enable, ctx->z_write_enable);
        iParticleLogCounter++;
        if (iParticleLogCounter % 10 == 0 || iParticleLogCounter == 100)
            xrLogger::FlushLog();
    }
#endif

    // 4. Look up in hash map
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pipelines.find(desc);
        if (it != m_pipelines.end())
        {
            ApplyDynamicStates(it->second.pipeline);
            ctx->m_lastBoundLayout = it->second.layout;
            ctx->m_lastBoundEntry  = &it->second;
            ctx->m_lastDesc = desc;
            ctx->m_hasLastDesc = true;
            ctx->m_pipelineDirty = false;
            return it->second.pipeline;
        }
    }

    // 5. Not found — compile a new pipeline
    PipelineEntry entry = CompilePipeline(desc);

    // 6. Store in cache and bind
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pipelines[desc] = entry;
    }

    ApplyDynamicStates(entry.pipeline);
    ctx->m_lastBoundLayout = entry.layout;
    ctx->m_lastBoundEntry  = &m_pipelines[desc];
    ctx->m_lastDesc = desc;
    ctx->m_hasLastDesc = true;
    ctx->m_pipelineDirty = false;
    return entry.pipeline;
}

// ─── Chunk 3: D3D9 declaration → Vulkan vertex input ──────────────────────────

// D3DDECLTYPE → byte size

// D3DDECLTYPE → VkFormat
static VkFormat D3DDeclTypeToVkFormat(uint8_t type)
{
    switch (type) {
    case 0:  return VK_FORMAT_R32_SFLOAT;
    case 1:  return VK_FORMAT_R32G32_SFLOAT;
    case 2:  return VK_FORMAT_R32G32B32_SFLOAT;
    case 3:  return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 4:  return VK_FORMAT_R8G8B8A8_UNORM;        // D3DCOLOR: present as RGBA so shader's .bgra swizzle (DX11 compat) corrects to proper colors
    case 5:  return VK_FORMAT_R8G8B8A8_UINT;
    case 6:  return VK_FORMAT_R16G16_SINT;
    case 7:  return VK_FORMAT_R16G16B16A16_SINT;
    case 8:  return VK_FORMAT_R8G8B8A8_UNORM;        // UBYTE4N
    case 9:  return VK_FORMAT_R16G16_SNORM;
    case 10: return VK_FORMAT_R16G16B16A16_SNORM;
    case 11: return VK_FORMAT_R16G16_UNORM;
    case 12: return VK_FORMAT_R16G16B16A16_UNORM;
    case 13: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case 14: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case 15: return VK_FORMAT_R16G16_SFLOAT;
    case 16: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

// Collision-proof field-name matcher.
// DXC/glslang emits SPIR-V input names as "I.<fieldname>" or "v.<fieldname>"
// (the HLSL struct variable name followed by dot-separated field).
// Extract the token after the last '.' and compare case-insensitively.
static bool nameMatchesField(const std::string& spvName, const char* field)
{
    size_t dot = spvName.find_last_of('.');
    const std::string tok = (dot == std::string::npos) ? spvName : spvName.substr(dot + 1);
    if (tok.size() != strlen(field)) return false;
    for (size_t i = 0; i < tok.size(); ++i)
        if (::toupper((unsigned char)tok[i]) != ::toupper((unsigned char)field[i])) return false;
    return true;
}

// True if the reflection declares any input variable at the given location.
static bool reflectionHasLocation(const std::vector<vk_ShaderReflectionInput>& reflection, uint32_t loc)
{
    for (const auto& in : reflection)
        if (in.location == loc) return true;
    return false;
}

// Map a D3D vertex-declaration usage+index to the exact SPIR-V input Location.
// Uses exact post-dot field-name matching against known X-Ray HLSL struct field names.
//
// Static geometry  (v_static):       Nh, T, B, tc, [lmh], P
// Static+color     (v_static_color):  Nh, T, B, tc, [lmh], Color, P
// Skinned geometry (v_skinned):       N,  T, B, tc, ind,   P
// UI               (v_TL_positiont):  P, Tex0, Color
// Combine          (_in):             P, tcJ
static uint32_t FindReflectionLocation(const std::vector<vk_ShaderReflectionInput>& reflection, uint8_t usage, uint8_t usageIndex)
{
    // Build a small list of candidate exact field names for this usage+index.
    // Listed in priority order — first hit wins.
    std::vector<const char*> fieldNames;
    switch (usage) {
        case 0:  // POSITION
        case 9:  // POSITIONT
            fieldNames = {"P"};
            break;
        case 1:  // BLENDWEIGHT (skinned)
            fieldNames = {"wt", "weight"};
            break;
        case 2:  // BLENDINDICES (skinned)
            fieldNames = {"ind"};
            break;
        case 3:  // NORMAL  — v_static uses "Nh", skinned uses "N"
            fieldNames = {"Nh", "N"};
            break;
        case 6:  // TANGENT
            fieldNames = {"T"};
            break;
        case 7:  // BINORMAL
            fieldNames = {"B"};
            break;
        case 5:  // TEXCOORD — usageIndex selects which set.
            //   idx 0: static "tc", UI "Tex0", combine jitter "tcJ", generic "tc0"/"uv"
            //   idx 1: static lightmap "lmh", UI "Tex1", skinned bone "ind"
            if (usageIndex == 0)
                fieldNames = {"tc", "Tex0", "tcJ", "tc0", "uv"};
            else if (usageIndex == 1)
                fieldNames = {"lmh", "Tex1", "lm", "ind", "tc1"};
            else
                fieldNames = {"tc", "Tex0", "uv"};
            break;
        case 10: // COLOR
            fieldNames = {"Color", "col"};
            break;
        default:
            break;
    }

    if (fieldNames.empty()) return (uint32_t)-1;

    for (const char* field : fieldNames)
    {
        for (const auto& in : reflection)
        {
            if (nameMatchesField(in.name, field))
            {
                Msg("* [Vulkan] Mapped Semantic Usage %d (Index %d) to Location %u (field '%s', SPIR-V '%s')",
                    usage, usageIndex, in.location, field, in.name.c_str());
                return in.location;
            }
        }
    }

    return (uint32_t)-1;
}

struct BuiltVertexInput
{
    std::vector<VkVertexInputBindingDescription>   bindings;
    std::vector<VkVertexInputAttributeDescription> attribs;
};

static BuiltVertexInput BuildVertexInput(const SDeclaration* decl, const std::vector<vk_ShaderReflectionInput>& reflection)
{
    BuiltVertexInput out;
    if (!decl || decl->dcl_code.empty())
        return out;

    // FIX 1.3: Compute stride as max(element.Offset + elementSize) per stream, NOT a sum.
    std::unordered_map<uint16_t, uint32_t> streamStrides;
    for (const auto& el : decl->dcl_code)
    {
        if (el.Stream == 0xFF) break;
        uint32_t endOffset = el.Offset + D3DDeclTypeSize(el.Type);
        auto it = streamStrides.find(el.Stream);
        if (it == streamStrides.end())
            streamStrides[el.Stream] = endOffset;
        else if (endOffset > it->second)
            it->second = endOffset;
    }

    // Binding descriptions (one per stream)
    for (auto& [stream, stride] : streamStrides)
    {
        VkVertexInputBindingDescription b{};
        b.binding   = stream;
        b.stride    = stride;
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        out.bindings.push_back(b);
    }

    // Track which SPIR-V input locations have been assigned a real attribute.
    std::unordered_set<uint32_t> assignedLocations;

    // Collect all valid decl elements for two-pass processing.
    struct PendingEl { VkFormat fmt; uint16_t stream; uint32_t offset; uint8_t usage; uint8_t usageIndex; };
    std::vector<PendingEl> pending;
    for (const auto& el : decl->dcl_code)
    {
        if (el.Stream == 0xFF) break;
        VkFormat fmt = D3DDeclTypeToVkFormat(el.Type);
        if (fmt == VK_FORMAT_UNDEFINED) continue;
        pending.push_back({ fmt, el.Stream, el.Offset, el.Usage, el.UsageIndex });
    }

    // PASS 1: exact field-name matches.
    // Each element that matches by name claims its precise SPIR-V location.
    // This runs before the sequential fallback so name-matched locations are
    // locked in and the fallback cannot steal them.
    std::vector<bool> matched(pending.size(), false);
    for (size_t i = 0; i < pending.size(); ++i)
    {
        uint32_t reflLoc = FindReflectionLocation(reflection, pending[i].usage, pending[i].usageIndex);
        if (reflLoc == (uint32_t)-1) continue;
        if (assignedLocations.count(reflLoc)) { matched[i] = true; continue; } // dup guard

        matched[i] = true;
        assignedLocations.insert(reflLoc);

        VkVertexInputAttributeDescription a{};
        a.location = reflLoc;
        a.binding  = pending[i].stream;
        a.format   = pending[i].fmt;
        a.offset   = pending[i].offset;
        out.attribs.push_back(a);
    }

    // PASS 2: sequential fallback for unmatched elements (e.g. UI shaders whose
    // field names aren't in our table).  Only assigns locations that actually
    // EXIST in the reflection and aren't already claimed — this prevents decl
    // elements with no shader counterpart (e.g. the D3DCOLOR in an FVF::TL quad
    // whose VS has no COLOR input) from stealing locations that belong to other
    // elements (e.g. the TEXCOORD that carries the jitter/tonemap coords).
    uint32_t locationIndex = 0;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        if (matched[i]) continue;

        // Advance past taken slots AND past locations the shader doesn't declare.
        while (locationIndex < 16 &&
               (assignedLocations.count(locationIndex) ||
                !reflectionHasLocation(reflection, locationIndex)))
        {
            ++locationIndex;
        }
        if (locationIndex >= 16) continue; // no free shader input — element is unused

        uint32_t assignedLoc = locationIndex++;
        assignedLocations.insert(assignedLoc);

        VkVertexInputAttributeDescription a{};
        a.location = assignedLoc;
        a.binding  = pending[i].stream;
        a.format   = pending[i].fmt;
        a.offset   = pending[i].offset;
        out.attribs.push_back(a);
    }

    // Cover every SPIR-V input location not served by a real decl element with a
    // safe FLOAT4 dummy at offset 0 (Vulkan requires all consumed locations exist).
    for (const auto& in : reflection)
    {
        if (!assignedLocations.count(in.location))
        {
            VkVertexInputAttributeDescription a{};
            a.location = in.location;
            a.binding  = 0;
            a.format   = VK_FORMAT_R32G32B32A32_SFLOAT;
            a.offset   = 0;
            out.attribs.push_back(a);
        }
    }

    return out;
}

// ─── Chunk 4: CompilePipeline — now calls vkCreateGraphicsPipelines ───────────

vk_PipelineCacheManager::PipelineEntry vk_PipelineCacheManager::CompilePipeline(const vk_PipelineStateDesc& desc)
{
    PipelineEntry entry{};

    auto* vs = (VkVertexShaderWrapper*)desc.vs;
    auto* ps = (VkPixelShaderWrapper*)desc.ps;

    // Guard: require at least a valid VS and PS VkShaderModule.
    // If not present (e.g. HLSL was passed before DXC integration), skip silently.
    if (!vs || vs->module == VK_NULL_HANDLE ||
        !ps || ps->module == VK_NULL_HANDLE)
    {
        Msg("* vk_PipelineCache: skipping compile — VS '%s' or PS '%s' module not ready (SPIR-V pending)",
            vs ? vs->name : "null", ps ? ps->name : "null");
        return entry;
    }

    // 1. Reflect shaders to generate Pipeline Layout and Vertex Inputs
    std::vector<vk_ShaderReflectionBinding> vsBindings, psBindings;
    vsBindings = vk_ShaderReflection::ReflectSPIRV((const uint32_t*)vs->pSPIRV, vs->size, nullptr, RC_dest_vertex);
    psBindings = vk_ShaderReflection::ReflectSPIRV((const uint32_t*)ps->pSPIRV, ps->size, nullptr, RC_dest_pixel);

    std::vector<vk_ShaderReflectionInput> vsInputs = vk_ShaderReflection::ReflectInputs((const uint32_t*)vs->pSPIRV, vs->size);

    // FIX 1.1: outSetLayouts now owned by entry.setLayouts — destroyed in Destroy()
    VkPipelineLayout layout = vk_ShaderReflection::CreatePipelineLayout(
        m_device, vsBindings, psBindings, entry.setLayouts);
    entry.layout = layout;

    // Combine bindings and extract expected dynamic uniform buffers in strictly ascending order
    std::vector<vk_ShaderReflectionBinding> allBindings;
    allBindings.insert(allBindings.end(), vsBindings.begin(), vsBindings.end());
    allBindings.insert(allBindings.end(), psBindings.begin(), psBindings.end());
    std::sort(allBindings.begin(), allBindings.end(), [](const vk_ShaderReflectionBinding& a, const vk_ShaderReflectionBinding& b) {
        if (a.set != b.set) return a.set < b.set;
        return a.binding < b.binding;
    });

    uint32_t lastSet = ~0u;
    uint32_t lastBinding = ~0u;
    for (const auto& b : allBindings)
    {
        if (b.binding < 128)
            entry.validBindings[b.binding] = true;

        if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER)
            entry.samplerBindings.emplace_back(b.binding, b.name);
        else if (b.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
            entry.textureBindings.push_back({ b.binding, b.engineSlot, b.isVS });

        if (b.set == lastSet && b.binding == lastBinding) continue;
        lastSet = b.set; lastBinding = b.binding;
        if (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) // Track uniform buffers
            entry.dynamicUniformBindings.push_back(b.binding);
    }

    // 2. Shader stage create infos
    VkPipelineShaderStageCreateInfo stageVS = {};
    stageVS.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageVS.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stageVS.module = vs->module;
    stageVS.pName  = vs->entryPoint;   // was "main" — use stored entry point

    VkPipelineShaderStageCreateInfo stagePS = {};
    stagePS.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stagePS.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stagePS.module = ps->module;
    stagePS.pName  = ps->entryPoint;   // was "main" — use stored entry point

    std::vector<VkPipelineShaderStageCreateInfo> stages = { stageVS, stagePS };

    // Optional geometry shader
    VkGeometryShaderWrapper* gs = (VkGeometryShaderWrapper*)desc.gs;
    if (gs && gs->module != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stageGS = {};
        stageGS.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageGS.stage  = VK_SHADER_STAGE_GEOMETRY_BIT;
        stageGS.module = gs->module;
        stageGS.pName  = gs->entryPoint;   // was "main"
        stages.push_back(stageGS);
    }

    // 3. Vertex input (Chunk 3)
    BuiltVertexInput vi = BuildVertexInput(desc.decl, vsInputs);
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount   = (uint32_t)vi.bindings.size();
    vertexInputInfo.pVertexBindingDescriptions      = vi.bindings.empty() ? nullptr : vi.bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)vi.attribs.size();
    vertexInputInfo.pVertexAttributeDescriptions    = vi.attribs.empty() ? nullptr : vi.attribs.data();

    // Add this right before calling vkCreateGraphicsPipelines to verify the exact structural layout
    //Msg("--- [VK PIPELINE DUMP] Creating Pipeline for Shader VS '%s' | PS '%s'",
    //    vs ? vs->name : "null", ps ? ps->name : "null");
    //for (const auto& attr : vi.attribs) {
    //    Msg("    -> Location: %u | Binding: %u | Format: %u | Offset: %u",
    //        attr.location, attr.binding, attr.format, attr.offset);
    //}

    // 4. Dynamic states — cull, depth, stencil, topology, viewport, scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateInfo.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicStateInfo.pDynamicStates    = dynamicStates.data();

    // 5. Fixed-function states (topology / rasterizer / MS / DS / blend)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // overridden by dynamic state

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    // cullMode / frontFace set dynamically

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    // depthTestEnable / depthWriteEnable / compareOp set dynamically

    // Blend: opaque attachment state — one entry per RT slot (FIX 1.2: must match colorFormatCount)
    uint32_t numAttachments = desc.colorFormatCount > 0 ? desc.colorFormatCount : 1;
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(numAttachments);
    // FIX 1.2: X-Ray UI uses 0 render targets, but we forced colorFormatCount = 1 earlier.
    bool isUISwapchainPass = (desc.colorFormatCount == 1 && desc.depthFormat == VK_FORMAT_UNDEFINED && ps != VK_NULL_HANDLE);

    for (uint32_t i = 0; i < numAttachments; ++i)
    {
        blendAttachments[i] = {};
        blendAttachments[i].colorWriteMask = desc.colorWriteMask
            ? desc.colorWriteMask
            : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);

        if (desc.blendEnable == TRUE)
        {
            blendAttachments[i].blendEnable = VK_TRUE;
            blendAttachments[i].srcColorBlendFactor = MapBlendFactor(desc.srcBlend);
            blendAttachments[i].dstColorBlendFactor = MapBlendFactor(desc.dstBlend);
            blendAttachments[i].colorBlendOp = MapBlendOp(desc.blendOp);
            
            // X-Ray typically sets D3DRS_ALPHABLENDENABLE but doesn't always provide separate alpha operations.
            // If srcBlendAlpha is defined, we map it, else fallback to the color factors
            blendAttachments[i].srcAlphaBlendFactor = (desc.srcBlendAlpha != u32(-1)) ? MapBlendFactor(desc.srcBlendAlpha) : blendAttachments[i].srcColorBlendFactor;
            blendAttachments[i].dstAlphaBlendFactor = (desc.dstBlendAlpha != u32(-1)) ? MapBlendFactor(desc.dstBlendAlpha) : blendAttachments[i].dstColorBlendFactor;
            blendAttachments[i].alphaBlendOp = (desc.blendOpAlpha != u32(-1)) ? MapBlendOp(desc.blendOpAlpha) : blendAttachments[i].colorBlendOp;
        }
        else if (desc.blendEnable == u32(-1) && isUISwapchainPass)
        {
            blendAttachments[i].blendEnable = VK_TRUE;
            blendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
        }
        else
        {
            blendAttachments[i].blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = numAttachments;
    colorBlending.pAttachments    = blendAttachments.data();

    // 6. FIX 1.2: Dynamic Rendering (Patched for Swapchain Handoff)
    VkPipelineRenderingCreateInfo renderingCI{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

    // colorFormatCount now correctly reflects intent (BindCurrentState only forces
    // the swapchain attachment for true UI draws — depth-only shadow passes keep 0).
    renderingCI.colorAttachmentCount    = desc.colorFormatCount;
    renderingCI.pColorAttachmentFormats = desc.colorFormatCount > 0 ? desc.colorFormats : nullptr;

    renderingCI.depthAttachmentFormat   = desc.depthFormat;
    renderingCI.stencilAttachmentFormat = desc.depthFormat; // D24S8 carries stencil in same format

    // 7. Assemble and compile
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext              = &renderingCI;   // chain dynamic rendering info
    pipelineInfo.stageCount         = (uint32_t)stages.size();
    pipelineInfo.pStages            = stages.data();
    pipelineInfo.pVertexInputState  = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState= &inputAssembly;
    pipelineInfo.pViewportState     = &viewportState;
    pipelineInfo.pRasterizationState= &rasterizer;
    pipelineInfo.pMultisampleState  = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState   = &colorBlending;
    pipelineInfo.pDynamicState      = &dynamicStateInfo;
    pipelineInfo.layout             = layout;
    pipelineInfo.renderPass         = VK_NULL_HANDLE; // using dynamic rendering — no render pass

    VkResult res = vkCreateGraphicsPipelines(
        m_device, m_pipelineCache, 1, &pipelineInfo, nullptr, &entry.pipeline);

    if (res != VK_SUCCESS)
    {
        Msg("! vk_PipelineCache::CompilePipeline — vkCreateGraphicsPipelines failed (VkResult=%d)", (int)res);
        entry.pipeline = VK_NULL_HANDLE;
    }
    else
    {
        Msg("* vk_PipelineCache: compiled new VkPipeline %p", (void*)entry.pipeline);
    }

    return entry;
}

VkPipelineLayout vk_PipelineCacheManager::GetLastBoundLayout(VkRecordContext* ctx) const
{
    return ctx->m_lastBoundLayout;
}

VkDescriptorSetLayout vk_PipelineCacheManager::GetLastBoundSetLayout(uint32_t set, VkRecordContext* ctx) const
{
    if (ctx->m_lastBoundEntry && set < ctx->m_lastBoundEntry->setLayouts.size())
        return ctx->m_lastBoundEntry->setLayouts[set];
    return VK_NULL_HANDLE;
}

const vk_PipelineCacheManager::PipelineEntry& vk_PipelineCacheManager::GetLastBoundEntry(VkRecordContext* ctx) const
{
    static const PipelineEntry dummy{};
    return ctx->m_lastBoundEntry ? *ctx->m_lastBoundEntry : dummy;
}
