#pragma once

#include "stdafx.h"
#include "../../xrEngine/render.h"
#include "vk_LayoutTransitions.h"
#include "../../xrRender/ResourceManager.h"
#include "../blender_combine.h"

class CRenderTarget : public IRender_Target
{
private:
    u32 dwWidth;
    u32 dwHeight;

public:
    // Core Render Targets
    ref_rt rt_Position;    // (x,y,z,?)
    ref_rt rt_Normal;      // (nx,ny,nz,hemi)
    ref_rt rt_Color;       // (r,g,b,specular-gloss)
    ref_rt rt_Accumulator; // (r,g,b,specular)
    ref_rt rt_Generic_0;   // post-process, intermediate
    ref_rt rt_Generic_1;   // post-process, intermediate
    ref_rt rt_ui_pda;
    ref_rt rt_secondVP;

    // smap
    ref_rt rt_smap_surf;
    ref_rt rt_smap_depth;
    ref_rt rt_smap_depth_minmax; // dummy for phase_combine

    // dummy textures
    ref_texture t_dummy_material;
    ref_texture t_dummy_sunmask;
    VkTexture2DWrapper* m_material_lut_wrapper;

    // Hardware depth buffer (backed by HW.m_vkDepthImage / m_vkDepthView).
    // Owned by CHW — CRenderTarget only wraps the handles.
    VkDSVWrapper pZB;

    // Shadow-map depth DSV (backed by rt_smap_depth's image/view).
    // Set once rt_smap_depth.create() is called.
    VkDSVWrapper rt_smap_dsv;

    // Track active dynamic rendering pass state
    bool m_bRenderingPassActive;
    uint32_t m_activeRendertargetsCount;
    uint32_t m_lastSwapchainClearFrame;
    u32      dwAccumulatorClearMark;
    u32      dwLightMarkerID;

    // ── Combine pass resources ────────────────────────────────────────────
    ref_shader  s_combine;  // blender_combine element 0 = combine_1.vs + combine_1_nomsaa.ps
    ref_geom    g_combine;  // FVF::TL fullscreen quad (RCache dynamic VB + QuadIB)

    // ── Accumulation pass resources ────────────────────────────────────────
    ref_shader  s_accum_direct;
    ref_shader  s_accum_mask;
    ref_shader  s_occq;

    CRenderTarget();
    virtual ~CRenderTarget();

    void reset_begin();
    void reset_end();

    virtual u32 get_width() { return dwWidth; }
    virtual u32 get_height() { return dwHeight; }

    // ── Pass begin/end helpers ────────────────────────────────────────────
    // Clear G-Buffer (Position, Normal, Color) + HW depth at the start of the frame
    void phase_scene_prepare();
    // Bind G-Buffer (Position, Normal, Color) + HW depth; open rendering pass.
    void phase_scene_begin();
    // End G-Buffer pass; transition rt_Position/Normal/Color → SHADER_READ_ONLY.
    void phase_scene_end();
    // Blit rt_Color to the provided swapchain image and transition to PRESENT_SRC.
    void phase_combine(VkCommandBuffer cmd, VkImage swapchainImage, VkImageView swapchainView);

    // ── u_setrt ───────────────────────────────────────────────────────────
    void u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb);
    void u_setrt(u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3, ID3DDepthStencilView* zb);

    // ── Lighting accumulator (Phase 4 stubs — real shaders in Phase 5) ───
    void accum_direct(u32 sub_phase);
    void accum_point(class light* L);
    void accum_spot(class light* L);
    void accum_reflected(class light* L) {}

    // ── Phase 5 stubs — referenced by lights_render.cpp and R_sun.cpp ───
    // All take the parameters from the actual call sites.
    void reset_light_marker(bool bResetStencil = false);
    void increment_light_marker();
    void phase_smap_spot_clear()                                    {}
    void phase_smap_spot(class light* /*L*/)                        {}
    void phase_smap_spot_tsh(class light* /*L*/)                    {}
    void accum_volumetric_lv(class light* /*L*/)                    {}
    void accum_volumetric(class light* /*L*/)                       {}
    void phase_smap_direct_tsh(class light* /*L*/, u32 /*phase*/)   {}
    void phase_smap_direct(class light* L, u32 sub_phase);
    void phase_accumulator();
    void accum_direct_cascade(u32 sub_phase, const Fmatrix& xf, const Fmatrix& xfPrev, float bias);
    void accum_direct_blend();
    void accum_direct_finalize();

    void draw_volume(class light* /*L*/)                            {}
    bool need_to_render_sunshafts()                                 { return false; }

    // Virtual stub overrides from IRender_Target
    virtual void set_blur(float f) {}
    virtual void set_gray(float f) {}
    virtual void set_duality_h(float f) {}
    virtual void set_duality_v(float f) {}
    virtual void set_noise(float f) {}
    virtual void set_noise_scale(float f) {}
    virtual void set_noise_fps(float f) {}
    virtual void set_color_base(u32 f) {}
    virtual void set_color_gray(u32 f) {}
    virtual void set_color_add(const Fvector& f) {}
    virtual void set_cm_imfluence(float f) {}
    virtual void set_cm_interpolate(float f) {}
    virtual void set_cm_textures(const shared_str& tex0, const shared_str& tex1) {}
};
