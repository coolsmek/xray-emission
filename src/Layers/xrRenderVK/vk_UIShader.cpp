#include "stdafx.h"
#include "vk_UIShader.h"

xr_unordered_flat_map<xr_string, ref_shader> g_vkUIShadersCache;

static ref_shader& GetCachedvkUIShader(const char* sh, const char* tex)
{
    xr_string key{ tex ? tex : "" };
    key += "_";
    key += sh;

    if (const auto it = g_vkUIShadersCache.find(key); it != g_vkUIShadersCache.end())
    {
        return it->second;
    }
    else
    {
        auto& shader = g_vkUIShadersCache[key];
        shader.create(sh, tex);
        return shader;
    }
}

void vkUIShader::create(LPCSTR sh, LPCSTR tex, bool no_cache)
{
    if (no_cache)
    {
        hShader.create(sh, tex);
    }
    else
    {
        hShader = GetCachedvkUIShader(sh, tex);
    }
}
