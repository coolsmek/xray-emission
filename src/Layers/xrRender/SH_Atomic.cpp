#include "stdafx.h"
#pragma hdrstop

#include "sh_atomic.h"
#include "ResourceManager.h"

#include "dxRenderDeviceRender.h"

// Atomic
//SVS::~SVS								()			{	_RELEASE(vs);		dxRenderDeviceRender::Instance().Resources->_DeleteVS			(this);	}
//SPS::~SPS								()			{	_RELEASE(ps);		dxRenderDeviceRender::Instance().Resources->_DeletePS			(this);	}
//SState::~SState							()			{	_RELEASE(state);	dxRenderDeviceRender::Instance().Resources->_DeleteState		(this);	}
//SDeclaration::~SDeclaration				()			{	_RELEASE(dcl);		dxRenderDeviceRender::Instance().Resources->_DeleteDecl		(this);	}

///////////////////////////////////////////////////////////////////////
//	SVS
SVS::SVS() :
	vs(0)
#if defined(USE_DX10) || defined(USE_DX11)
//	,signature(0)
#endif	//	USE_DX10
{
	;
}


SVS::~SVS()
{
	DEV->_DeleteVS(this);
#if defined(USE_VK)
	if (vs)
	{
		if (vs->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, vs->module, nullptr);
		xr_free(vs->pSPIRV);
		xr_delete(vs);
	}
#else
	_RELEASE(vs);
#endif
}


///////////////////////////////////////////////////////////////////////
//	SPS
SPS::~SPS()
{
#if defined(USE_VK)
	if (ps)
	{
		if (ps->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, ps->module, nullptr);
		xr_free(ps->pSPIRV);
		xr_delete(ps);
	}
#else
	_RELEASE(ps);
#endif
	DEV->_DeletePS(this);
}

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
///////////////////////////////////////////////////////////////////////
//	SGS
SGS::~SGS()
{
#if defined(USE_VK)
	if (gs)
	{
		if (gs->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, gs->module, nullptr);
		xr_free(gs->pSPIRV);
		xr_delete(gs);
	}
#else
	_RELEASE(gs);
#endif
	DEV->_DeleteGS(this);
}

#	if defined(USE_DX11) || defined(USE_VK)
SHS::~SHS()
{
#if defined(USE_VK)
	if (sh)
	{
		if (sh->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, sh->module, nullptr);
		xr_free(sh->pSPIRV);
		xr_delete(sh);
	}
#else
	_RELEASE(sh);
#endif
	DEV->_DeleteHS(this);
}

SDS::~SDS()
{
#if defined(USE_VK)
	if (sh)
	{
		if (sh->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, sh->module, nullptr);
		xr_free(sh->pSPIRV);
		xr_delete(sh);
	}
#else
	_RELEASE(sh);
#endif
	DEV->_DeleteDS(this);
}

SCS::~SCS()
{
#if defined(USE_VK)
	if (sh)
	{
		if (sh->module != VK_NULL_HANDLE)
			vkDestroyShaderModule(HW.m_vkDevice, sh->module, nullptr);
		xr_free(sh->pSPIRV);
		xr_delete(sh);
	}
#else
	_RELEASE(sh);
#endif
	DEV->_DeleteCS(this);
}
#	endif	// USE_DX11 || USE_VK

///////////////////////////////////////////////////////////////////////
//	SInputSignature  (DX10 / DX11 only — no VK equivalent)
#if defined(USE_DX10) || defined(USE_DX11)
SInputSignature::SInputSignature(ID3DBlob* pBlob)
{
	VERIFY(pBlob);
	signature = pBlob;
	signature->AddRef();
};

SInputSignature::~SInputSignature()
{
	_RELEASE(signature);
	DEV->_DeleteInputSignature(this);
}
#endif	//	USE_DX10 / USE_DX11
#endif	//	USE_DX10 || USE_DX11 || USE_VK

///////////////////////////////////////////////////////////////////////
//	SState
SState::~SState()
{
	_RELEASE(state);
	DEV->_DeleteState(this);
}

///////////////////////////////////////////////////////////////////////
//	SDeclaration
SDeclaration::~SDeclaration()
{
	DEV->_DeleteDecl(this);
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	xr_map<ID3DBlob*, ID3DInputLayout*>::iterator iLayout;
	iLayout = vs_to_layout.begin();
	for (; iLayout != vs_to_layout.end(); ++iLayout)
	{
		//	Release vertex layout
		_RELEASE(iLayout->second);
	}
#else	//	USE_DX10
	//	Release vertex layout
	_RELEASE(dcl);
#endif	//	USE_DX10
}
