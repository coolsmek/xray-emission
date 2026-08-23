#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"

#ifndef _EDITOR
#include "../../xrEngine/render.h"
#endif

#include "../../xrEngine/tntQAVI.h"
#include "../../xrEngine/xrTheora_Surface.h"
#include "gifPlayer.h"

#include "dxRenderDeviceRender.h"

#define		PRIORITY_HIGH	12
#define		PRIORITY_NORMAL	8
#define		PRIORITY_LOW	4


void resptrcode_texture::create(LPCSTR _name)
{
	_set(DEV->_CreateTexture(_name));
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CTexture::CTexture()
{
	pSurface = NULL;
	pAVI = NULL;
	pTheora = NULL;
    gifPlayer = nullptr;
	desc_cache = 0;
	seqMSPF = 0;
	flags.MemoryUsage = 0;
	flags.bLoaded = false;
	flags.bLoading = false;
	flags.bUser = false;
	flags.seqCycles = FALSE;
	m_material = 1.0f;
	bind = xr_make_delegate(this, &CTexture::apply_load);
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	m_pSRView = nullptr;
#endif
}

CTexture::~CTexture()
{
	Unload();

	// release external reference
	DEV->_DeleteTexture(this);
}

void CTexture::surface_set(ID3DBaseTexture* surf)
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	if (surf) surf->AddRef();

	_RELEASE(pSurface);

	pSurface = surf;

#if defined(USE_VK)
	// Release the old SRV wrapper (Vulkan handles inside are owned by the VkTexture2DWrapper,
	// NOT by this lightweight view object — only delete the wrapper struct itself).
	delete m_pSRView;
	m_pSRView = nullptr;

	if (surf && surf->imageView != VK_NULL_HANDLE)
	{
		m_pSRView = new ID3DShaderResourceView{ surf->imageView, surf->sampler, surf->format };
	}
#endif
}

ID3DBaseTexture* CTexture::surface_get()
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	if (pSurface) pSurface->AddRef();
	return pSurface;
}

void CTexture::PostLoad()
{
	if (pTheora) bind = xr_make_delegate(this, &CTexture::apply_theora);
	else if (pAVI) bind = xr_make_delegate(this, &CTexture::apply_avi);
	else if (!seqDATA.empty()) bind = xr_make_delegate(this, &CTexture::apply_seq);
	else if (gifPlayer) bind = xr_make_delegate(this, &CTexture::apply_gif);
	else bind = xr_make_delegate(this, &CTexture::apply_normal);
}

void CTexture::apply_load(u32 dwStage)
{
    if (!flags.bLoaded) Load();
    else PostLoad();
    if (bind == xr_make_delegate(this, &CTexture::apply_load))
    {
        // This should not happen - if bind is still apply_load, fall back to apply_normal
        // which will just apply the (potentially unloaded) surface
        apply_normal(dwStage);
    }
    else
    {
        bind(dwStage);
    }
};


void CTexture::apply_theora(u32 dwStage)
{
	while (flags.bLoading) { SwitchToThread(); }
#if !defined(USE_VK)
	if (pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : RDEVICE.dwTimeContinual))
	{
		R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;
		D3DLOCKED_RECT R;
		RECT rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = pTheora->Width(true);
		rect.bottom = pTheora->Height(true);

		u32 _w = pTheora->Width(false);

		R_CHK(T2D->LockRect(0,&R,&rect,0));
		R_ASSERT(R.Pitch == int(pTheora->Width(false)*4));
		int _pos = 0;
		pTheora->DecompressFrame((u32*)R.pBits, _w - rect.right, _pos);
		VERIFY(u32(_pos) == rect.bottom*_w);
		R_CHK(T2D->UnlockRect(0));
	}
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
#else
	// VK: theora video texture update NYI; just register the texture slot
	if (dwStage < (u32)CBackend::mtMaxPixelShaderTextures)
		RCache.SetTexturePS((int)dwStage, this);
#endif
};

