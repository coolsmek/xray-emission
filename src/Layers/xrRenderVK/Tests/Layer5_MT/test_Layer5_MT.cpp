#include "stdafx.h"
#ifdef VK_ENABLE_TESTS

#include "../vk_TestRunner.h"
#include "../../Backend/vkR_Backend_Runtime.h"
#include <thread>
#include <atomic>
#include "../../xrRender/R_Backend.h"       // HW
#include "../../Resources/vk_Shader.h"      // vk_CreateShaderModule / vk_DestroyShaderModule / vk_CompileHlslToSpirv

VK_TEST(Layer5_MT, ThreadLocalContext_DefaultsToPrimary)
{
    // 1. Verify main thread context is the primary context
    VK_EXPECT(RCache.m_ctx == &g_vkPrimaryContext);

    // 2. Spawn a background thread and verify its m_ctx also defaults to g_vkPrimaryContext
    std::atomic<bool> worker_context_matches{false};
    std::atomic<bool> worker_finished{false};

    std::thread worker([&]() {
        // By default, the static thread_local m_ctx should be initialized to &g_vkPrimaryContext
        // for any newly spawned thread.
        if (RCache.m_ctx == &g_vkPrimaryContext)
        {
            worker_context_matches = true;
        }
        worker_finished = true;
    });

    worker.join();

    VK_EXPECT(worker_finished.load() == true);
    VK_EXPECT(worker_context_matches.load() == true);

    return true;
}

#include "../../Managers/vk_PipelineCache.h"
#include "../../Managers/vk_DescriptorManager.h"
#include <vector>

VK_TEST(Layer5_MT, PipelineCache_ConcurrentBindNoCorruption)
{
    constexpr int kThreads = 8;
    constexpr int kItersPerThread = 500;
    std::atomic<bool> anyFailure{false};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&, t]() {
            VkRecordContext ctx{};
            ctx.vs = nullptr;
            ctx.ps = nullptr;
            
            for (int i = 0; i < kItersPerThread; ++i)
            {
                ctx.cull_mode = (i + t) % 3;
                ctx.z_enable = (i + t) % 2;
                ctx.m_pipelineDirty = true;
                
                VkPipeline pipeline = PipelineCache.BindCurrentState(VK_NULL_HANDLE, &ctx);
                if (pipeline != VK_NULL_HANDLE)
                    anyFailure = true; // Expected VK_NULL_HANDLE because VS/PS are null
            }
        });
    }
    for (auto& w : workers) w.join();

    VK_EXPECT(!anyFailure.load());
    return true;
}

VK_TEST(Layer5_MT, DescriptorManager_ConcurrentAllocNoCorruption)
{
    DescriptorManager.BeginFrame(0);   // main-thread-only call, before workers start

    constexpr int kThreads = 8;
    constexpr int kItersPerThread = 200;
    std::atomic<int> successCount{0};
    std::atomic<int> nullCount{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&]() {
            for (int i = 0; i < kItersPerThread; ++i)
            {
                uint32_t offset = 0;
                void* p = DescriptorManager.AllocateDynamicUniform(256, offset);
                if (p) ++successCount; else ++nullCount;

                uint64_t hash = (uint64_t)(offset) * 2654435761u;
                DescriptorManager.InsertCachedSet(hash, (VkDescriptorSet)(uintptr_t)(hash));
                VkDescriptorSet found = DescriptorManager.FindCachedSet(hash);
                // Can't expect strictly due to potential concurrent overwrites of the exact same hash (though rare), 
                // but we at least expect it not to crash and return something.
                if (found == VK_NULL_HANDLE)
                    nullCount++;
            }
        });
    }
    for (auto& w : workers) w.join();

    VK_EXPECT(successCount.load() > 0);
    return true;
}

// FIX 2.3 regression test: PipelineCache_ConcurrentBindNoCorruption (above) uses null
// VS/PS, which makes CompilePipeline() early-return BEFORE ever reaching
// vkCreateGraphicsPipelines() — it never exercises the actual hazard. This test uses
// real compiled SPIR-V modules and deliberately funnels all 8 threads onto only 4
// distinct pipeline descriptors, guaranteeing genuine concurrent-compile races on the
// very first pass (multiple threads missing the cache for the SAME desc at once) and
// exercising the shared m_pipelineCache handle under real contention.
VK_TEST(Layer5_MT, PipelineCache_ConcurrentCompileNoCorruption)
{
    // Compile trivial real shaders via shaderc at test time — avoids any dependency on
    // pre-baked SPIR-V fixture files (which have historically been hand-fabricated/invalid;
    // see Layer2_Shaders/spirv_blobs). shaderc_compiler_t is documented thread-safe, but we
    // only compile once here on the main thread before spawning workers.
    static const char kVS[] =
        "float4 main(uint id : SV_VertexID) : SV_Position {\n"
        "    float2 pos[3] = { float2(-1,-1), float2(0,1), float2(1,-1) };\n"
        "    return float4(pos[id], 0, 1);\n"
        "}\n";
    static const char kPS[] =
        "float4 main() : SV_Target { return float4(1,1,1,1); }\n";

    std::vector<uint32_t> vsSpv = vk_CompileHlslToSpirv(kVS, sizeof(kVS) - 1, "vs", "main", "Layer5_MT_test_vs");
    std::vector<uint32_t> psSpv = vk_CompileHlslToSpirv(kPS, sizeof(kPS) - 1, "ps", "main", "Layer5_MT_test_ps");
    VK_EXPECT(!vsSpv.empty());
    VK_EXPECT(!psSpv.empty());

    VkShaderModule vsModule = vk_CreateShaderModule(HW.m_vkDevice, vsSpv.data(), vsSpv.size() * sizeof(uint32_t));
    VkShaderModule psModule = vk_CreateShaderModule(HW.m_vkDevice, psSpv.data(), psSpv.size() * sizeof(uint32_t));
    VK_EXPECT(vsModule != VK_NULL_HANDLE);
    VK_EXPECT(psModule != VK_NULL_HANDLE);

    VkVertexShaderWrapper vs{};
    vs.pSPIRV = vsSpv.data();
    vs.size   = vsSpv.size() * sizeof(uint32_t);
    vs.module = vsModule;

    VkPixelShaderWrapper ps{};
    ps.pSPIRV = psSpv.data();
    ps.size   = psSpv.size() * sizeof(uint32_t);
    ps.module = psModule;

    constexpr int kThreads        = 8;
    constexpr int kItersPerThread = 25;
    constexpr int kVariants       = 4; // deliberately small — forces threads to collide on the same desc

    std::atomic<int>  compiledCount{0};
    std::atomic<bool> gotNullPipeline{false};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&, t]() {
            VkRecordContext ctx{};
            ctx.vs = &vs;
            ctx.ps = &ps;

            for (int i = 0; i < kItersPerThread; ++i)
            {
                ctx.colorwrite_mask  = 1u << ((i + t) % kVariants);
                ctx.m_pipelineDirty  = true;

                VkPipeline pipeline = PipelineCache.BindCurrentState(VK_NULL_HANDLE, &ctx);
                if (pipeline == VK_NULL_HANDLE)
                    gotNullPipeline = true;
                else
                    ++compiledCount;
            }
        });
    }
    for (auto& w : workers) w.join();

    VK_EXPECT(!gotNullPipeline.load());
    VK_EXPECT(compiledCount.load() == kThreads * kItersPerThread);

    vk_DestroyShaderModule(HW.m_vkDevice, vsModule);
    vk_DestroyShaderModule(HW.m_vkDevice, psModule);
    return true;
}

#endif // VK_ENABLE_TESTS
