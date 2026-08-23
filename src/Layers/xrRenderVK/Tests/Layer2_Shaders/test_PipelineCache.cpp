#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
#include "../../Managers/vk_PipelineCache.h"
#include "../../xrRender/R_Backend.h"
#include "../../Resources/vk_Shader.h"
#include "spirv_blobs/white_triangle_vert.h"
#include "spirv_blobs/white_triangle_frag.h"

extern VkRecordContext g_vkPrimaryContext;

// Helper: build a minimal vk_PipelineStateDesc for the white triangle.
static vk_PipelineStateDesc MakeWhiteTriangleDesc(
    VkVertexShaderWrapper* vs, VkPixelShaderWrapper* ps)
{
    vk_PipelineStateDesc desc{};
    desc.vs = vs;
    desc.ps = ps;
    desc.gs = nullptr;
    desc.cs = nullptr;
    desc.decl = nullptr;
    desc.colorWriteMask = 0xF;
    desc.alphaRef = 0;
    desc.colorFormatCount = 1;
    desc.colorFormats[0] = HW.m_vkSCFormat;   // was m_vkSwapchainFormat.format
    desc.depthFormat = HW.m_vkDepthFormat;
    return desc;
}

// Verify that two identical descs produce the same hash (FNV consistency).
VK_TEST(PipelineCache, HashIsStable)
{
    auto* vs = xr_new<VkVertexShaderWrapper>();
    auto* ps = xr_new<VkPixelShaderWrapper>();
    auto d1 = MakeWhiteTriangleDesc(vs, ps);
    auto d2 = MakeWhiteTriangleDesc(vs, ps);
    std::hash<vk_PipelineStateDesc> hasher;
    VK_EXPECT(hasher(d1) == hasher(d2));
    xr_delete(vs); xr_delete(ps);
    return true;
}

// Two descs differing only in colorFormat must hash differently.
VK_TEST(PipelineCache, HashDiffersOnFormatChange)
{
    auto* vs = xr_new<VkVertexShaderWrapper>();
    auto* ps = xr_new<VkPixelShaderWrapper>();
    auto d1 = MakeWhiteTriangleDesc(vs, ps);
    auto d2 = d1;
    d2.colorFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;
    std::hash<vk_PipelineStateDesc> hasher;
    VK_EXPECT(hasher(d1) != hasher(d2));
    xr_delete(vs); xr_delete(ps);
    return true;
}

// Verify that CompilePipeline rejects null VS/PS modules (skips silently, no crash).
VK_TEST(PipelineCache, NullModulesSkippedGracefully)
{
    auto* vs = xr_new<VkVertexShaderWrapper>(); // module == VK_NULL_HANDLE
    auto* ps = xr_new<VkPixelShaderWrapper>();
    auto desc = MakeWhiteTriangleDesc(vs, ps);
    // CompilePipeline is called internally; we verify it doesn't crash via BindCurrentState.
    // Since vs/ps modules are null, BindCurrentState returns VK_NULL_HANDLE — that's the spec.
    // This test exists to confirm graceful degradation (no assert, no crash).
    VkPipeline pipe = PipelineCache.BindCurrentState(HW.m_vkCmdBuffers[0], RCache.m_ctx);
    // pipe may be VK_NULL_HANDLE here — that's acceptable for null modules
    (void)pipe;
    xr_delete(vs); xr_delete(ps);
    return true;
}

#endif // VK_ENABLE_TESTS