void CTexture::apply_avi(u32 dwStage)
{
	while (flags.bLoading) { SwitchToThread(); }
#if !defined(USE_VK)
	if (pAVI->NeedUpdate())
	{
		R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
		ID3DTexture2D* T2D = (ID3DTexture2D*)pSurface;
		D3DLOCKED_RECT R;
		R_CHK(T2D->LockRect(0,&R,NULL,0));
		R_ASSERT(R.Pitch == int(pAVI->m_dwWidth*4));
		BYTE* ptr;
		pAVI->GetFrame(&ptr);
		CopyMemory(R.pBits, ptr, pAVI->m_dwWidth*pAVI->m_dwHeight*4);
		R_CHK(T2D->UnlockRect(0));
	}
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
#else
	if (dwStage < (u32)CBackend::mtMaxPixelShaderTextures)
		RCache.SetTexturePS((int)dwStage, this);
#endif
};

void CTexture::apply_seq(u32 dwStage)
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	// SEQ
	u32 frame = RDEVICE.dwTimeContinual / seqMSPF; //RDEVICE.dwTimeGlobal
	u32 frame_data = seqDATA.size();
	if (flags.seqCycles)
	{
		u32 frame_id = frame % (frame_data * 2);
		if (frame_id >= frame_data) frame_id = (frame_data - 1) - (frame_id % frame_data);
		pSurface = seqDATA[frame_id];
#if defined(USE_VK)
		if (frame_id < m_seqSRView.size()) m_pSRView = m_seqSRView[frame_id];
#endif
	}
	else
	{
		u32 frame_id = frame % frame_data;
		pSurface = seqDATA[frame_id];
#if defined(USE_VK)
		if (frame_id < m_seqSRView.size()) m_pSRView = m_seqSRView[frame_id];
#endif
	}
#if !defined(USE_VK)
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
#else
	if (dwStage < (u32)CBackend::mtMaxPixelShaderTextures)
		RCache.SetTexturePS((int)dwStage, this);
#endif
};

void CTexture::apply_gif(u32 dwStage)
{
    while (flags.bLoading) { SwitchToThread(); }
#if !defined(USE_VK)
    if (gifPlayer->UpdateFrame())
    {
        const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
        R_ASSERT(gifFrame);
        pSurface = gifFrame->surface;
    }
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface));
#else
    if (dwStage < (u32)CBackend::mtMaxPixelShaderTextures)
        RCache.SetTexturePS((int)dwStage, this);
#endif
}

void CTexture::apply_normal(u32 dwStage)
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
    // should dwLastUsedFrame = Device.dwFrame; be placed inside the #if !defined(USE_VK)?
#if !defined(USE_VK)
    dwLastUsedFrame = Device.dwFrame;
	CHK_DX(HW.pDevice->SetTexture(dwStage,pSurface));
#else
    // VK: record this texture into RCache for vk_FlushDescriptors to bind.
    if (dwStage < CTexture::rstVertex)
    {
        if ((int)dwStage < CBackend::mtMaxPixelShaderTextures)
            RCache.SetTexturePS((int)dwStage, this);
    }
    else if (dwStage < CTexture::rstGeometry)
    {
        int vsIdx = (int)(dwStage - CTexture::rstVertex);
        if (vsIdx < CBackend::mtMaxVertexShaderTextures)
            RCache.SetTextureVS(vsIdx, this);
    }
#endif
};

void CTexture::Preload()
{
	m_bumpmap = DEV->m_textures_description.GetBumpName(cName);
	m_material = DEV->m_textures_description.GetMaterial(cName);
}

