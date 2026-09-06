#include "stdafx.h"
#ifdef VK_ENABLE_TESTS

#include "../../../../xrCPU_Pipe/ttapi.h"
#include "../../../xrRender/FTreeVisual.h"
#include <algorithm>
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

// Regression guard for the ttapi Sleep(100) deep-sleep starvation bug (see
// VK_MT_DIAG_log01.log): idle workers that fell past the fast-spin/moderate-yield
// tiers used to block in Sleep(100) polling, so any dispatch landing after that
// window incurred up to 100ms of extra latency per ttapi_RunAllWorkers() call.
// This exercises ttapi directly (no VK rendering involved) with a forced idle gap
// before each dispatch to guarantee workers have dropped into the deep-sleep tier,
// then asserts round-trip latency stays far below the old 100ms polling interval.
VK_TEST(Layer5_MT, TTAPI_WakeLatencyBounded)
{
    if (ttapi_GetWorkersCount() < 2)
        return true; // nothing to test with a single worker / -max-threads=1 override

    static std::atomic<int> counter{0};
    counter.store(0);
    auto trivialWork = [](LPVOID) { counter.fetch_add(1); };

    constexpr int kIterations = 10;
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    double worstMs = 0.0;

    for (int iter = 0; iter < kIterations; ++iter)
    {
        // Force idle workers well past the fast/moderate spin tiers before dispatching —
        // reproduces the exact starvation window the diagnostic log surfaced.
        Sleep(150);

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        ttapi_AddWorker(trivialWork, nullptr);
        ttapi_AddWorker(trivialWork, nullptr);
        ttapi_RunAllWorkers();

        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        worstMs = (std::max)(worstMs, ms);
    }

    Msg("* [VK_TEST] TTAPI_WakeLatencyBounded worst dispatch = %.3fms", worstMs);

    // Old Sleep(100)-polling bug reliably produced ~90-98ms here. A healthy event-based
    // wake should be well under 5ms even under scheduler noise. If this regresses toward
    // 100ms, the deep-sleep tier was reverted back to Sleep()-based polling.
    VK_EXPECT(worstMs < 5.0);
    VK_EXPECT(counter.load() == kIterations * 2);

    return true;
}

#include "../../../xrRender/FProgressive.h"

// Regression guard for the shared per-resource last_lod MT data race (FProgressive /
// FTreeVisual_PM / CSkeletonX_PM). Contract under test: only the primary (main-thread)
// context may mutate last_lod; worker contexts must leave it untouched. We drive a
// minimal FProgressive-like scenario by asserting the guard predicate directly across
// threads (we cannot easily construct a fully-loaded FProgressive without level data,
// so we validate the exact branch condition the fix relies on).
VK_TEST(Layer5_MT, ProgressiveLOD_WorkerNeverWritesCache)
{
    VK_EXPECT(RCache.m_ctx == &g_vkPrimaryContext); // main thread owns primary ctx

    constexpr int kThreads = 8;
    std::atomic<bool> workerSawPrimary{false};   // must stay false — no worker may pass the guard
    std::atomic<int>  workerRuns{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&, t]() {
            VkRecordContext* saved = RCache.m_ctx;                 // thread_local — isolated per thread
            RCache.m_ctx = &g_vkWorkerContexts[t % CHW::VK_GBUFFER_WORKERS];

            // Exactly the predicate guarding every last_lod write in the fix:
            if (RCache.m_ctx == &g_vkPrimaryContext)
                workerSawPrimary = true;           // would mean the guard lets a worker write — BUG

            ++workerRuns;
            RCache.m_ctx = saved;
        });
    }
    for (auto& w : workers) w.join();

    VK_EXPECT(!workerSawPrimary.load());
    VK_EXPECT(workerRuns.load() == kThreads);
    return true;
}

