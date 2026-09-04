#ifndef r_backendH
#define r_backendH
#pragma once

//#define RBackend_PGO

#ifdef	RBackend_PGO
#define PGO(a)	a
#else
#define PGO(a)
#endif

#include "r_DStreams.h"
#include "r_constants_cache.h"
#include "r_backend_xform.h"
#include "r_backend_hemi.h"
#include "r_backend_tree.h"

#ifdef USE_DX11
#	include "..\xrRenderPC_R4\r_backend_lod.h"
#endif

#include "fvf.h"

const u32 CULL_CCW = D3DCULL_CCW;
const u32 CULL_CW = D3DCULL_CW;
const u32 CULL_NONE = D3DCULL_NONE;

///		detailed statistic
struct R_statistics_element
{
	u32 verts, dips;
	ICF void add(u32 _verts)
	{
#if defined(USE_VK)
		extern void vk_stat_element_add(R_statistics_element* elem, u32 _verts);
		vk_stat_element_add(this, _verts);
#else
		verts += _verts;
		dips++;
#endif
	}
};

struct R_statistics
{
	R_statistics_element s_static;
	R_statistics_element s_flora;
	R_statistics_element s_flora_lods;
	R_statistics_element s_details;
	R_statistics_element s_ui;
	R_statistics_element s_dynamic;
	R_statistics_element s_dynamic_sw;
	R_statistics_element s_dynamic_inst;
	R_statistics_element s_dynamic_1B;
	R_statistics_element s_dynamic_2B;
	R_statistics_element s_dynamic_3B;
	R_statistics_element s_dynamic_4B;
};

#if defined(USE_VK)
struct VkRecordContext;
#endif

#pragma warning(push)
#pragma warning(disable:4324)
class ECORE_API CBackend
{
#if defined(USE_VK)
    friend class vk_PipelineCacheManager;
    // VK's phase_scene_prepare must reset cached backend state before the scene,
    // because the 3D-PDA path draws ~120 UI primitives first and VK (unlike DX11's
    // per-draw-resetting immediate context) caches state. Grant CRenderTarget access
    // to the otherwise-private Invalidate() instead of making it public globally.
    friend class CRenderTarget;
public:
    static thread_local VkRecordContext* m_ctx;
#endif
public:
#if defined(USE_VK)
    bool m_texturesDirty = true;
    bool m_pipelineDirty = true;
    // Accessors/setters for private texture arrays
    IC CTexture* GetTexturePS(int i) const { return textures_ps[i]; }
    IC CTexture* GetTextureVS(int i) const { return textures_vs[i]; }
    IC void SetTexturePS(int i, CTexture* t) { if (textures_ps[i] != t) { textures_ps[i] = t; m_texturesDirty = true; } }
    IC void SetTextureVS(int i, CTexture* t) { if (textures_vs[i] != t) { textures_vs[i] = t; m_texturesDirty = true; } }
#endif
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	enum MaxTextures
	{
		//	Actually these values are 128
		mtMaxPixelShaderTextures = 16,
		mtMaxVertexShaderTextures = 4,
		mtMaxGeometryShaderTextures = 16,
#	if defined(USE_DX11) || defined(USE_VK)
		mtMaxHullShaderTextures = 16,
		mtMaxDomainShaderTextures = 16,
		mtMaxComputeShaderTextures = 16,
#	endif
	};

