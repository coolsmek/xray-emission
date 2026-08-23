#ifndef	dx10BufferUtils_included
#define	dx10BufferUtils_included
#pragma once
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)

#if defined(USE_VK)
#include "../xrRenderVK/Resources/vk_ResourceManager.h"

namespace dx10BufferUtils
{
	inline HRESULT CreateVertexBuffer(ID3DVertexBuffer** ppBuffer, const void* pData, UINT DataSize, bool bImmutable = true)
	{
		*ppBuffer = ResourceManager.CreateVertexBuffer(pData, (VkDeviceSize)DataSize);
		return (*ppBuffer) ? S_OK : E_FAIL;
	}

	inline HRESULT CreateIndexBuffer(ID3DIndexBuffer** ppBuffer, const void* pData, UINT DataSize, bool bImmutable = true)
	{
		*ppBuffer = ResourceManager.CreateIndexBuffer(pData, (VkDeviceSize)DataSize);
		return (*ppBuffer) ? S_OK : E_FAIL;
	}

	inline HRESULT CreateConstantBuffer(ID3DBuffer** ppBuffer, UINT DataSize)
	{
		return S_OK;
	}

	inline void ConvertVertexDeclaration(const xr_vector<D3DVERTEXELEMENT9>& declIn,
	                              xr_vector<D3D_INPUT_ELEMENT_DESC>& declOut)
	{
	}
};
#else
namespace dx10BufferUtils
{
	HRESULT CreateVertexBuffer(ID3DVertexBuffer** ppBuffer, const void* pData, UINT DataSize, bool bImmutable = true);
	HRESULT CreateIndexBuffer(ID3DIndexBuffer** ppBuffer, const void* pData, UINT DataSize, bool bImmutable = true);
	HRESULT CreateConstantBuffer(ID3DBuffer** ppBuffer, UINT DataSize);
	void ConvertVertexDeclaration(const xr_vector<D3DVERTEXELEMENT9>& declIn,
	                              xr_vector<D3D_INPUT_ELEMENT_DESC>& declOut);
};
#endif

#endif	//	USE_DX10 || USE_DX11 || USE_VK
#endif	//	dx10BufferUtils_included
