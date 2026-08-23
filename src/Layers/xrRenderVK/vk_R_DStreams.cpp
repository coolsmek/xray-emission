#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"
#include "../xrRender/R_DStreams.h"
#include "Resources/vk_ResourceManager.h"

int rsDVB_Size = 4096;
int rsDIB_Size = 512;

// ── Vertex Stream ────────────────────────────────────────────────────────────

_VertexStream::_VertexStream() { _clear(); }

void _VertexStream::_clear()
{
    pVB = NULL;
    mSize = 0;
    mPosition = 0;
    mDiscardID = 0;
    mFrameBase = 0;
    mFrameRegionSize = 0;
}

#if defined(USE_VK)
// (Removed broken multi-buffering logic here)
#endif

void _VertexStream::Create()
{
    mSize = rsDVB_Size * 1024;

    // We create a host-visible, host-coherent buffer for streaming dynamic geometry
    pVB = ResourceManager.CreateDynamicVertexBuffer(mSize);
    R_ASSERT(pVB);

    mPosition = 0;
    mDiscardID = 0;
    mFrameBase = 0;
    mFrameRegionSize = mSize; // safe default until FrameReset is called

    Msg("* DVB created: %dK", mSize / 1024);
}

void _VertexStream::Destroy()
{
    if (pVB) {
        ResourceManager.DestroyDynamicBuffer(pVB);
        pVB = nullptr;
    }
    _clear();
}

// ── Per-frame sub-range reset (Option A ring isolation) ───────────────────────
// Called once per frame AFTER vkWaitForFences(frameSlot) so we know this
// region of the buffer is no longer in use by the GPU.
void _VertexStream::FrameReset(u32 frameSlot, u32 framesInFlight)
{
    if (!pVB || framesInFlight == 0) return;
    mFrameRegionSize = mSize / framesInFlight;
    mFrameBase       = frameSlot * mFrameRegionSize;
    mPosition        = mFrameBase;
}

void* _VertexStream::Lock(u32 vl_Count, u32 Stride, u32& vOffset)
{
    u32 bytes_need = vl_Count * Stride;
    R_ASSERT2((bytes_need <= mFrameRegionSize) && vl_Count,
              make_string("bytes_need = %d, mFrameRegionSize = %d, vl_Count = %d", bytes_need, mFrameRegionSize, vl_Count));

    u32 regionEnd = mFrameBase + mFrameRegionSize;

    // Round UP to the next stride boundary — never truncate down below mFrameBase.
    // Truncating (mPosition/Stride)*Stride can land below mFrameBase when mFrameBase
    // is not a multiple of Stride (e.g. mFrameBase=2097152, Stride=28 → truncation
    // gives 2097144, 8 bytes inside the previous frame-slot's region).
    u32 bytePos = ((mPosition + Stride - 1) / Stride) * Stride;

    if ((bytePos + bytes_need) > regionEnd)
    {
        // Wrap within this frame's sub-range; round the base UP to a stride boundary.
        bytePos = ((mFrameBase + Stride - 1) / Stride) * Stride;
        mDiscardID++;
    }

    mPosition = bytePos;
    vOffset   = bytePos / Stride; // exact: bytePos is a strict multiple of Stride
    return (BYTE*)pVB->mapped_ptr + bytePos;
}

void _VertexStream::Unlock(u32 Count, u32 Stride)
{
    u32 byteSize = Count * Stride;
    VkBufferWrapper* wrapper = (VkBufferWrapper*)pVB;
    if (wrapper && wrapper->alloc) {
        MemoryManager.FlushAllocation((VmaAllocation)wrapper->alloc, mPosition, byteSize);
    }
    mPosition += byteSize;
}

void _VertexStream::reset_begin() {
    old_pVB = pVB;
    Destroy();
}

void _VertexStream::reset_end() {
    Create();
}

// ── Index Stream ─────────────────────────────────────────────────────────────

#if defined(USE_VK)
// (Removed broken multi-buffering logic here)
#endif

void _IndexStream::Create()
{
    mSize = rsDIB_Size * 1024;

    pIB = ResourceManager.CreateDynamicIndexBuffer(mSize);
    R_ASSERT(pIB);

    mPosition = 0;
    mDiscardID = 0;
    mFrameBase = 0;
    mFrameRegionSize = mSize; // safe default until FrameReset is called
    Msg("* DIB created: %dK", mSize / 1024);
}

void _IndexStream::Destroy()
{
    if (pIB) {
        ResourceManager.DestroyDynamicBuffer(pIB);
        pIB = NULL;
    }
    _clear();
}

// ── Per-frame sub-range reset for index stream ────────────────────────────────
void _IndexStream::FrameReset(u32 frameSlot, u32 framesInFlight)
{
    if (!pIB || framesInFlight == 0) return;
    mFrameRegionSize = mSize / framesInFlight;
    mFrameBase       = frameSlot * mFrameRegionSize;
    mPosition        = mFrameBase;
}

u16* _IndexStream::Lock(u32 Count, u32& vOffset)
{
    u32 bytes_need = Count * 2;
    R_ASSERT2((bytes_need <= mFrameRegionSize) && Count,
              make_string("bytes_need = %d, mFrameRegionSize = %d, Count = %d", bytes_need, mFrameRegionSize, Count));

    u32 regionEnd = mFrameBase + mFrameRegionSize;

    // Align to 2-byte boundary, then guard against dipping below mFrameBase.
    u32 bytePos = ((mPosition + 1) / 2) * 2;
    if (bytePos < mFrameBase) bytePos = mFrameBase;

    if ((bytePos + bytes_need) > regionEnd)
    {
        // Wrap within this frame's sub-range
        bytePos = mFrameBase;
        mDiscardID++;
    }

    mPosition = bytePos;
    vOffset   = bytePos / 2; // index offset (not byte offset)
    return (u16*)((BYTE*)pIB->mapped_ptr + bytePos);
}

void _IndexStream::Unlock(u32 RealCount)
{
    u32 byteSize = RealCount * 2;
    VkBufferWrapper* wrapper = (VkBufferWrapper*)pIB;
    if (wrapper && wrapper->alloc) {
        MemoryManager.FlushAllocation((VmaAllocation)wrapper->alloc, mPosition, byteSize);
    }
    mPosition += byteSize;
    // No unmap needed, persistently mapped
}

void _IndexStream::reset_begin() {
    old_pIB = pIB;
    Destroy();
}

void _IndexStream::reset_end() {
    Create();
}
