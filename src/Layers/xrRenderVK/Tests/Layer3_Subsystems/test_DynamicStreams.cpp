#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
// vk_DynamicStreams.h is the Phase 6b new file.
// #include "../../Resources/vk_DynamicStreams.h"

// Forward declare the placeholder class and functions
class vk_DynamicVertexStream
{
public:
    void Initialize(size_t totalSize, size_t vertexStride);
    bool Lock(uint32_t vertexCount, void** outPtr);
    void Unlock(uint32_t actualCount);
    void BeginFrame();
    void Destroy();
};

struct UIVertex { float x, y, z; uint32_t color; float u, v; }; // 24 bytes

// Lock must return non-null pointer within the ring buffer.
VK_TEST(DynamicStreams, LockReturnsMappedPointer)
{
    vk_DynamicVertexStream stream;
    stream.Initialize(512 * sizeof(UIVertex), sizeof(UIVertex));

    UIVertex* verts = nullptr;
    bool ok = stream.Lock(16, reinterpret_cast<void**>(&verts));
    VK_EXPECT(ok);
    VK_EXPECT(verts != nullptr);

    // Write 16 vertices — ASAN catches out-of-bounds
    for (int i = 0; i < 16; ++i)
        verts[i] = {(float)i, 0.f, 0.f, 0xFFFFFFFF, 0.f, 0.f};

    stream.Unlock(16);
    stream.Destroy();
    return true;
}

// Lock twice in one frame: offsets must not overlap.
VK_TEST(DynamicStreams, TwoLocksNoOverlap)
{
    vk_DynamicVertexStream stream;
    stream.Initialize(512 * sizeof(UIVertex), sizeof(UIVertex));

    UIVertex* verts1 = nullptr;
    UIVertex* verts2 = nullptr;
    stream.Lock(8, reinterpret_cast<void**>(&verts1));
    stream.Unlock(8);
    stream.Lock(8, reinterpret_cast<void**>(&verts2));
    stream.Unlock(8);

    // Must not alias — different pointer regions
    VK_EXPECT(verts1 != nullptr && verts2 != nullptr);
    VK_EXPECT(verts2 >= verts1 + 8 || verts2 < verts1);

    stream.Destroy();
    return true;
}

// Ring wrap: after filling the ring, the next lock must succeed (wraps to start).
VK_TEST(DynamicStreams, RingWrapSucceeds)
{
    const uint32_t kCapacity = 32; // Only 32 verts in this ring
    vk_DynamicVertexStream stream;
    stream.Initialize(kCapacity * sizeof(UIVertex), sizeof(UIVertex));

    // Fill it exactly
    UIVertex* verts = nullptr;
    stream.Lock(kCapacity, reinterpret_cast<void**>(&verts));
    stream.Unlock(kCapacity);

    // Simulate advancing to next frame (ring resets)
    stream.BeginFrame();

    // Must succeed again from the beginning
    bool ok = stream.Lock(8, reinterpret_cast<void**>(&verts));
    VK_EXPECT(ok);
    VK_EXPECT(verts != nullptr);
    stream.Unlock(8);

    stream.Destroy();
    return true;
}

// Zero-size lock is safe (returns valid pointer, Unlock(0) is no-op).
VK_TEST(DynamicStreams, ZeroSizeLockSafe)
{
    vk_DynamicVertexStream stream;
    stream.Initialize(64 * sizeof(UIVertex), sizeof(UIVertex));
    void* p = nullptr;
    bool ok = stream.Lock(0, &p);
    // Implementation may return true with p=non-null (at current offset), or
    // return false — either is acceptable; must not crash.
    stream.Unlock(0);
    stream.Destroy();
    return true;
}

#endif // VK_ENABLE_TESTS