void CTexture::Load()
{
	PROF_EVENT("CTexture::Load");
	if (flags.bLoaded || flags.bLoading) return;
	flags.bLoading = true;
	flags.bLoaded = false;
	desc_cache = 0;
	if (pSurface)
	{
		flags.bLoading = false;
		flags.bLoaded = true;
		return;
	}

	flags.bUser = false;
	flags.MemoryUsage = 0;

	if (!cName.size() || !*cName)
	{
		flags.bLoading = false;
		flags.bLoaded = true;
		return;
	}

#if !defined(USE_VK)
	if (0==_stricmp(*cName,"$null"))
	{
		flags.bLoading = false;
		flags.bLoaded = true;
		return;
	}
#endif

	if (0!=strstr(*cName,"$user$"))
	{
		flags.bUser	= true;
		flags.bLoading = false;
		flags.bLoaded = true;
		return;
	}

	Preload();
	//#ifndef		DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		// Check for OGM
		string_path fn;
        if (strstr(Core.Params, "-vkdebug"))
            Msg("VK DEBUG CTexture::Load - Checking OGM for: %s", *cName);
		if (FS.exist(fn, "$game_textures$", *cName, ".ogm"))
		{
			// AVI
			pTheora = xr_new<CTheoraSurface>();
			m_play_time = 0xFFFFFFFF;

			if (!pTheora->Load(fn))
			{
				xr_delete(pTheora);
				FATAL("Can't open video stream");
			}
			else
			{
				flags.MemoryUsage = pTheora->Width(true) * pTheora->Height(true) * 4;
				BOOL bstop_at_end = (0 != strstr(cName.c_str(), "intro\\")) || (0 != strstr(cName.c_str(), "outro\\"));
				pTheora->Play(!bstop_at_end, RDEVICE.dwTimeContinual);

				// Now create texture
				ID3DTexture2D* pTexture = 0;
				u32 _w = pTheora->Width(false);
				u32 _h = pTheora->Height(false);

				HRESULT hrr = HW.pDevice->CreateTexture(
					_w, _h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, NULL);

				pSurface = pTexture;
				if (FAILED(hrr))
				{
					FATAL("Invalid video stream");
					R_CHK(hrr);
					xr_delete(pTheora);
					pSurface = 0;
				}
			}
		}
		else {
            if (strstr(Core.Params, "-vkdebug"))
                Msg("VK DEBUG CTexture::Load - Checking AVI for: %s", *cName);
            if (FS.exist(fn, "$game_textures$", *cName, ".avi"))
            {
                // AVI
                pAVI = xr_new<CAviPlayerCustom>();

                if (!pAVI->Load(fn))
                {
                    xr_delete(pAVI);
                    FATAL("Can't open video stream");
                }
                else
                {
                    flags.MemoryUsage = pAVI->m_dwWidth * pAVI->m_dwHeight * 4;

                    // Now create texture
                    ID3DTexture2D* pTexture = 0;
                    HRESULT hrr = HW.pDevice->CreateTexture(
                        pAVI->m_dwWidth, pAVI->m_dwHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, NULL);
                    pSurface = pTexture;
                    if (FAILED(hrr))
                    {
                        FATAL("Invalid video stream");
                        R_CHK(hrr);
                        xr_delete(pAVI);
                        pSurface = 0;
                    }
                }
            }
            else {
                if (strstr(Core.Params, "-vkdebug"))
                    Msg("VK DEBUG CTexture::Load - Checking SEQ for: %s", *cName);
                if (FS.exist(fn, "$game_textures$", *cName, ".seq"))
                {
			// Sequence
			string256 buffer;
			IReader* _fs = FS.r_open(fn);

			flags.seqCycles = FALSE;
			_fs->r_string(buffer, sizeof(buffer));
			if (0 == stricmp(buffer, "cycled"))
			{
				flags.seqCycles = TRUE;
				_fs->r_string(buffer, sizeof(buffer));
			}
			u32 fps = atoi(buffer);
			seqMSPF = 1000 / fps;

			while (!_fs->eof())
			{
				_fs->r_string(buffer, sizeof(buffer));
				_Trim(buffer);
				if (buffer[0])
				{
					// Load another texture
					u32 mem = 0;
					ID3DBaseTexture* frameSurf = ::RImplementation.texture_load(buffer, mem);
					if (frameSurf)
					{
						seqDATA.push_back(frameSurf);
						flags.MemoryUsage += mem;
#if defined(USE_VK)
						// Build a per-frame SRView wrapper (imageView+sampler) directly.
						// Do NOT call surface_set — we're inside Load() with bLoading == true.
						ID3DShaderResourceView* srv = nullptr;
						if (frameSurf->imageView != VK_NULL_HANDLE)
							srv = new ID3DShaderResourceView{ frameSurf->imageView,
							                                  frameSurf->sampler,
							                                  frameSurf->format };
						m_seqSRView.push_back(srv);
#endif
					}
				}
			}
			pSurface = 0;
			FS.r_close(_fs);
		}
        else if (FS.exist(fn, "$game_textures$", *cName, ".gif"))
        {
            gifPlayer = xr_new<CGIFAnimationPlayer>();
            if (!gifPlayer->Load(fn))
            {
                xr_delete(gifPlayer);
                pSurface = nullptr;
            }
            else
            {
                flags.MemoryUsage = gifPlayer->GetUsedMemory();

                gifPlayer->Play();

                const CGIFAnimationPlayer::Frame* const gifFrame = gifPlayer->GetActiveFrame();
                pSurface = gifFrame->surface;
            }
        }
		else
		{
			// Normal texture
			u32 mem = 0;
			pSurface = ::RImplementation.texture_load(*cName, mem);

			// Calc memory usage and preload into vid-mem
			if (pSurface)
			{
				flags.MemoryUsage = mem;
#if defined(USE_VK)
				// Build the VK SRView (imageView+sampler) directly here.
				// NOTE: do NOT call surface_set() — we are inside Load() with
				// flags.bLoading == true, and surface_set spins on that flag → deadlock.
				delete m_pSRView;
				m_pSRView = nullptr;
				if (pSurface->imageView != VK_NULL_HANDLE)
					m_pSRView = new ID3DShaderResourceView{ pSurface->imageView,
					                                        pSurface->sampler,
					                                        pSurface->format };
#endif
			}
		}
        } // close seq check else
        } // close avi check else
		//#endif
	}
	PostLoad();
	flags.bLoading = false;
	flags.bLoaded = true;
}

