#include "stdafx.h"
#include "vk_FontRender.h"

#include "../../xrEngine/GameFont.h"

vkFontRender::vkFontRender()
{
}

vkFontRender::~vkFontRender()
{
	pShader.destroy();
	pGeom.destroy();
}

void vkFontRender::Initialize(LPCSTR cShader, LPCSTR cTexture)
{
    // VK: redirect "font" → "hud\font" since "font" shader has no texture binding in VK path
    LPCSTR effectiveShader = (cShader && xr_strcmp(cShader, "font") == 0) ? "hud\\font" : cShader;

	pShader.create(effectiveShader, cTexture);
	pGeom.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
    pFontTexture.create(cTexture);
    m_initTextureName = cTexture ? cTexture : "";

    //Msg("VK FONT MEDIUM Initialize: shader='%s' texture='%s' pFontTexture=%s",
    //    cShader ? cShader : "NULL",
    //    cTexture ? cTexture : "NULL",
    //    (bool)pFontTexture ? "OK" : "NULL");
}

extern ENGINE_API xr_atomic_bool g_bRendering;
extern ENGINE_API Fvector2 g_current_font_scale;

void vkFontRender::OnRender(CGameFont& owner)
{
	VERIFY(g_bRendering);
	if (pShader) {
	    RCache.set_Shader(pShader);

        // Font glyphs are alpha-blended (texture alpha * vertex color). The shader-
        // cache blender for hud\font does not record blend into SimulatorStates in
        // the VK path, so set it explicitly here (mirrors R4 UI alpha blending).
        RCache.set_Blend(TRUE); // defaults: SRC_ALPHA / ONE_MINUS_SRC_ALPHA, ADD
	}

    /* TEMP: log shader element name + PS slot 0 immediately after set_Shader
    {
	    CTexture* t = RCache.GetTexturePS(0);

	    Msg("VK FONT SHADER this=%p initTex='%s' PSslot0_after_setShader='%s'",
            (void*)this,
            m_initTextureName.c_str(),
            t ? t->cName.c_str() : "NULL");
    }// TEMP: log shader element name + PS slot 0 immediately after set_Shad*/

    if (!(owner.uFlags & CGameFont::fsValid))
    {
        // Prefer the directly-owned texture reference — it never depends on
        // pipeline binding state and is valid as soon as the resource is created.
        u32 texW = 0, texH = 0;

        if (pFontTexture)
        {
            // Log surface state BEFORE calling get_Width/Height
            ID3DBaseTexture* surf = pFontTexture->surface_get();
#ifdef VK_ENABLE_TESTS
			Msg("VK FONT MEDIUM fsValid attempt: pSurface=%p bLoaded=%d bLoading=%d",
                (void*)surf,
                (int)pFontTexture->flags.bLoaded,
                (int)pFontTexture->flags.bLoading);
#endif
            if (surf) surf->Release(); // surface_get does AddRef

            texW = pFontTexture->get_Width();
            texH = pFontTexture->get_Height();
#ifdef VK_ENABLE_TESTS
            Msg("VK FONT MEDIUM get_Width/Height returned: texW=%u texH=%u", texW, texH);
#endif
        }
#ifdef VK_ENABLE_TESTS
        else
        {
            Msg("VK FONT MEDIUM pFontTexture is NULL — not yet created");
        }
#endif
        if (texW > 0 && texH > 0)
        {
            owner.vTS.set((int)texW, (int)texH);
            owner.fTCHeight = owner.fHeight / float(owner.vTS.y);
            owner.uFlags |= CGameFont::fsValid;  // only cache when we have real dims
#ifdef VK_ENABLE_TESTS
			Msg("VK FONT MEDIUM fsValid SET: vTS=(%d,%d) fTCHeight=%.4f fHeight=%.2f",
                (int)owner.vTS.x, (int)owner.vTS.y, owner.fTCHeight, owner.fHeight);
#endif
        }
#ifdef VK_ENABLE_TESTS
        else
        {
            Msg("VK FONT MEDIUM fsValid NOT set this frame (texW=%u texH=%u) — will retry", texW, texH);
        }
#endif
        // If texture isn't loaded yet, do NOT set fsValid — retry next frame
    }

//commented out - if (!s_uvLogged...
    /*bool s_uvLogged = false;
    if (!s_uvLogged && (owner.uFlags & CGameFont::fsValid) && !owner.strings.empty())
    {
        s_uvLogged = true;
        const char* str = owner.strings[0].string;
        if (str && str[0])
        {
            u16 ch = (u16)(u8)str[0];
            Fvector l = owner.GetCharTC(ch);
            float tu = l.x / owner.vTS.x;
            float tv = l.y / owner.vTS.y;
            float fTCWidth = l.z / owner.vTS.x;
            Msg("VK FONT MEDIUM First char '%c' (0x%02X): TCMap=(%.1f,%.1f,%.1f) vTS=(%d,%d) UV=(%.4f,%.4f) fTCWidth=%.4f fTCHeight=%.4f",
                (ch >= 32 && ch < 127) ? (char)ch : '?', ch,
                l.x, l.y, l.z,
                (int)owner.vTS.x, (int)owner.vTS.y,
                tu, tv, fTCWidth, owner.fTCHeight);
        }
    }*/

#ifdef VK_ENABLE_TESTS
    // --- TEXTURE VERIFICATION LOG (per-instance, remove after diagnosis) ---
    if (!m_texVerified && (owner.uFlags & CGameFont::fsValid)) {
        m_texVerified = true;
        CTexture* activeT = RCache.GetTexturePS(0);
        Msg("VK FONT TEX this=%p initTex='%s' pFontTexture='%s' dims=%ux%u | PSslot0='%s' dims=%ux%u",
            (void*)this,
            m_initTextureName.c_str(),
            pFontTexture ? pFontTexture->cName.c_str() : "NULL",
            pFontTexture ? pFontTexture->get_Width()  : 0u,
            pFontTexture ? pFontTexture->get_Height() : 0u,
            activeT ? activeT->cName.c_str() : "NULL",
            activeT ? activeT->get_Width()  : 0u,
            activeT ? activeT->get_Height() : 0u);
    }
    // --- END TEXTURE VERIFICATION LOG ---
#endif

	for (u32 i = 0; i < owner.strings.size();)
	{
		// calculate first-fit
		int count = 1;
		int length = owner.smart_strlen(owner.strings[i].string);

		while ((i + count) < owner.strings.size())
		{
			int L = owner.smart_strlen(owner.strings[i + count].string);
			if ((L + length) < MAX_MB_CHARS)
			{
				count++;
				length += L;
			}
			else break;
		}

		// lock memory
		u32 vOffset;
		FVF::TL* v = (FVF::TL*)RCache.Vertex.Lock(length * 4, pGeom.stride(), vOffset);
		FVF::TL* start = v;

		// fill vertices
		u32 last = i + count;
		for (; i < last; i++)
		{
			CGameFont::String& PS = owner.strings[i];
			wide_char wsStr[MAX_MB_CHARS];

			int len = owner.IsMultibyte() ? mbhMulti2Wide(wsStr, NULL, MAX_MB_CHARS, PS.string) : xr_strlen(PS.string);

			if (len)
			{
				float X = float(iFloor(PS.x));
				float Y = float(iFloor(PS.y));
				float S = PS.height * g_current_font_scale.y;
				float Y2 = Y + S;
				float fSize = 0;

				if (PS.align)
					fSize = owner.IsMultibyte() ? owner.SizeOf_(wsStr) : owner.SizeOf_(PS.string);

				switch (PS.align)
				{
				case CGameFont::alCenter:
					X -= (iFloor(fSize * 0.5f)) * g_current_font_scale.x;
					break;
				case CGameFont::alRight:
					X -= iFloor(fSize);
					break;
				}

				// Half-pixel offset: cancel VS DX9 correction (mirrors dxFontRender.cpp)
				X  -= 0.5f;
				Y  -= 0.5f;
				Y2 -= 0.5f;

				u32 clr, clr2;
				clr2 = clr = PS.c;
				if (owner.uFlags & CGameFont::fsGradient)
				{
					u32 _R = color_get_R(clr) / 2;
					u32 _G = color_get_G(clr) / 2;
					u32 _B = color_get_B(clr) / 2;
					u32 _A = color_get_A(clr);
					clr2 = color_rgba(_R, _G, _B, _A);
				}

				float tu, tv;
				for (int j = 0; j < len; j++)
				{
					Fvector l;
					l = owner.IsMultibyte() ? owner.GetCharTC(wsStr[1 + j]) : owner.GetCharTC((u16)(u8)PS.string[j]);

					float scw = l.z * g_current_font_scale.x;
					float fTCWidth = l.z / owner.vTS.x;

					if (!fis_zero(l.z))
					{
						tu = (l.x / owner.vTS.x);
						tv = (l.y / owner.vTS.y);

						v->set(X, Y2, clr2, tu, tv + owner.fTCHeight);
						v++;
						v->set(X, Y, clr, tu, tv);
						v++;
						v->set(X + scw, Y2, clr2, tu + fTCWidth, tv + owner.fTCHeight);
						v++;
						v->set(X + scw, Y, clr, tu + fTCWidth, tv);
						v++;
					}
					X += scw * owner.vInterval.x;
					if (owner.IsMultibyte())
					{
						X -= 2;
						if (IsNeedSpaceCharacter(wsStr[1 + j]))
							X += owner.fXStep;
					}
				}
			}
		}

		// Unlock and draw
		u32 vCount = (u32)(v - start);
		RCache.Vertex.Unlock(vCount, pGeom.stride());
		if (vCount)
		{
			RCache.set_Geometry(pGeom);
			RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, vCount, 0, vCount / 2);
		}
	}
}
