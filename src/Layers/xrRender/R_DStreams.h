#ifndef r_DStreamsH
#define r_DStreamsH
#pragma once

enum
{
	LOCKFLAGS_FLUSH = D3DLOCK_DISCARD,
	LOCKFLAGS_APPEND = D3DLOCK_NOOVERWRITE
};

class ECORE_API _VertexStream
{
private :
	ID3DVertexBuffer* pVB;
	u32 mSize; // size in bytes
	u32 mPosition; // position in bytes (within current frame's region)
	u32 mDiscardID; // ID of discard - usually for caching
	// Per-frame-slot sub-range (Option A, VK ring isolation)
	u32 mFrameBase;       // byte offset of current frame's region
	u32 mFrameRegionSize; // bytes per frame region
public:
	ID3DVertexBuffer* old_pVB;
#ifdef DEBUG
	u32							dbg_lock;
#endif
private:
	void _clear();
public:
	void Create();
	void Destroy();
	void reset_begin();
	void reset_end();

	// Call once per frame after the fence for frameSlot has been waited.
	// Resets mPosition to the start of frameSlot's exclusive sub-range.
	void FrameReset(u32 frameSlot, u32 framesInFlight);

	IC ID3DVertexBuffer* Buffer() { return pVB; }
	IC u32 DiscardID() { return mDiscardID; }
	IC void Flush() { mPosition = mFrameBase + mFrameRegionSize; }

	void* Lock(u32 vl_Count, u32 Stride, u32& vOffset);
	void Unlock(u32 Count, u32 Stride);
	u32 GetSize() { return mSize; }

	_VertexStream();
	~_VertexStream() { Destroy(); };
};

class ECORE_API _IndexStream
{
private :
	ID3DIndexBuffer* pIB;
	u32 mSize; // real size (usually mCount, aligned on 512b boundary)
	u32 mPosition;
	u32 mDiscardID;
	// Per-frame-slot sub-range (Option A, VK ring isolation)
	u32 mFrameBase;
	u32 mFrameRegionSize;
public:
	ID3DIndexBuffer* old_pIB;
private:
	void _clear()
	{
		pIB = NULL;
		mSize = 0;
		mPosition = 0;
		mDiscardID = 0;
		mFrameBase = 0;
		mFrameRegionSize = 0;
	}

public:
	void Create();
	void Destroy();
	void reset_begin();
	void reset_end();

	// Call once per frame after the fence for frameSlot has been waited.
	void FrameReset(u32 frameSlot, u32 framesInFlight);

	IC ID3DIndexBuffer* Buffer() { return pIB; }
	IC u32 DiscardID() { return mDiscardID; }
	void Flush() { mPosition = mFrameBase + mFrameRegionSize; }

	u16* Lock(u32 Count, u32& vOffset);
	void Unlock(u32 RealCount);

	_IndexStream() { _clear(); };
	~_IndexStream() { Destroy(); };
};
#endif