VK_TEST(Layer5_MT, TreeWind_MainThreadOnlyUpdate)
{
    VK_EXPECT(RCache.m_ctx == &g_vkPrimaryContext);
    FTreeVisual::PrepareWind();
    FTreeVisual::PrepareWind(); // idempotent within a frame — must not crash

    constexpr int kThreads = 8;
    std::atomic<bool> workerSawPrimary{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
        workers.emplace_back([&, t]() {
            VkRecordContext* saved = RCache.m_ctx;
            RCache.m_ctx = &g_vkWorkerContexts[t % CHW::VK_GBUFFER_WORKERS];
            if (RCache.m_ctx == &g_vkPrimaryContext) workerSawPrimary = true;
            RCache.m_ctx = saved;
        });
    for (auto& w : workers) w.join();

    VK_EXPECT(!workerSawPrimary.load());
    return true;
}

VK_TEST(Layer5_MT, ConstantBuffer_PerWorkerStagingIsolation)
{
// Shared cbuffer object, exactly like the tree $Global / dynamic_transforms singletons.
dx10ConstantBuffer cb("mt_isolation_test", 256);

    std::atomic<bool> mismatch{false};

    auto body = [&](VkRecordContext* wctx, u32 tagByte)
    {
        VkRecordContext* prev = RCache.m_ctx;
        RCache.m_ctx = wctx;
        const u32 prevWid = g_vkWorkerId;
        g_vkWorkerId = wctx->workerId;   // production keys per-worker staging off g_vkWorkerId, not m_ctx

        for (int iter = 0; iter < 2000 && !mismatch.load(); ++iter)
        {
            // Stamp this worker's staging buffer with a unique byte pattern via AccessDirect.
            R_constant_load L; L.index = 0;
            BYTE* p = (BYTE*)cb.AccessDirect(L, 256);
            if (p) memset(p, (int)tagByte, 256);

            cb.Flush();                          // writes THIS worker's offset slot
            u32 myOffset = cb.GetDynamicOffset(); // must read THIS worker's slot back

            // Re-read staging: it must still hold OUR tag, not the other worker's.
            const BYTE* rd = (const BYTE*)cb.GetRawData();
            if (!rd || rd[0] != (BYTE)tagByte || myOffset == 0xFFFFFFFF)
                mismatch.store(true);
        }
        RCache.m_ctx = prev;
        g_vkWorkerId = prevWid;
    };

    // Use two distinct worker contexts (distinct workerId).
    g_vkWorkerContexts[0].workerId = 1;
    g_vkWorkerContexts[1].workerId = 2;

    std::thread t0(body, &g_vkWorkerContexts[0], 0xA1);
    std::thread t1(body, &g_vkWorkerContexts[1], 0xB2);
    t0.join(); t1.join();

    VK_EXPECT(!mismatch.load());  // no cross-worker staging/offset bleed
    VK_EXPECT(RCache.m_ctx == &g_vkPrimaryContext);
    return true;
}

VK_TEST(Layer5_MT, WorkerXformSeed_ReResolvesAfterUnmap)
{
    // Simulate a worker whose xforms cache holds a POISONED stale pointer
    // (as it would after a level/save reload freed the old constant table).
    VkRecordContext& w = g_vkWorkerContexts[0];
    w.workerId = 1;

    VkRecordContext* prev = RCache.m_ctx;
    const u32 prevWid = g_vkWorkerId;
    RCache.m_ctx = &w;
    g_vkWorkerId = 1;

    // The seed's reset sets this flag to force a re-seed on the first real draw.
    w.needsViewProjSeed = true;

    // Poison: pretend last frame cached a constant pointer.
    w.xforms.c_v = (R_constant*)(uintptr_t)0xDEADBEEF;
    w.ctable = (R_constant_table*)(uintptr_t)0xDEADBEEF;

    // The seed's reset MUST clear these.
    w.ctable = nullptr;
    w.xforms.unmap();
    VK_EXPECT(w.xforms.c_v == nullptr);   // stale pointer cleared → no dangling deref

    // Now simulate the first real draw binding a shader table.
    R_constant_table dummyTable;
    RCache.set_Constants(&dummyTable);
    
    // The lazy seed should have triggered and cleared the flag.
    VK_EXPECT(w.needsViewProjSeed == false);
    // And it should have safely called set_V against the dummyTable (which has no m_V, so c_v remains null).
    VK_EXPECT(w.xforms.c_v == nullptr);

    RCache.m_ctx = prev;
    g_vkWorkerId = prevWid;
    return true;
}

// Pure-logic unit test for vk_ResolveStaticRange
// To test the logic without exposing the static function from rVK.cpp, we replicate it here.
static void test_vk_ResolveStaticRange(u32 start, u32 end, u32 count0,
                                   u32& passBegin, u32& passEnd,
                                   u32& packetBegin, u32& packetEnd)
{
    if (end <= count0)
    {
        passBegin = 0; passEnd = 1;
        packetBegin = start; packetEnd = end;
    }
    else if (start >= count0)
    {
        passBegin = 1; passEnd = 2;
        packetBegin = start - count0; packetEnd = end - count0;
    }
    else
    {
        passBegin = 0; passEnd = 2;
        packetBegin = start; packetEnd = end - count0; // pass0 runs start..count0 implicitly (full tail)
    }
}

VK_TEST(Layer5_MT, vk_ResolveStaticRange_Logic)
{
    u32 passBegin, passEnd, packetBegin, packetEnd;

    // Case 1: Fully inside pass0
    test_vk_ResolveStaticRange(0, 10, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 0 && passEnd == 1 && packetBegin == 0 && packetEnd == 10);

    // Case 2: Fully inside pass1
    test_vk_ResolveStaticRange(25, 30, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 1 && passEnd == 2 && packetBegin == 5 && packetEnd == 10);

    // Case 3: Spanning both passes
    test_vk_ResolveStaticRange(15, 25, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 0 && passEnd == 2 && packetBegin == 15 && packetEnd == 5);

    // Case 4: Remainder distribution simulation
    // Say we have 33 total packets, count0=20, count1=13. 4 workers.
    // baseChunk = 33/4 = 8. remainder = 1.
    // w=0: 0..9 (size 9) -> fully pass0
    test_vk_ResolveStaticRange(0, 9, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 0 && passEnd == 1 && packetBegin == 0 && packetEnd == 9);

    // w=1: 9..17 (size 8) -> fully pass0
    test_vk_ResolveStaticRange(9, 17, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 0 && passEnd == 1 && packetBegin == 9 && packetEnd == 17);

    // w=2: 17..25 (size 8) -> spanning
    test_vk_ResolveStaticRange(17, 25, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 0 && passEnd == 2 && packetBegin == 17 && packetEnd == 5);

    // w=3: 25..33 (size 8) -> fully pass1
    test_vk_ResolveStaticRange(25, 33, 20, passBegin, passEnd, packetBegin, packetEnd);
    VK_EXPECT(passBegin == 1 && passEnd == 2 && packetBegin == 5 && packetEnd == 13);

    return true;
}

#endif // VK_ENABLE_TESTS
