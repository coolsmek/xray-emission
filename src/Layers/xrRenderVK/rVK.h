// Implements the core CRender class inheriting from the abstract engine interface IRender_interface
#pragma once
#include "stdafx.h"
#include "../../xrEngine/Render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/irenderable.h"

// Forward declarations
class dxRender_Visual;
class CStreamReader;
#include "../xrRender/r__dsgraph_manager.h"
#include "../xrRender/r__sector.h"
#include "../xrRender/r__occlusion.h"
#include "../xrRender/PSLibrary.h"
#include "RenderTarget/vk_RenderTarget.h"
#include "../xrRender/hom.h"
#include "../xrRender/detailmanager.h"
#include "../xrRender/modelpool.h"
#include "../xrRender/wallmarksengine.h"
#include "SMAP_Allocator.h"
#include "../xrRender/light_db.h"
#include "../xrRender/LightTrack.h"
#include "../xrRender/r_sun_cascades.h"
#include "../xrRender/IRenderDetailModel.h"
#include "../../xrEngine/Fmesh.h"

class CSkeletonWallmark;

// Vulkan CRender — implements the engine's abstract IRender_interface.
// This is the primary frame driver for xrRenderVK.
class CRender : public IRender_interface, public pureFrame
{
public:
    enum
    {
        MSAA_ATEST_NONE = 0x0,
        MSAA_ATEST_DX10_0_ATOC = 0x1,
        MSAA_ATEST_DX10_1_NATIVE = 0x2,
        MSAA_ATEST_DX10_1_ATOC = 0x3,
    };

    CRender();
    virtual ~CRender();

    struct _options
    {
        u32 ssfx_branches : 1;
        u32 ssfx_blood : 1;
        u32 ssfx_rain : 1;
        u32 ssfx_hud_raindrops : 1;
        u32 ssfx_ssr : 1;
        u32 ssfx_terrain : 1;
        u32 ssfx_volumetric : 1;
        u32 ssfx_water : 1;
        u32 ssfx_ao : 1;
        u32 ssfx_il : 1;
        u32 ssfx_core : 1;
        u32 ssfx_bloom : 1;
        u32 ssfx_sss : 1;
        u32 ssfx_fog : 1;
        u32 ssfx_motionblur : 1;
        u32 ssfx_taa : 1;
        u32 ssfx_motionvectors : 1;
        u32 ssfx_glass : 1;
        u32 bug : 1;
        u32 ssao_blur_on : 1;
        u32 ssao_opt_data : 1;
        u32 ssao_half_data : 1;
        u32 ssao_hbao : 1;
        u32 ssao_hdao : 1;
        u32 ssao_ultra : 1;
        u32 hbao_vectorized : 1;
        u32 volsize : 16;
        u32 smapsize : 16;
        u32 depth16 : 1;
        u32 mrt : 1;
        u32 mrtmixdepth : 1;
        u32 fp16_filter : 1;
        u32 fp16_blend : 1;
        u32 albedo_wo : 1;
        u32 HW_smap : 1;
        u32 HW_smap_PCF : 1;
        u32 HW_smap_FETCH4 : 1;
        u32 HW_smap_FORMAT : 32;
        u32 nvstencil : 1;
        u32 nvdbt : 1;
        u32 nullrt : 1;
        u32 no_ram_textures : 1;
        u32 distortion : 1;
        u32 distortion_enabled : 1;
        u32 sunfilter : 1;
        u32 sunstatic : 1;
        u32 sjitter : 1;
        u32 noshadows : 1;
        u32 Tshadows : 1;
        u32 disasm : 1;
        u32 advancedpp : 1;
        u32 volumetricfog : 1;
        u32 dx10_msaa : 1;
        u32 dx10_msaa_hybrid : 1;
        u32 dx10_msaa_opt : 1;
        u32 dx10_sm4_1 : 1;
        u32 dx10_msaa_alphatest : 2;
        u32 dx10_msaa_samples : 4;
        u32 dx10_minmax_sm : 2;
        u32 dx10_minmax_sm_screenarea_threshold;
        u32 dx11_enable_tessellation : 1;
        u32 forcegloss : 1;
        u32 forceskinw : 1;
        u32 dx11_hdr10 : 1;
        float forcegloss_v;
    } o;