	enum
	{
		MaxCBuffers = 14
	};
#else	//	USE_DX10
	enum MaxTextures
	{
		mtMaxPixelShaderTextures = 16,
		mtMaxVertexShaderTextures = 4,
	};
#endif	//	USE_DX10


public:
	// Dynamic geometry streams
	_VertexStream Vertex;
	_IndexStream Index;
	ID3DIndexBuffer* QuadIB;
	ID3DIndexBuffer* old_QuadIB;
	ID3DIndexBuffer* CuboidIB;
	R_xforms xforms;
	R_hemi hemi;
	R_tree tree;
#ifdef USE_DX11
	R_LOD LOD;
#endif

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ref_cbuffer m_aVertexConstants[MaxCBuffers];
	ref_cbuffer m_aPixelConstants[MaxCBuffers];
	ref_cbuffer m_aGeometryConstants[MaxCBuffers];
#	if defined(USE_DX11) || defined(USE_VK)
	ref_cbuffer m_aHullConstants[MaxCBuffers];
	ref_cbuffer m_aDomainConstants[MaxCBuffers];
	ref_cbuffer m_aComputeConstants[MaxCBuffers];
#	endif
	D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology;
	ID3DInputLayout* m_pInputLayout;
	DWORD dummy0; //	Padding to avoid warning
	DWORD dummy1; //	Padding to avoid warning
	DWORD dummy2; //	Padding to avoid warning
#endif

#if defined(USE_VK)
public:
	struct StreamState
	{
		VkBuffer     VBuffer;
		VkDeviceSize Offset;
		u32          Stride;
	};
private:
	VkCommandBuffer m_activeCmdBuffer;
	u32             m_currentImageIndex;
	StreamState     m_boundVertexStreams[4];
	VkBuffer        m_boundIndexStream;
	VkDeviceSize    m_boundIndexOffset;
	VkIndexType     m_boundIndexType;
public:
	void OnFrameBegin(VkCommandBuffer cmdBuffer, u32 imageIndex);
	void InvalidateCtx();
	void SetVertexStream(u32 streamSlot, VkBufferWrapper* buf, VkDeviceSize offset, u32 stride);
	void SetIndexStream(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
	VkCommandBuffer GetActiveCommandBuffer() const;
    void SetActiveCommandBuffer(VkCommandBuffer cmd);
    bool CheckAndResetRenderPassDirty();
    u32 GetCurrentImageIndex() const;
#endif

private:
	// Render-targets
	ID3DRenderTargetView* pRT[4];
	ID3DDepthStencilView* pZB;

	// Vertices/Indices/etc
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	SDeclaration* decl;
#else	//	USE_DX10
	IDirect3DVertexDeclaration9* decl;
#endif	//	USE_DX10
	ID3DVertexBuffer* vb;
	ID3DIndexBuffer* ib;
	u32 vb_stride;

	// Pixel/Vertex constants
	ALIGN(16) R_constants constants;
	R_constant_table* ctable;

	// Shaders/State
	ID3DState* state;
	ID3DPixelShader* ps;
	ID3DVertexShader* vs;
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ID3DGeometryShader* gs;
#	if defined(USE_DX11) || defined(USE_VK)
	ID3D11HullShader* hs;
	ID3D11DomainShader* ds;
	ID3D11ComputeShader* cs;
#	endif
#endif	//	USE_DX10 || USE_VK

#ifdef DEBUG
	LPCSTR							ps_name;
	LPCSTR							vs_name;
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	LPCSTR							gs_name;
#	if defined(USE_DX11) || defined(USE_VK)
	LPCSTR							hs_name;
	LPCSTR							ds_name;
	LPCSTR							cs_name;
#	endif
#endif	//	USE_DX10
#endif
    u32 stencil_enable;
    u32 stencil_func;
    u32 stencil_ref;
    u32 stencil_mask;
    u32 stencil_writemask;
    u32 stencil_fail;
    u32 stencil_pass;
    u32 stencil_zfail;

#ifdef USE_VK
    // Phase A.3 Alpha blending tracking
    u32 blend_enable;
    u32 blend_src;
    u32 blend_dst;
    u32 blend_op;
    u32 blend_src_alpha;
    u32 blend_dst_alpha;
    u32 blend_op_alpha;
#endif
	u32 colorwrite_mask;
	u32 fill_mode;
	u32 cull_mode;
	u32 z_enable;
#ifdef USE_VK
	u32 z_write_enable;
#endif
	u32 z_func;
	u32 alpha_ref;

	// Lists
	STextureList* T;
	SMatrixList* M;
	SConstantList* C;

	// Lists-expanded
	CTexture* textures_ps [mtMaxPixelShaderTextures]; // stages
	//CTexture*						textures_vs	[5	];	// dmap + 4 vs
	CTexture* textures_vs [mtMaxVertexShaderTextures]; // 4 vs
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	CTexture* textures_gs [mtMaxGeometryShaderTextures]; // 4 vs
#	if defined(USE_DX11) || defined(USE_VK)
	CTexture* textures_hs [mtMaxHullShaderTextures]; // 4 vs
	CTexture* textures_ds [mtMaxDomainShaderTextures]; // 4 vs
	CTexture* textures_cs [mtMaxComputeShaderTextures]; // 4 vs
#	endif
#endif	//	USE_DX10 || USE_VK
#ifdef _EDITOR
	CMatrix*						matrices	[8	];	// matrices are supported only for FFP
#endif

	void Invalidate();
public:
	struct _stats
	{
		u32 polys;
		u32 verts;
		u32 calls;
		u32 vs;
		u32 ps;
#ifdef	DEBUG
		u32								decl;
		u32								vb;
		u32								ib;
		u32								states;			// Number of times the shader-state changes
		u32								textures;		// Number of times the shader-tex changes
		u32								matrices;		// Number of times the shader-xform changes
		u32								constants;		// Number of times the shader-consts changes
#endif
		u32 xforms;
		u32 target_rt;
		u32 target_zb;

		R_statistics r;
	} stat;

public:
#if defined(USE_VK)
	IC CTexture* get_ActiveTexture(u32 stage);
#else
	IC CTexture* get_ActiveTexture(u32 stage)
	{
		auto ctx = this;
		CTexture* tex = NULL;
		if (stage < CTexture::rstVertex) tex = ctx->textures_ps[stage];
		else if (stage < CTexture::rstGeometry) tex = ctx->textures_vs[stage - CTexture::rstVertex];
#ifdef USE_DX10
		else tex = ctx->textures_gs[stage - CTexture::rstGeometry];
#elif USE_DX11
		else if (stage < CTexture::rstHull) tex = ctx->textures_gs[stage - CTexture::rstGeometry];
		else if (stage < CTexture::rstDomain) tex = ctx->textures_hs[stage - CTexture::rstHull];
		else if (stage < CTexture::rstCompute) tex = ctx->textures_ds[stage - CTexture::rstDomain];
		else if (stage < CTexture::rstInvalid) tex = ctx->textures_cs[stage - CTexture::rstCompute];
		else
		{
			VERIFY(!"Invalid texture stage");
		}
#endif
		return tex;
	}
#endif

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	IC void get_ConstantDirect(shared_str& n, u32 DataSize, void** pVData, void** pGData, void** pPData);
#else	//USE_DX10
	IC R_constant_array& get_ConstantCache_Vertex() { return constants.a_vertex; }
	IC R_constant_array& get_ConstantCache_Pixel() { return constants.a_pixel; }
#endif	//	USE_DX10

	// API
	IC void set_xform(u32 ID, const Fmatrix& M);
	IC void set_xform_world(const Fmatrix& M);
	IC void set_xform_view(const Fmatrix& M);
	IC void set_xform_project(const Fmatrix& M);

	IC void set_xform_world_prev(const Fmatrix& M);
	IC void set_xform_view_prev(const Fmatrix& M);
	IC void set_xform_project_prev(const Fmatrix& M);

	IC const Fmatrix& get_xform_world();
	IC const Fmatrix& get_xform_view();
	IC const Fmatrix& get_xform_project();

	IC void set_RT(ID3DRenderTargetView* RT, u32 ID = 0);
	IC void set_ZB(ID3DDepthStencilView* ZB);
	IC ID3DRenderTargetView* get_RT(u32 ID = 0);
	IC ID3DDepthStencilView* get_ZB();

	void set_Constants(R_constant_table* _C);
	IC void set_Constants(ref_ctable& _C) { set_Constants(&*_C); }

	//Went from T -> _T
	void set_Textures(STextureList* _T);
	IC void set_Textures(ref_texture_list& _T) { set_Textures(&*_T); }

#ifdef _EDITOR
	IC	void						set_Matrices		(SMatrixList* M);
	IC	void						set_Matrices		(ref_matrix_list& M)				{ set_Matrices(&*M);			}
#endif

	IC void set_Element(ShaderElement* S, u32 pass = 0);
	IC void set_Element(ref_selement& S, u32 pass = 0) { set_Element(&*S, pass); }

	IC void set_Shader(Shader* S, u32 pass = 0);
	IC void set_Shader(ref_shader& S, u32 pass = 0) { set_Shader(&*S, pass); }

	ICF void set_States(ID3DState* _state);
#ifdef USE_VK
	void set_States(ref_state& _state);
#else
	ICF void set_States(ref_state& _state) { set_States(_state->state); }
#endif

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_Format(SDeclaration* _decl);
#else	//	USE_DX10
	ICF void set_Format(IDirect3DVertexDeclaration9* _decl);
#endif	//	USE_DX10

	ICF void set_PS(ID3DPixelShader* _ps, LPCSTR _n = 0);
	ICF void set_PS(ref_ps& _ps) { set_PS(_ps ? _ps->ps : nullptr, _ps ? _ps->cName.c_str() : nullptr); }

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_GS(ID3DGeometryShader* _gs, LPCSTR _n = 0);
	ICF void set_GS(ref_gs& _gs) { set_GS(_gs ? _gs->gs : nullptr, _gs ? _gs->cName.c_str() : nullptr); }

#	if defined(USE_DX11) || defined(USE_VK)
	ICF void set_HS(ID3D11HullShader* _hs, LPCSTR _n = 0);
	ICF void set_HS(ref_hs& _hs) { set_HS(_hs ? _hs->sh : nullptr, _hs ? _hs->cName.c_str() : nullptr); }

	ICF void set_DS(ID3D11DomainShader* _ds, LPCSTR _n = 0);
	ICF void set_DS(ref_ds& _ds) { set_DS(_ds ? _ds->sh : nullptr, _ds ? _ds->cName.c_str() : nullptr); }

	ICF void set_CS(ID3D11ComputeShader* _cs, LPCSTR _n = 0);
	ICF void set_CS(ref_cs& _cs) { set_CS(_cs ? _cs->sh : nullptr, _cs ? _cs->cName.c_str() : nullptr); }
#	endif

#endif	//	USE_DX10

#if defined(USE_DX11) || defined(USE_VK)
	ICF bool is_TessEnabled();
#else
	ICF bool is_TessEnabled() { return false; }
#endif

	ICF void set_VS(ref_vs& _vs);
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_VS(SVS* _vs);
protected: //	In DX10 we need input shader signature which is stored in ref_vs
#endif	//	USE_DX10
	ICF void set_VS(ID3DVertexShader* _vs, LPCSTR _n = 0);
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
public:
#endif	//	USE_DX10

	ICF void set_Vertices(ID3DVertexBuffer* _vb, u32 _vb_stride);
	ICF void set_Indices(ID3DIndexBuffer* _ib);
	ICF void set_Geometry(SGeometry* _geom);
	ICF void set_Geometry(ref_geom& _geom) { set_Geometry(&*_geom); }
	IC void set_Stencil(u32 _enable, u32 _func = D3DCMP_ALWAYS, u32 _ref = 0x00, u32 _mask = 0x00,
	                    u32 _writemask = 0x00, u32 _fail = D3DSTENCILOP_KEEP, u32 _pass = D3DSTENCILOP_KEEP,
	                    u32 _zfail = D3DSTENCILOP_KEEP);
	IC void set_Z(u32 _enable);
#ifdef USE_VK
	IC void set_ZWrite(u32 _enable);
#endif
	IC void set_ZFunc(u32 _func);
	IC void set_AlphaRef(u32 _value);
	IC void set_ColorWriteEnable(
		u32 _mask = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
			D3DCOLORWRITEENABLE_ALPHA);
	IC void set_FillMode(u32 _mode);
	IC void set_CullMode(u32 _mode);
	IC u32 get_CullMode() { return cull_mode; }
	void set_ClipPlanes(u32 _enable, Fplane* _planes = NULL, u32 count = 0);
	void set_ClipPlanes(u32 _enable, Fmatrix* _xform = NULL, u32 fmask = 0xff);
	IC void set_Scissor(Irect* rect = NULL);

#ifdef USE_VK
    // Explicit alpha-blend state for UI/2D passes whose shader-cache blenders
    // don't record blend into SimulatorStates. Mirrors D3DRS_* enum values so
    // set_States and this path populate identical fields.
    // D3DBLEND_SRCALPHA=5, D3DBLEND_INVSRCALPHA=6, D3DBLENDOP_ADD=1, D3DBLEND_ONE=2, D3DBLEND_ZERO=1
    void set_Blend(u32 enable,
                   u32 src   = 5,  u32 dst   = 6,
                   u32 op    = 1,
                   u32 srcA  = 2,  u32 dstA  = 1,
                   u32 opA   = 1);
#endif

	// constants
#if defined(USE_VK)
	ICF R_constant* get_c(LPCSTR n);
	ICF R_constant* get_c(shared_str& n);
#else
	ICF R_constant* get_c(LPCSTR n)
	{
		R_constant_table* ct = ctable;
		if (ct) return ct->get(n);
		else return nullptr;
	}

	ICF R_constant* get_c(shared_str& n)
	{
		R_constant_table* ct = ctable;
		if (ct) return ct->get(n);
		else return nullptr;
	}
#endif

	// constants - direct (fast)
	// went from C -> _C
	ICF void set_c(R_constant* _C, const Fmatrix& A) { if (_C) constants.set(_C, A); }
	ICF void set_c(R_constant* _C, const Fvector4& A) { if (_C) constants.set(_C, A); }
	ICF void set_c(R_constant* _C, float x, float y, float z, float w) { if (_C) constants.set(_C, x, y, z, w); }
	ICF void set_ca(R_constant* _C, u32 e, const Fmatrix& A) { if (_C) constants.seta(_C, e, A); }
	ICF void set_ca(R_constant* _C, u32 e, const Fvector4& A) { if (_C) constants.seta(_C, e, A); }
	ICF void set_ca(R_constant* _C, u32 e, float x, float y, float z, float w)
	{
		if (_C) constants.seta(_C, e, x, y, z, w);
	}
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_c(R_constant* _C, float A) { if (_C) constants.set(_C, A); }
	ICF void set_c(R_constant* _C, int A) { if (_C) constants.set(_C, A); }
#endif	//	USE_DX10


	// constants - LPCSTR (slow)
	ICF void set_c(LPCSTR n, const Fmatrix& A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(LPCSTR n, const Fvector4& A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(LPCSTR n, float x, float y, float z, float w) { if (R_constant* C = get_c(n)) set_c(C, x, y, z, w); }
	ICF void set_ca(LPCSTR n, u32 e, const Fmatrix& A) { if (R_constant* C = get_c(n)) set_ca(C, e, A); }
	ICF void set_ca(LPCSTR n, u32 e, const Fvector4& A) { if (R_constant* C = get_c(n)) set_ca(C, e, A); }
	ICF void set_ca(LPCSTR n, u32 e, float x, float y, float z, float w)
	{
		if (R_constant* C = get_c(n)) set_ca(C, e, x, y, z, w);
	}
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_c(LPCSTR n, float A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(LPCSTR n, int A) { if (R_constant* C = get_c(n)) set_c(C, A); }
#endif	//	USE_DX10

	// constants - shared_str (average)
	ICF void set_c(shared_str& n, const Fmatrix& A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(shared_str& n, const Fvector4& A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(shared_str& n, float x, float y, float z, float w)
	{
		if (R_constant* C = get_c(n)) set_c(C, x, y, z, w);
	}

	ICF void set_ca(shared_str& n, u32 e, const Fmatrix& A) { if (R_constant* C = get_c(n)) set_ca(C, e, A); }
	ICF void set_ca(shared_str& n, u32 e, const Fvector4& A) { if (R_constant* C = get_c(n)) set_ca(C, e, A); }
	ICF void set_ca(shared_str& n, u32 e, float x, float y, float z, float w)
	{
		if (R_constant* C = get_c(n)) set_ca(C, e, x, y, z, w);
	}
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	ICF void set_c(shared_str& n, float A) { if (R_constant* C = get_c(n)) set_c(C, A); }
	ICF void set_c(shared_str& n, int A) { if (R_constant* C = get_c(n)) set_c(C, A); }
#endif	//	USE_DX10

	ICF void Render(D3DPRIMITIVETYPE T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC);
	ICF void Render(D3DPRIMITIVETYPE T, u32 startV, u32 PC);

#if defined(USE_DX11) || defined(USE_VK)
	ICF void Compute(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
#endif

	// Device create / destroy / frame signaling
	void RestoreQuadIBData(); // Igor: is used to test bug with rain, particles corruption
	void CreateQuadIB();
	void OnFrameBegin();
	void OnFrameEnd();
	void OnDeviceCreate();
	void OnDeviceDestroy();

	// Debug render
	void dbg_DP(D3DPRIMITIVETYPE pt, ref_geom geom, u32 vBase, u32 pc);
	void dbg_DIP(D3DPRIMITIVETYPE pt, ref_geom geom, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC);
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
	//	TODO: DX10: Implement this.
	IC void dbg_SetRS(D3DRENDERSTATETYPE p1, u32 p2)
	{
		VERIFY(!"Not implemented");
	}

	IC void dbg_SetSS(u32 sampler, D3DSAMPLERSTATETYPE type, u32 value)
	{
		VERIFY(!"Not implemented");
	}
#else	//	USE_DX10
	IC void dbg_SetRS(D3DRENDERSTATETYPE p1, u32 p2)
	{
		CHK_DX(HW.pDevice->SetRenderState(p1,p2));
	}

	IC void dbg_SetSS(u32 sampler, D3DSAMPLERSTATETYPE type, u32 value)
	{
		CHK_DX(HW.pDevice->SetSamplerState(sampler,type,value));
	}
#endif	//	USE_DX10

	void dbg_Draw					(D3DPRIMITIVETYPE T, FVF::L* pVerts, int vcnt, u16* pIdx, int pcnt);
	void dbg_Draw					(D3DPRIMITIVETYPE T, FVF::L* pVerts, int pcnt);
	IC void dbg_DrawAABB			(Fvector& T, float sx, float sy, float sz, u32 C)						{	Fvector half_dim;	half_dim.set(sx,sy,sz); Fmatrix	TM;	TM.translate(T); dbg_DrawOBB(TM,half_dim,C);	}
	void dbg_DrawOBB				(Fmatrix& T, Fvector& half_dim, u32 C);
	IC void dbg_DrawTRI				(Fmatrix& T, Fvector* p, u32 C)											{	dbg_DrawTRI(T,p[0],p[1],p[2],C);	}
	void dbg_DrawTRI				(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C);
	void dbg_DrawLINE				(Fmatrix& T, Fvector& p1, Fvector& p2, u32 C);
	void dbg_DrawEllipse			(Fmatrix& T, u32 C);

	CBackend() { Invalidate(); };

private:
	// Debug Draw
	void InitDebugDraw();
	void DestroyDebugDraw();

	// DX9 doesn't need this
#if defined(USE_DX10) || defined(USE_DX11)
	ref_geom vs_L;
	ref_geom vs_TL;
#endif

#if defined(USE_DX10) || defined(USE_DX11)
private:
	//	DirectX 10 internal functionality
	//void CreateConstantBuffers();
	//void DestroyConstantBuffers();
	void ApplyVertexLayout();
	void ApplyRTandZB();
	void ApplyPrimitieTopology(D3D_PRIMITIVE_TOPOLOGY Topology);
	bool CBuffersNeedUpdate(ref_cbuffer buf1[MaxCBuffers], ref_cbuffer buf2[MaxCBuffers], u32& uiMin, u32& uiMax);

private:
	ID3DBlob* m_pInputSignature;

	bool m_bChangedRTorZB;
#endif	//	USE_DX10
};
#pragma warning(pop)

extern ECORE_API CBackend RCache;

#ifndef _EDITOR
#	include "D3DUtils.h"
#endif

#endif
