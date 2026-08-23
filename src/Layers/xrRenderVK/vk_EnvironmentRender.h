#pragma once
// vk_EnvironmentRender.h — No-op IEnvironmentRender for the Vulkan path.
// This prevents null-deref crashes during engine initialization.

#include "../../Include/xrRender/EnvironmentRender.h"

#include "../xrRender/blenders/blender.h"

class CBlender_skybox : public IBlender
{
public:
    virtual LPCSTR getComment() { return "INTERNAL: combiner"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C)
    {
        switch (C.iElement)
        {
        case 0:
            C.r_Pass("sky2", "sky2", FALSE, TRUE, FALSE);
            C.r_dx10Texture("s_sky0", "$null");
            C.r_dx10Texture("s_sky1", "$null");
            C.r_dx10Sampler("smp_rtlinear");
            C.r_dx10Texture("s_tonemap", "$user$tonemap");
            C.PassSET_ZB(FALSE, FALSE);
            C.r_Stencil(FALSE); // sky must never read/write stencil (see phase_combine)
            C.r_End();
            break;
        case 1:
            C.r_Pass("ssfx_sky_mv", "ssfx_sky_mv", FALSE, TRUE, FALSE);
            C.PassSET_ZB(FALSE, FALSE);
            C.r_Stencil(FALSE); // sky must never read/write stencil (see phase_combine)
            C.r_End();
            break;
        }
    }
};

class vkEnvironmentRender : public IEnvironmentRender
{
public:
    vkEnvironmentRender();
    virtual ~vkEnvironmentRender() override = default;
    virtual void Copy(IEnvironmentRender& _in) override;
    virtual void OnFrame(CEnvironment& env) override;
    virtual void OnLoad() override;
    virtual void OnUnload() override;
    virtual void RenderSky(CEnvironment& env, bool only_MV = false) override;
    virtual void RenderClouds(CEnvironment& env) override;
    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;
    virtual particles_systems::library_interface const& particles_systems_library() override;

private:
    CBlender_skybox m_b_skybox;

    ref_shader sh_2sky;
    ref_geom sh_2geom;

    ref_shader sh_2sky_mv;

    ref_shader clouds_sh;
    ref_geom clouds_geom;

    ref_texture tonemap;
    ref_texture tsky0, tsky1;
    ref_texture tenv0, tenv1;   // $user$env_s0/1 — env cubemaps sampled by hmodel() in combine
};