void CTexture::Unload()
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}

	// Already unloaded or never loaded: nothing to do.
	if (!flags.bLoaded)
		return;

#ifdef DEBUG
	string_path				msg_buff;
	xr_sprintf				(msg_buff,sizeof(msg_buff),"* Unloading texture [%s] pSurface RefCount=",cName.c_str());
#endif // DEBUG

	//.	if (flags.bLoaded)		Msg		("* Unloaded: %s",cName.c_str());

	flags.bLoaded = FALSE;
	if (!seqDATA.empty())
	{
		for (u32 I = 0; I < seqDATA.size(); I++)
		{
			_RELEASE(seqDATA[I]);
		}
		seqDATA.clear();
#if defined(USE_VK)
		for (u32 I = 0; I < m_seqSRView.size(); I++)
			delete m_seqSRView[I];   // wrapper struct only; Vk handles owned by seqDATA wrappers
		m_seqSRView.clear();
		m_pSRView = nullptr;         // was pointing into m_seqSRView
#endif
		pSurface = 0;
	}
	flags.MemoryUsage = 0;

    if (gifPlayer)
    {
        xr_delete(gifPlayer);
        pSurface = nullptr;
    }

#ifdef DEBUG
	_SHOW_REF		(msg_buff, pSurface);
#endif // DEBUG

	_RELEASE(pSurface);

	xr_delete(pAVI);
	xr_delete(pTheora);

	bind = xr_make_delegate(this, &CTexture::apply_load);
}

void CTexture::desc_update()
{
	while (flags.bLoading) { SwitchToThread(); }
	desc_cache = pSurface;
#if !defined(USE_VK)
	if (pSurface && (D3DRTYPE_TEXTURE == pSurface->GetType()))
	{
		ID3DTexture2D* T = (ID3DTexture2D*)pSurface;
		R_CHK(T->GetLevelDesc(0,&desc));
	}
#else
	// VK: populate desc from the VkTexture2DWrapper fields directly
	if (pSurface)
	{
		desc.Width    = pSurface->width;
		desc.Height   = pSurface->height;
		desc.Format   = pSurface->format;
		desc.MipLevels = pSurface->mips;
	}
#endif
}

void CTexture::video_Play(BOOL looped, u32 _time)
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	if (pTheora) pTheora->Play(looped, (_time != 0xFFFFFFFF) ? (m_play_time = _time) : RDEVICE.dwTimeContinual);
}

void CTexture::video_Pause(BOOL state)
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	if (pTheora) pTheora->Pause(state);
}

void CTexture::video_Stop()
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	if (pTheora) pTheora->Stop();
}

BOOL CTexture::video_IsPlaying()
{
	while (flags.bLoading)
	{
		SwitchToThread();
	}
	return (pTheora) ? pTheora->IsPlaying() : FALSE;
}
