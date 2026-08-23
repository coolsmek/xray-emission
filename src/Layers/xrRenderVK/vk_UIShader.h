#pragma once
// vk_UIShader.h — No-op IUIShader for the Vulkan path.
// Prevents null-deref crashes during UI initialization.

#include "../../Include/xrRender/UIShader.h"
#include "../xrRender/Shader.h"

class vkUIShader : public IUIShader
{
public:
    ref_shader hShader;
    virtual ~vkUIShader() override = default;
    virtual void Copy(IUIShader& _in) override
    {
        *this = *((vkUIShader*)&_in);
    }
    virtual void create(LPCSTR sh, LPCSTR tex = nullptr, bool no_cache = false) override;
    virtual bool inited() override { return hShader; }
};
