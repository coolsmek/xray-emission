#include "stdafx.h"
#include "dx10ConstantBuffer.h"

#if !defined(USE_VK)
#include "dx10BufferUtils.h"
#endif
#include "../xrRender/dxRenderDeviceRender.h"

#if defined(USE_VK)
#include "../xrRenderVK/Managers/vk_DescriptorManager.h"
#endif

dx10ConstantBuffer::~dx10ConstantBuffer()
{
#if !defined(USE_VK)
	// _DeleteConstantBuffer is declared only under USE_DX10||USE_DX11, not USE_VK
	if (Device.m_pRender && DEV)
		DEV->_DeleteConstantBuffer(this);
#endif
	//	Flush();
	_RELEASE(m_pBuffer);
#if defined(USE_VK)
	for (u32 i = 0; i < VK_CB_MAX_WORKERS; ++i) xr_free(m_pBufferData[i]);
#else
	xr_free(m_pBufferData);
#endif
}

dx10ConstantBuffer::dx10ConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable)
#if defined(USE_VK)
	: m_uiBufferSize(0), m_uiMembersCRC(0), m_pBuffer(nullptr)
#else
	: m_bChanged(true), m_uiBufferSize(0), m_uiMembersCRC(0), m_pBuffer(nullptr), m_pBufferData(nullptr)
#endif
{
#if !defined(USE_VK)
	D3D_SHADER_BUFFER_DESC Desc;

	CHK_DX(pTable->GetDesc(&Desc));

	m_strBufferName._set(Desc.Name);
	m_eBufferType = Desc.Type;
	m_uiBufferSize = Desc.Size;

	//	Fill member list with variable descriptions
	m_MembersList.resize(Desc.Variables);
	m_MembersNames.resize(Desc.Variables);
	for (u32 i = 0; i < Desc.Variables; ++i)
	{
		ID3DShaderReflectionVariable* pVar;
		ID3DShaderReflectionType* pType;

		D3D_SHADER_VARIABLE_DESC var_desc;

		pVar = pTable->GetVariableByIndex(i);
		VERIFY(pVar);
		pType = pVar->GetType();
		VERIFY(pType);
		pType->GetDesc(&m_MembersList[i]);
		//	Buffers with the same layout can contain totally different members
		CHK_DX(pVar->GetDesc(&var_desc));
		m_MembersNames[i] = var_desc.Name;
	}

	m_uiMembersCRC = crc32(&m_MembersList[0], Desc.Variables * sizeof(m_MembersList[0]));

	R_CHK(dx10BufferUtils::CreateConstantBuffer(&m_pBuffer, Desc.Size));
	VERIFY(m_pBuffer);
	m_pBufferData = xr_malloc(Desc.Size);
	VERIFY(m_pBufferData);
#else
	// VK: this constructor is never called in production code.
	// Constant buffers in VK are populated via SPIR-V reflection, not DX10 reflection.
	(void)pTable;
	for (u32 i = 0; i < VK_CB_MAX_WORKERS; ++i)
	{
		m_pBufferData[i]     = nullptr;
		m_bChanged[i]        = true;
		m_vkDynamicOffset[i] = 0;
		m_vkFlushFrame[i]    = 0xFFFFFFFF;
	}
#endif
}

#if defined(USE_VK)
dx10ConstantBuffer::dx10ConstantBuffer(const char* name, u32 size)
	: m_uiBufferSize(size), m_uiMembersCRC(0), m_pBuffer(nullptr)
{
	m_strBufferName._set(name);
	m_eBufferType = 0; // D3D11_CT_CBUFFER equivalent

	for (u32 i = 0; i < VK_CB_MAX_WORKERS; ++i)
	{
		m_pBufferData[i] = xr_malloc(size);
		VERIFY(m_pBufferData[i]);
		ZeroMemory(m_pBufferData[i], size);
		m_bChanged[i]       = true;
		m_vkDynamicOffset[i] = 0;
		m_vkFlushFrame[i]    = 0xFFFFFFFF;
	}
}
#endif

bool dx10ConstantBuffer::Similar(dx10ConstantBuffer& _in)
{
	if (m_strBufferName._get() != _in.m_strBufferName._get())
		return false;

	if (m_eBufferType != _in.m_eBufferType)
		return false;

	if (m_uiMembersCRC != _in.m_uiMembersCRC)
		return false;

	if (m_MembersList.size() != _in.m_MembersList.size())
		return false;

	if (memcmp(&m_MembersList[0], &_in.m_MembersList[0], m_MembersList.size() * sizeof(m_MembersList[0])))
		return false;

	VERIFY(m_MembersNames.size() == _in.m_MembersNames.size());

	int iMemberNum = m_MembersNames.size();
	for (int i = 0; i < iMemberNum; ++i)
	{
		if (m_MembersNames[i].c_str() != _in.m_MembersNames[i].c_str())
			return false;
	}

	return true;
}

void dx10ConstantBuffer::Flush()
{
#if defined(USE_VK)
    const u32 workerId = g_vkWorkerId;
    uint32_t ringBufferOffset = 0;
    void* pRingPtr = DescriptorManager.AllocateDynamicUniform(m_uiBufferSize, ringBufferOffset);
    if (pRingPtr)
    {
        CopyMemory(pRingPtr, m_pBufferData[workerId], m_uiBufferSize);
        m_vkDynamicOffset[workerId] = ringBufferOffset;
    }
    m_bChanged[workerId] = false;
    m_vkFlushFrame[workerId] = Device.dwFrame;

#else
    if (m_bChanged)
    {
#if defined(USE_DX11)
        void    *pData;
        D3D11_MAPPED_SUBRESOURCE    pSubRes;
        CHK_DX(HW.pContext->Map(m_pBuffer, 0, D3D_MAP_WRITE_DISCARD, 0, &pSubRes));
        pData = pSubRes.pData;
        VERIFY(pData);
        VERIFY(m_pBufferData);
        CopyMemory(pData, m_pBufferData, m_uiBufferSize);
        HW.pContext->Unmap(m_pBuffer, 0);
        m_bChanged = false;
#else
        void    *pData;
        CHK_DX(m_pBuffer->Map(D3D_MAP_WRITE_DISCARD, 0, &pData));
        VERIFY(pData);
        VERIFY(m_pBufferData);
        CopyMemory(pData, m_pBufferData, m_uiBufferSize);
        m_pBuffer->Unmap();
        m_bChanged = false;
#endif
    }
#endif
}