    struct _stats
    {
        u32 l_total, l_visible;
        u32 l_shadowed, l_unshadowed;
        s32 s_used, s_merged, s_finalclip;
        u32 o_queries, o_culled;
        u32 ic_total, ic_culled;
        u32 ls_shadowed_in;
        u32 ls_shadowed_after_vis;
        u32 ls_shadowed_rendered;
        u32 ls_shadowed_pending_skipped;
        u32 ls_shadowed_invisible_skipped;
        u32 ls_shadowed_peak_in;
        u32 ls_shadowed_peak_after_vis;
        u32 ls_unshadowed_point_in;
        u32 ls_unshadowed_spot_in;
        u32 ls_unshadowed_point_rendered;
        u32 ls_unshadowed_spot_rendered;
    } stats;

    bool is_sun();
    CSector* pLastSector;
    CSector* pOutdoorSector;
    Fvector vLastCameraPos;
    u32 uLastLTRACK;
    xr_vector<IRender_Portal*> Portals;
    xr_vector<IRender_Sector*> Sectors;
    xrXRC Sectors_xrc;
    CDB::MODEL* rmPortals;
    CHOM HOM;
    R_occlusion HWOCC;

    xr_vector<FSlideWindowItem> SWIs;
    xr_vector<ref_shader> Shaders;
    typedef svector<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH + 1> VertexDeclarator;
    xr_vector<VertexDeclarator> nDC, xDC;
    xr_vector<ID3DVertexBuffer*> nVB, xVB;
    xr_vector<ID3DIndexBuffer*> nIB, xIB;
    xr_vector<dxRender_Visual*> Visuals;
    CPSLibrary PSLibrary;

    CDetailManager* Details;
    CModelPool* Models;
    CWallmarksEngine* Wallmarks;

    CRenderTarget* Target;

    CLight_DB Lights;
    SMAP_Allocator LP_smap_pool;
    light_Package LP_normal;
    light_Package LP_pending;

    shared_str c_sbase;
    shared_str c_lmaterial;

    bool m_bMakeAsyncSS;
    bool m_bFirstFrameAfterReset;
    xr_vector<sun::cascade> m_sun_cascades;

    CFrustum rainwet_cull_frustum;
    Fvector3 rainwet_cull_COP;
    Fmatrix rainwet_cull_xform;

    CDSGraphManager GMRainWet = CDSGraphManager(u32(0), u32(STYPE_RENDERABLE), { true,false,false,false,true,false,false });
    CDSGraphManager GMBase = CDSGraphManager(u32(CDSGraphManager::VQ_HOM + CDSGraphManager::VQ_SSA + CDSGraphManager::VQ_FADE),
        u32(STYPE_RENDERABLE + STYPE_PARTICLE + STYPE_LIGHTSOURCE),
        { true,true,true,true,false,false,false });

    xr_task_group                                               main_task_static, main_task_dynamic, sun_cascades_task, raimwet_task;

    xr_set<light*>                                              v_all_lights;
    xr_list<light*>                                             v_all_lights_dque;

    IRender_Sector* rimp_detectSector(Fvector& P, Fvector& D);
    void render_forward();
    void render_Reticle();
    void render_smap_direct(Fmatrix& mCombined);
    void render_indirect(light* L);
    void render_lights(light_Package& LP);
    void render_menu();
    void render_rain();

    void render_sun_cascade(u32 cascade_ind);
    void init_cacades();
    void render_sun_cascades();

