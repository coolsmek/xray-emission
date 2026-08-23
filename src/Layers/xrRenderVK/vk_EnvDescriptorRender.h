#pragma once
#include "../../Include/xrRender/EnvironmentRender.h"
#include "../xrRender/blenders/blender.h"

class vkEnvDescriptorRender : public IEnvDescriptorRender {
    friend class vkEnvDescriptorMixerRender;
public:
    virtual ~vkEnvDescriptorRender() override = default;
    virtual void Copy(IEnvDescriptorRender& _in)         override;
    virtual void OnDeviceCreate(CEnvDescriptor& owner)   override;
    virtual void OnDeviceDestroy()                       override;

private:
    ref_texture sky_texture;
    ref_texture sky_texture_env;
    ref_texture clouds_texture;
};

class vkEnvDescriptorMixerRender : public IEnvDescriptorMixerRender {
public:
    virtual ~vkEnvDescriptorMixerRender() override = default;
    virtual void Copy(IEnvDescriptorMixerRender& _in)   override;
    virtual void Destroy()                              override;
    virtual void Clear()                                override;
    virtual void lerp(IEnvDescriptorRender* inA, IEnvDescriptorRender* inB) override;

public:
    STextureList sky_r_textures;
    STextureList sky_r_textures_env;
    STextureList clouds_r_textures;
};
