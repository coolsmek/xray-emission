#pragma once
// vk_WallMarkArray.h — No-op IWallMarkArray for the Vulkan path.
// This prevents null-deref crashes during game material loading.

#include "../../Include/xrRender/WallMarkArray.h"

class vkWallMarkArray : public IWallMarkArray
{
public:
    virtual ~vkWallMarkArray() override = default;
    virtual void Copy(IWallMarkArray& _in) override {}
    virtual void AppendMark(LPCSTR s_textures) override {}
    virtual void clear() override {}
    virtual bool empty() override { return true; }
    virtual wm_shader GenerateWallmark() override;
};