    ShaderElement* rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq);
    ShaderElement* rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq);
    D3DVERTEXELEMENT9* getVB_Format(int id, BOOL _alt = FALSE);
    ID3DVertexBuffer* getVB(int id, BOOL _alt = FALSE);
    ID3DIndexBuffer* getIB(int id, BOOL _alt = FALSE);
    FSlideWindowItem* getSWI(int id);
    IRender_Portal* getPortal(int id);
    IRender_Sector* getSectorActive();
    IRenderVisual* model_CreatePE(LPCSTR name);
    ID3DBaseTexture* texture_load(LPCSTR fname, u32& msize, bool bStaging = false);
    IRender_Sector* detectSector(const Fvector& P, Fvector& D);
    IRender_Sector* detectLastSector(const Fvector& P);
    void detectSectors_sphere(CSector* sector, FixedSet<IRender_Sector*>& m_sectors, const Fvector& b_center, const Fvector& b_dim);
    void detectSectors_frustum(CSector* sector, FixedSet<IRender_Sector*>& m_sectors, CFrustum* _frustum);
    int translateSector(IRender_Sector* pSector);

    IC u32 occq_begin(u32& ID) { return HWOCC.occq_begin(ID); }
    IC void occq_end(u32& ID) { HWOCC.occq_end(ID); }
    IC R_occlusion::occq_result occq_get(u32& ID) { return HWOCC.occq_get(ID); }

    ICF void apply_object(IRenderable* O)
    {
        if (0 == O) return;
        if (0 == O->renderable_ROS()) return;
        CROS_impl& LT = *((CROS_impl*)O->renderable_ROS());
        LT.update_smooth(O);
        RCache.m_ctx->o_hemi = 0.75f * LT.get_hemi();
        RCache.m_ctx->o_sun = 0.75f * LT.get_sun();
        //--DSR-- HeatVision_start
        RCache.m_ctx->hemi.set_hotness(O->GetHotness(), O->GetTransparency(), 0.f, 0.f);
        RCache.m_ctx->hemi.set_glowing(
            sil_glow_color.x,
            sil_glow_color.y,
            sil_glow_color.z, O->GetGlowing());
        //--DSR-- HeatVision_end
        CopyMemory(RCache.m_ctx->o_hemi_cube, LT.get_hemi_cube(), CROS_impl::NUM_FACES*sizeof(float));
    }

    IC void apply_lmaterial()
    {
        R_constant* C = RCache.get_c(c_sbase); // get sampler
        if (0 == C) return;
        VERIFY(RC_dest_sampler == C->destination);
        VERIFY(RC_dx10texture == C->type);
        CTexture* T = RCache.get_ActiveTexture(u32(C->samp.index));
        VERIFY(T);
        float mtl = T ? T->m_material : 0.f;
        RCache.m_ctx->hemi.set_material(RCache.m_ctx->o_hemi, mtl, 0, (mtl + .5f) / 4.f);
        RCache.m_ctx->hemi.set_pos_faces(RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_POS_X], RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_POS_Y], RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_POS_Z]);
        RCache.m_ctx->hemi.set_neg_faces(RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_NEG_X], RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Y], RCache.m_ctx->o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Z]);
    }
    // ── IRender_interface — core frame pump ───────────────────────────────
    virtual void        Render();

    // ── Remaining pure-virtual stubs (fill in as systems are implemented) ─
    virtual GenerationLevel get_generation()                            { return GENERATION_R2; }
    virtual bool is_sun_static()                                        { return true; }
    virtual DWORD get_dx_level()                                        { return 0x000B0000; }
    virtual void create();
    virtual void destroy();
    virtual void reset_begin();
    virtual void reset_end();
    virtual void level_Load(IReader*);
    virtual void level_Unload();
    virtual HRESULT shader_compile(LPCSTR name, DWORD const* pSrcData, UINT SrcDataLen, LPCSTR pFunctionName, LPCSTR pTarget, DWORD Flags, void*& result);
    virtual LPCSTR getShaderPath()                                      { return "r3\\"; }
    virtual ref_shader getShader(int id)
    {
        if (id >= 0 && id < (int)Shaders.size()) return Shaders[id];
        return ref_shader();
    }
    virtual IRender_Sector* getSector(int id);
    virtual IRenderVisual* getVisual(int id);
    virtual IRender_Sector* detectSector(const Fvector& P);
    virtual IRender_Target* getTarget()                                 { return Target; }
    virtual void flush()                                                {}
    virtual void add_Occluder(Fbox2& bb_screenspace)                    {}
    virtual void add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V) {}
    virtual void add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl = 0.f, bool ignore_opt = false, bool random_rotation = true) {}
    virtual void add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation) {}
    virtual void clear_static_wallmarks()                               {}
    virtual void add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start, const Fvector& dir, float size, float ttl = 0.f, bool ignore_opt = false) {}
    virtual void remove_SkeletonWallmarksFromObject(IKinematics* obj)   {}
    virtual void update_Wallmarks()                                     {}
    virtual IBlender* blender_create(CLASS_ID cls);
    virtual void blender_destroy(IBlender*& B);

    virtual IRender_ObjectSpecific* ros_create(IRenderable* parent);
    virtual void ros_destroy(IRender_ObjectSpecific*& p);
    virtual IRender_Light* light_create();
    virtual IRender_Glow* glow_create();
    virtual IRenderVisual* model_CreateParticles(LPCSTR name);
    virtual IRenderVisual* model_Create(LPCSTR name, IReader* data = 0);
    virtual IRenderVisual* model_CreateChild(LPCSTR name, IReader* data);
    virtual IRenderVisual* model_Duplicate(IRenderVisual* V);
    virtual void model_Delete(IRenderVisual*& V, BOOL bDiscard = FALSE);
    virtual void model_Delete_Deffered(IRenderVisual*& V);
    virtual void model_Logging(BOOL bEnable);
    virtual void models_Prefetch();
    virtual void models_PrefetchOne(LPCSTR name, bool assert = true);
    virtual void models_Clear(BOOL b_complete);
    virtual bool models_Exists(LPCSTR name);
    virtual void TakeScreenshot(LPCSTR path, Fvector2 dimensions, DxEncoding encoding = eDXE_A8R8G8B8);
    virtual BOOL occ_visible(vis_data& V)                               { return HOM.visible(V); }
    virtual BOOL occ_visible(Fbox& B)                                   { return HOM.visible(B); }
    virtual BOOL occ_visible(sPoly& P)                                  { return HOM.visible(P); }
    virtual void Calculate();
    virtual void Screenshot(ScreenshotMode mode = SM_NORMAL, LPCSTR name = 0);
    virtual void Screenshot(ScreenshotMode mode, CMemoryWriter& memory_writer);
    virtual void ScreenshotAsyncBegin();
    virtual void ScreenshotAsyncEnd(CMemoryWriter& memory_writer);
    virtual void rmNear()   override;
    virtual void rmFar()    override;
    virtual void rmNormal() override;
    virtual void add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm);
    virtual void add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start, const Fvector& dir, float size);
    virtual void add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start, const Fvector& dir, float size);
    virtual u32 active_phase()                                          { return 0; }
    virtual void RenderToTarget(RRT target);
    void FlushAndCloseUIPass();   // VK: end active pass + clear RT/ZB slots

    // Engine base layout calls we need to stub for the build
    virtual void set_HUD(BOOL V)                                        {}
    virtual BOOL get_HUD()                                              { return FALSE; }
    virtual void set_Invisible(BOOL V)                                  {}
    virtual void set_Object(IRenderable* O)                             {}
    virtual void add_Visual(IRenderVisual* V)                           {}
    virtual void add_Geometry(IRenderVisual* V)                         {}
    virtual void OnFrame()                                              {}

    // ── Level loader helpers ───────────────────────────────────────────────────
    // Mirror the R4/R2 loader decomposition so each subsystem can be ported
    // independently without rewriting level_Load() each time.
    void LoadBuffers (CStreamReader* fs, BOOL _alternative);
    void LoadSWIs    (CStreamReader* fs);
    void LoadVisuals (IReader* fs);
    void LoadLights  (IReader* fs);
    void LoadSectors (IReader* fs);

protected:
    virtual void ScreenshotImpl(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer);
};

extern CRender RImplementation;
