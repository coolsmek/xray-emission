#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"
#include "../xrRender/R_DStreams.h"
#include "Resources/vk_ResourceManager.h"

int rsDVB_Size = 32768;
int rsDIB_Size = 8192;

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
    mPeakUsage = 0;
    mWrapCountThisFrame = 0;
    mCallIndexThisFrame = 0; // NEW
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
    mPeakUsage = 0;
    mWrapCountThisFrame = 0;
    mCallIndexThisFrame = 0; // NEW

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

void _VertexStream::Flush()
{
#ifdef DEBUG
    // Under Option A (per-frame isolated ring partitions), mid-frame Flush() 
    // is actively harmful as it would force a wrap to the beginning of the frame's 
    // region, overwriting data from earlier this same frame that hasn't been submitted yet.
    // Msg("~ [VK] _VertexStream::Flush() called - intentionally ignoring.");
#endif
}

// ── Per-frame sub-range reset (Option A ring isolation) ───────────────────────
// Called once per frame AFTER vkWaitForFences(frameSlot) so we know this
// region of the buffer is no longer in use by the GPU.
void _VertexStream::FrameReset(u32 frameSlot, u32 framesInFlight)
{
    if (strstr(Core.Params, "-vk_dvb_stats"))
    {
        Msg("VK_DVB_STATS Frame %d [VertexStream]: peak_usage = %.2f%% (%d / %d bytes), wrap_count = %d",
            Device.dwFrame, 
            mFrameRegionSize > 0 ? (float)mPeakUsage / mFrameRegionSize * 100.f : 0.f, 
            mPeakUsage, mFrameRegionSize, mWrapCountThisFrame);
    }
    
    mPeakUsage = 0;
    mWrapCountThisFrame = 0;
    mCallIndexThisFrame = 0; // NEW

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

    mCallIndexThisFrame++; // NEW: count this call regardless of wrap

    if ((bytePos + bytes_need) > regionEnd)
    {
        if (strstr(Core.Params, "-vk_dvb_stats"))
        {
            Msg("VK_DVB_WRAP Frame %d [VertexStream] call #%d: vl_Count=%d Stride=%d bytes_need=%d "
                "mPosition_before=%d mFrameBase=%d regionEnd=%d room_left=%d",
                Device.dwFrame, mCallIndexThisFrame, vl_Count, Stride, bytes_need,
                mPosition, mFrameBase, regionEnd, (int)regionEnd - (int)bytePos);
        }

        // Wrap within this frame's sub-range; round the base UP to a stride boundary.
        bytePos = ((mFrameBase + Stride - 1) / Stride) * Stride;
        mDiscardID++;
        mWrapCountThisFrame++;
    }

    u32 currentUsage = (bytePos + bytes_need) - mFrameBase;
    if (currentUsage > mPeakUsage) mPeakUsage = currentUsage;

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
    mPeakUsage = 0;
    mWrapCountThisFrame = 0;
    mCallIndexThisFrame = 0; // NEW
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

void _IndexStream::Flush()
{
#ifdef DEBUG
    // Under Option A, mid-frame Flush() is actively harmful. See _VertexStream::Flush.
#endif
}

// ── Per-frame sub-range reset for index stream ────────────────────────────────
void _IndexStream::FrameReset(u32 frameSlot, u32 framesInFlight)
{
    if (strstr(Core.Params, "-vk_dvb_stats"))
    {
        Msg("VK_DVB_STATS Frame %d [IndexStream]: peak_usage = %.2f%% (%d / %d bytes), wrap_count = %d",
            Device.dwFrame, 
            mFrameRegionSize > 0 ? (float)mPeakUsage / mFrameRegionSize * 100.f : 0.f, 
            mPeakUsage, mFrameRegionSize, mWrapCountThisFrame);
    }
    
    mPeakUsage = 0;
    mWrapCountThisFrame = 0;
    mCallIndexThisFrame = 0; // NEW

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

    mCallIndexThisFrame++; // NEW

    if ((bytePos + bytes_need) > regionEnd)
    {
        if (strstr(Core.Params, "-vk_dvb_stats"))
        {
            Msg("VK_DVB_WRAP Frame %d [IndexStream] call #%d: Count=%d bytes_need=%d "
                "mPosition_before=%d mFrameBase=%d regionEnd=%d room_left=%d",
                Device.dwFrame, mCallIndexThisFrame, Count, bytes_need,
                mPosition, mFrameBase, regionEnd, (int)regionEnd - (int)bytePos);
        }

        // Wrap within this frame's sub-range
        bytePos = mFrameBase;
        mDiscardID++;
        mWrapCountThisFrame++;
    }

    u32 currentUsage = (bytePos + bytes_need) - mFrameBase;
    if (currentUsage > mPeakUsage) mPeakUsage = currentUsage;

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
