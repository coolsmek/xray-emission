#include "stdafx.h"
#pragma hdrstop

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
#include "../xrRenderDX10/dx10BufferUtils.h"
#endif	//	USE_DX11

CBackend RCache;

// Create Quad-IB
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)

void CBackend::RestoreQuadIBData()
{
}

void CBackend::CreateQuadIB()
{
#ifdef USE_VK
    Msg("* [VK] CreateQuadIB: entering");
#endif
	constexpr u32 dwTriCount = 4 * (4096 * 8);
	constexpr u32 dwIdxCount = dwTriCount * 2 * 3;
	static u16 IndexBuffer[dwIdxCount];
	u16* Indices = IndexBuffer;

    {
        int Cnt = 0;
        int ICnt = 0;
        for (int i = 0; i < dwTriCount; i++)
        {
            Indices[ICnt++] = u16(Cnt + 0);
            Indices[ICnt++] = u16(Cnt + 1);
            Indices[ICnt++] = u16(Cnt + 2);

            Indices[ICnt++] = u16(Cnt + 3);
            Indices[ICnt++] = u16(Cnt + 2);
            Indices[ICnt++] = u16(Cnt + 1);

            Cnt += 4;
        }
    }

	D3D_BUFFER_DESC desc;
	desc.ByteWidth = dwIdxCount * 2;
	desc.Usage = D3D_USAGE_DEFAULT;
	desc.BindFlags = D3D_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	D3D_SUBRESOURCE_DATA subData;
	subData.pSysMem = IndexBuffer;

    R_CHK(HW.pDevice->CreateBuffer ( &desc, &subData, &QuadIB));
#ifdef USE_VK
    Msg("* [VK] CreateQuadIB: QuadIB=%p, buffer=%p",
    (void*)QuadIB, QuadIB ? (void*)QuadIB->buffer : nullptr);
#endif
    HW.stats_manager.increment_stats_ib(QuadIB);
}

#else	//	USE_DX11

void CBackend::RestoreQuadIBData()
{
	const u32 dwTriCount = 4 * 1024;
	u16* Indices = 0;
	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
	R_CHK(QuadIB->Unlock());
}

void CBackend::CreateQuadIB()
{
	const u32 dwTriCount = 4 * 1024;
	const u32 dwIdxCount = dwTriCount * 2 * 3;

#if defined(USE_VK)
	// In Vulkan, static buffers are created DEVICE_LOCAL and VkBufferWrapper::Lock is a stub
	// that returns NULL. We must pre-build the array on the CPU and upload it directly.
	xr_vector<u16> IndicesVec(dwIdxCount);
	u16* Indices = IndicesVec.data();
#else
	u16* Indices = 0;
	u32 dwUsage = D3DUSAGE_WRITEONLY;
	if (HW.Caps.geometry.bSoftware) dwUsage |= D3DUSAGE_SOFTWAREPROCESSING;
	R_CHK(HW.pDevice->CreateIndexBuffer (dwIdxCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&QuadIB,NULL));
#endif
	HW.stats_manager.increment_stats_ib(QuadIB);

#if !defined(USE_VK)
	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
#endif
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
#if defined(USE_VK)
	QuadIB = ResourceManager.CreateIndexBuffer(Indices, dwIdxCount * 2);
#else
	R_CHK(QuadIB->Unlock());
#endif
}

#endif	//	USE_DX11

// Device dependance
void CBackend::OnDeviceCreate()
{
	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: entering CreateQuadIB"); xrLogger::FlushLog(); }
	CreateQuadIB();

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	// Under Vulkan, CreateQuadIB uses CreateBuffer which is fully supported now.
#endif

#if defined(USE_VK)
    extern VkRecordContext g_vkPrimaryContext;
    m_ctx = &g_vkPrimaryContext;
#endif

	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: entering Vertex.Create"); xrLogger::FlushLog(); }
	// streams
	Vertex.Create();
	
	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: entering Index.Create"); xrLogger::FlushLog(); }
	Index.Create();

	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: entering InitDebugDraw"); xrLogger::FlushLog(); }
	InitDebugDraw();

	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: entering Invalidate"); xrLogger::FlushLog(); }
	// invalidate caching
	Invalidate();

	if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CBackend::OnDeviceCreate: done"); xrLogger::FlushLog(); }
}

void CBackend::OnDeviceDestroy()
{
	// streams
	Index.Destroy();
	Vertex.Destroy();

#ifdef USE_VK
	// Under Vulkan, the ResourceManager owns the buffer allocations (FreeAll),
	// but we must null the QuadIB handle to avoid dangling references.
	QuadIB = nullptr;
#else
	// Quad
	HW.stats_manager.decrement_stats_ib(QuadIB);
	_RELEASE(QuadIB);
#endif

	DestroyDebugDraw();
}
