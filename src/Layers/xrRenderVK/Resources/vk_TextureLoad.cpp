// vk_TextureLoad.cpp — CRender::texture_load for xrRenderVK.
// Called by the shared SH_Texture.cpp::CTexture::Load() path for DDS game textures.
// Returns a heap-allocated VkTexture2DWrapper* on success, nullptr on failure.
// TODO: add VkTexture2DWrapper ref-counting or a global cache for proper lifetime management.
#include "stdafx.h"
#include "Resources/vk_Texture.h"

// Defined in vk_ResourceManager_Resources.cpp — strips .dds/.tga/.bmp extensions from texture names.
void fix_texture_name(LPSTR fn);

extern int psTextureLOD;

int get_texture_load_lod(LPCSTR fn)
{
    CInifile::Sect& sect = pSettings->r_section("reduce_lod_texture_list");
    CInifile::SectCIt it_ = sect.Data.begin();
    CInifile::SectCIt it_e_ = sect.Data.end();

    ENGINE_API bool is_enough_address_space_available();
    static bool enough_address_space_available = is_enough_address_space_available();

    for (CInifile::SectCIt it = it_; it != it_e_; ++it)
    {
        if (strstr(fn, it->first.c_str()))
        {
            if (psTextureLOD < 1)
            {
                if (enough_address_space_available)
                    return 0;
                else
                    return 1;
            }
            else if (psTextureLOD < 3)
                return 1;
            else
                return 2;
        }
    }

    if (psTextureLOD < 2)
        return 0;
    else if (psTextureLOD < 4)
        return 1;
    else
        return 2;
}

u32 calc_texture_size(int lod, u32 mip_cnt, u32 orig_size)
{
    if (1 == mip_cnt)
        return orig_size;

    int _lod = lod;
    float res = float(orig_size);

    while (_lod > 0)
    {
        --_lod;
        res -= res / 1.333f;
    }
    return iFloor(res);
}

ID3DBaseTexture* CRender::texture_load(LPCSTR fRName, u32& ret_msize, bool /*bStaging*/)
{
    ret_msize = 0;
    if (!fRName || !fRName[0])
        return nullptr;

    // Strip any existing extension (.dds/.tga etc.) — mirrors fix_texture_name in DX10 path.
    // This prevents FS.exist from searching for e.g. "terrain_escape.dds.dds".
    string_path fname;
    xr_strcpy(fname, fRName);
    fix_texture_name(fname);

    // ── Resolve file path — same three-alias chain as dx10Texture.cpp ────────
    // $level$         → current level directory (lightmaps: lmap#N_N, level_lods, etc.)
    // $game_saves$    → save-game directory (rarely used for textures)
    // $game_textures$ → main texture VFS (DB archives + loose files)
    string_path fn;
    if      (FS.exist(fn, "$level$",         fname, ".dds")) {}
    else if (FS.exist(fn, "$game_saves$",    fname, ".dds")) {}
    else if (FS.exist(fn, "$game_textures$", fname, ".dds")) {}
    else
    {
        Msg("~ [VK] texture_load: DDS not found for '%s', generating dummy", fRName);
        VkTexture2DWrapper* wrapper = xr_new<VkTexture2DWrapper>();
        if (vk_CreateDummyTexture(*wrapper) != VK_SUCCESS)
        {
            xr_delete(wrapper);
            return nullptr;
        }
        return (ID3DBaseTexture*)wrapper;
    }

    // ── Read file into memory ─────────────────────────────────────────────────
    IReader* R = FS.r_open(fn);
    if (!R)
    {
        Msg("! VK texture_load: FS.r_open failed for '%s'", fn);
        return nullptr;
    }

    const size_t fileSize = R->length();

    // ── Calculate Mip Skip ────────────────────────────────────────────────────
    int skip = get_texture_load_lod(fn);

    // ── Upload to GPU ─────────────────────────────────────────────────────────
    VkTexture2DWrapper* wrapper = xr_new<VkTexture2DWrapper>();
    VkResult res = vk_CreateTexture2D(R->pointer(), fileSize, *wrapper, skip);

    // Close the file reader only after GPU staging upload is complete
    FS.r_close(R);

    if (res != VK_SUCCESS)
    {
        Msg("! VK texture_load: vk_CreateTexture2D failed (%d) for '%s'", (int)res, fn);
        xr_delete(wrapper);
        return nullptr;
    }

    string128 texImgName, texViewName;
    xr_sprintf(texImgName, "Texture: %s", fRName);
    xr_sprintf(texViewName, "TexView: %s", fRName);
    vk_SetDebugName(HW.m_vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)wrapper->image, texImgName);
    vk_SetDebugName(HW.m_vkDevice, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)wrapper->imageView, texViewName);

    // ── Estimate GPU memory footprint ─────────────────────────────────────────
    ret_msize = calc_texture_size(skip, wrapper->mips + skip, (u32)fileSize);

    return wrapper;
}

