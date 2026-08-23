#include "stdafx.h"
#pragma hdrstop

#ifndef _EDITOR
#include "../../xrEngine/render.h"
#endif

#include "../xrRender/ResourceManager.h"
#include "../xrRender/tss.h"
#include "../xrRender/blenders/blender.h"
#include "../xrRender/blenders/blender_recorder.h"
#include "../xrRender/ShaderResourceTraits.h"
#include "../xrRenderDX10/dx10ConstantBuffer.h"
#include "vk_BufferUtils.h"
#include "vk_ShaderReflection.h"

void fix_texture_name(LPSTR fn)
{
    LPSTR ext = strext(fn);
    if (!ext) return;
    if (stricmp(ext, ".tga") == 0 ||
        stricmp(ext, ".dds") == 0 ||
        stricmp(ext, ".bmp") == 0 ||
        stricmp(ext, ".ogm") == 0 ||
        stricmp(ext, ".gif") == 0)
    {
        *ext = 0;
    }
}

template <class T>
BOOL reclaim(xr_vector<T*>& vec, const T* ptr)
{
    typename xr_vector<T*>::iterator it = vec.begin();
    typename xr_vector<T*>::iterator end = vec.end();
    for (; it != end; it++)
        if (*it == ptr)
        {
            vec.erase(it);
            return TRUE;
        }
    return FALSE;
}

static BOOL dcl_equal(D3DVERTEXELEMENT9* a, D3DVERTEXELEMENT9* b)
{
    u32 a_size = vk_GetDeclLength(a);
    u32 b_size = vk_GetDeclLength(b);
    if (a_size != b_size) return FALSE;
    return 0 == memcmp(a, b, a_size * sizeof(D3DVERTEXELEMENT9));
}

//--------------------------------------------------------------------------------------------------------------
// VK: m_hs/m_ds/m_cs maps and CreateShader<T> template are #ifdef USE_DX11 only.
// Hull/domain/compute shaders are unused in the VK deferred pipeline.
SHS* CResourceManager::_CreateHS(LPCSTR Name)
{
    SHS* sh = xr_new<SHS>(); sh->sh = nullptr;
    sh->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    if (0 != stricmp(Name, "null")) Msg("~ [VK] _CreateHS: '%s' unused in VK path", Name);
    return sh;
}
void CResourceManager::_DeleteHS(const SHS* /*HS*/) {}

SDS* CResourceManager::_CreateDS(LPCSTR Name)
{
    SDS* sh = xr_new<SDS>(); sh->sh = nullptr;
    sh->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    if (0 != stricmp(Name, "null")) Msg("~ [VK] _CreateDS: '%s' unused in VK path", Name);
    return sh;
}
void CResourceManager::_DeleteDS(const SDS* /*DS*/) {}

SCS* CResourceManager::_CreateCS(LPCSTR Name)
{
    SCS* sh = xr_new<SCS>(); sh->sh = nullptr;
    sh->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    if (0 != stricmp(Name, "null")) Msg("~ [VK] _CreateCS: '%s' unused in VK path", Name);
    return sh;
}
void CResourceManager::_DeleteCS(const SCS* /*CS*/) {}

//--------------------------------------------------------------------------------------------------------------
SState* CResourceManager::_CreateState(SimulatorStates& state_code)
{
    xrCriticalSectionGuard guard(creationGuard);

    for (u32 it = 0; it < v_states.size(); it++)
    {
        SState* C = v_states[it];
        SimulatorStates& base = C->state_code;
        if (base.equal(state_code)) return C;
    }

    v_states.push_back(xr_new<SState>());
    v_states.back()->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_states.back()->state = ID3DState::Create(state_code);
    v_states.back()->state_code = state_code;
    return v_states.back();
}

void CResourceManager::_DeleteState(const SState* state)
{
    if (0 == (state->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(v_states, state)) return;
    Msg("! ERROR: Failed to find compiled stateblock");
}

//--------------------------------------------------------------------------------------------------------------
SPass* CResourceManager::_CreatePass(const SPass& proto)
{
    xrCriticalSectionGuard guard(creationGuard);
    for (u32 it = 0; it < v_passes.size(); it++)
        if (v_passes[it]->equal(proto))
            return v_passes[it];

    SPass* P = xr_new<SPass>();
    P->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    P->state = proto.state;
    P->ps = proto.ps;
    P->vs = proto.vs;
    P->gs = proto.gs;
#if defined(USE_DX11) || defined(USE_VK)
    P->hs = proto.hs;
    P->ds = proto.ds;
    P->cs = proto.cs;
#endif
    P->constants = proto.constants;
    P->T = proto.T;
#ifdef _EDITOR
    P->M = proto.M;
#endif
    P->C = proto.C;

    v_passes.push_back(P);
    return v_passes.back();
}

void CResourceManager::_DeletePass(const SPass* P)
{
    if (0 == (P->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(v_passes, P)) return;
    Msg("! ERROR: Failed to find compiled pass");
}

//--------------------------------------------------------------------------------------------------------------
SVS* CResourceManager::_CreateVS(LPCSTR _name)
{
    xrCriticalSectionGuard guard(creationGuard);
    xr_string res_name = _name;

    const int m_skinning = Engine.External.GetSkinningMode();
    if (m_skinning > 0)
    {
        res_name += "_" + xr_string::ToString(m_skinning);
    }

    LPCSTR name = res_name.c_str();
    LPSTR N = LPSTR(name);
    map_VS::iterator I = m_vs.find(N);
    if (I != m_vs.end()) return I->second;
    else
    {
        SVS* _vs = xr_new<SVS>();
        _vs->skinning = m_skinning;
        _vs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        m_vs.insert(mk_pair(_vs->set_name(name), _vs));
        if (0 == stricmp(_name, "null"))
        {
            return _vs;
        }

        string_path shName;
        {
            const char* pchr = strchr(_name, '(');
            ptrdiff_t size = pchr ? pchr - _name : xr_strlen(_name);
            strncpy(shName, _name, size);
            shName[size] = 0;
        }

        string_path cname;
        strconcat(sizeof(cname), cname, ::Render->getShaderPath(), shName, ".vs");
        FS.update_path(cname, "$game_shaders$", cname);

        IReader* file = FS.r_open(cname);
        if (!file)
        {
            string1024 tmp;
            xr_sprintf(tmp, "VK: %s is missing. Replace with stub_default.vs", cname);
            Msg(tmp);
            strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".vs");
            FS.update_path(cname, "$game_shaders$", cname);
            file = FS.r_open(cname);
        }
        u32 const size = file->length();
        char* const data = (LPSTR)_alloca(size + 1);
        CopyMemory(data, file->pointer(), size);
        data[size] = 0;
        FS.r_close(file);

        LPCSTR c_target = "vs_2_0";
        LPCSTR c_entry = "main";

        if (strstr(data, "main_vs_1_1"))
        {
            c_target = "vs_1_1";
            c_entry = "main_vs_1_1";
        }
        if (strstr(data, "main_vs_2_0"))
        {
            c_target = "vs_2_0";
            c_entry = "main_vs_2_0";
        }

        HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)data, size, c_entry, c_target,
                                                     0, (void*&)_vs->vs);

        VERIFY(SUCCEEDED(_hr));
        if (SUCCEEDED(_hr) && _vs->vs && _vs->vs->pSPIRV) {
            vk_ShaderReflection::ReflectSPIRV((const uint32_t*)_vs->vs->pSPIRV, _vs->vs->size, &_vs->constants, RC_dest_vertex);
        }

        CHECK_OR_EXIT(
            !FAILED(_hr),
            make_string("Shader compilation failed, check your log file for additional information.")
        );

        return _vs;
    }
}

void CResourceManager::_DeleteVS(const SVS* vs)
{
    if (0 == (vs->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*vs->cName);
    map_VS::iterator I = m_vs.find(N);
    if (I != m_vs.end())
    {
        m_vs.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find compiled vertex-shader '%s'", *vs->cName);
}

//--------------------------------------------------------------------------------------------------------------
SPS* CResourceManager::_CreatePS(LPCSTR _name)
{
    xrCriticalSectionGuard guard(creationGuard);
    string_path name;
    xr_strcpy(name, _name);
    if (0 == ::Render->m_MSAASample) xr_strcat(name, "_0");
    if (1 == ::Render->m_MSAASample) xr_strcat(name, "_1");
    if (2 == ::Render->m_MSAASample) xr_strcat(name, "_2");
    if (3 == ::Render->m_MSAASample) xr_strcat(name, "_3");
    if (4 == ::Render->m_MSAASample) xr_strcat(name, "_4");
    if (5 == ::Render->m_MSAASample) xr_strcat(name, "_5");
    if (6 == ::Render->m_MSAASample) xr_strcat(name, "_6");
    if (7 == ::Render->m_MSAASample) xr_strcat(name, "_7");
    LPSTR N = LPSTR(name);
    map_PS::iterator I = m_ps.find(N);
    if (I != m_ps.end()) return I->second;
    else
    {
        SPS* _ps = xr_new<SPS>();
        _ps->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        m_ps.insert(mk_pair(_ps->set_name(name), _ps));
        if (0 == stricmp(_name, "null"))
        {
            _ps->ps = NULL;
            return _ps;
        }

        string_path shName;
        const char* pchr = strchr(_name, '(');
        ptrdiff_t strSize = pchr ? pchr - _name : xr_strlen(_name);
        strncpy(shName, _name, strSize);
        shName[strSize] = 0;

        string_path cname;
        strconcat(sizeof(cname), cname, ::Render->getShaderPath(), shName, ".ps");
        FS.update_path(cname, "$game_shaders$", cname);

        IReader* file = FS.r_open(cname);
        if (!file)
        {
            string1024 tmp;
            xr_sprintf(tmp, "VK: %s is missing. Replace with stub_default.ps", cname);
            Msg(tmp);
            strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".ps");
            FS.update_path(cname, "$game_shaders$", cname);
            file = FS.r_open(cname);
        }

        R_ASSERT2(file, cname);
        u32 const size = file->length();
        char* const data = (LPSTR)_alloca(size + 1);
        CopyMemory(data, file->pointer(), size);
        data[size] = 0;
        FS.r_close(file);

        LPCSTR c_target = "ps_2_0";
        LPCSTR c_entry = "main";
        if (strstr(data, "main_ps_1_1"))
        {
            c_target = "ps_1_1";
            c_entry = "main_ps_1_1";
        }
        if (strstr(data, "main_ps_1_2"))
        {
            c_target = "ps_1_2";
            c_entry = "main_ps_1_2";
        }
        if (strstr(data, "main_ps_1_3"))
        {
            c_target = "ps_1_3";
            c_entry = "main_ps_1_3";
        }
        if (strstr(data, "main_ps_1_4"))
        {
            c_target = "ps_1_4";
            c_entry = "main_ps_1_4";
        }
        if (strstr(data, "main_ps_2_0"))
        {
            c_target = "ps_2_0";
            c_entry = "main_ps_2_0";
        }

        HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)data, size, c_entry, c_target,
                                                     0, (void*&)_ps->ps);

        VERIFY(SUCCEEDED(_hr));
        if (SUCCEEDED(_hr) && _ps->ps && _ps->ps->pSPIRV) {
            vk_ShaderReflection::ReflectSPIRV((const uint32_t*)_ps->ps->pSPIRV, _ps->ps->size, &_ps->constants, RC_dest_pixel);
        }

        CHECK_OR_EXIT(
            !FAILED(_hr),
            make_string("Shader compilation failed, check your log file for additional information.")
        );

        return _ps;
    }
}

void CResourceManager::_DeletePS(const SPS* ps)
{
    if (0 == (ps->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*ps->cName);
    map_PS::iterator I = m_ps.find(N);
    if (I != m_ps.end())
    {
        m_ps.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find compiled pixel-shader '%s'", *ps->cName);
}

//--------------------------------------------------------------------------------------------------------------
SGS* CResourceManager::_CreateGS(LPCSTR name)
{
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(name);
    map_GS::iterator I = m_gs.find(N);
    if (I != m_gs.end()) return I->second;
    else
    {
        SGS* _gs = xr_new<SGS>();
        _gs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        m_gs.insert(mk_pair(_gs->set_name(name), _gs));
        if (0 == stricmp(name, "null"))
        {
            _gs->gs = NULL;
            return _gs;
        }

        string_path cname;
        strconcat(sizeof(cname), cname, ::Render->getShaderPath(), name, ".gs");
        FS.update_path(cname, "$game_shaders$", cname);

        IReader* file = FS.r_open(cname);
        if (!file)
        {
            string1024 tmp;
            xr_sprintf(tmp, "VK: %s is missing. Replace with stub_default.gs", cname);
            Msg(tmp);
            strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".gs");
            FS.update_path(cname, "$game_shaders$", cname);
            file = FS.r_open(cname);
        }

        R_ASSERT2(file, cname);

        LPCSTR c_target = "gs_4_0";
        LPCSTR c_entry = "main";

        HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)file->pointer(), file->length(), c_entry,
                                                     c_target, 0, (void*&)_gs->gs);

        VERIFY(SUCCEEDED(_hr));
        if (SUCCEEDED(_hr) && _gs->gs && _gs->gs->pSPIRV) {
            vk_ShaderReflection::ReflectSPIRV((const uint32_t*)_gs->gs->pSPIRV, _gs->gs->size, &_gs->constants, RC_dest_geometry);
        }

        FS.r_close(file);

        CHECK_OR_EXIT(
            !FAILED(_hr),
            make_string("Shader compilation failed, check your log file for additional information.")
        );

        return _gs;
    }
}

void CResourceManager::_DeleteGS(const SGS* gs)
{
    if (0 == (gs->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*gs->cName);
    map_GS::iterator I = m_gs.find(N);
    if (I != m_gs.end())
    {
        m_gs.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find compiled geometry shader '%s'", *gs->cName);
}

//--------------------------------------------------------------------------------------------------------------
SDeclaration* CResourceManager::_CreateDecl(D3DVERTEXELEMENT9* dcl)
{
    xrCriticalSectionGuard guard(creationGuard);
    for (u32 it = 0; it < v_declarations.size(); it++)
    {
        SDeclaration* D = v_declarations[it];
        if (dcl_equal(dcl, &*D->dcl_code.begin())) return D;
    }

    SDeclaration* D = xr_new<SDeclaration>();
    u32 dcl_size = vk_GetDeclLength(dcl) + 1;
    D->dcl_code.assign(dcl, dcl + dcl_size);
    D->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_declarations.push_back(D);
    return D;
}

void CResourceManager::_DeleteDecl(const SDeclaration* dcl)
{
    if (0 == (dcl->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(v_declarations, dcl)) return;
    Msg("! ERROR: Failed to find compiled vertex-declarator");
}

//--------------------------------------------------------------------------------------------------------------
R_constant_table* CResourceManager::_CreateConstantTable(R_constant_table& C)
{
    if (C.empty()) return NULL;

    xrCriticalSectionGuard guard(creationGuard);

    for (u32 it = 0; it < v_constant_tables.size(); it++)
        if (v_constant_tables[it]->equal(C))
            return v_constant_tables[it];

    auto NewElem = xr_new<R_constant_table>(C);
    NewElem->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_constant_tables.push_back(NewElem);
    return NewElem;
}

void CResourceManager::_DeleteConstantTable(const R_constant_table* C)
{
    if (0 == (C->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(v_constant_tables, C)) return;
    Msg("! ERROR: Failed to find compiled constant-table");
}


//--------------------------------------------------------------------------------------------------------------
CRT* CResourceManager::_CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount, bool useUAV)
{
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CResourceManager::_CreateRT: %s", Name); xrLogger::FlushLog(); }
    R_ASSERT(Name && Name[0] && w && h);

    LPSTR N = LPSTR(Name);
    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CResourceManager::_CreateRT: locking creationGuard"); xrLogger::FlushLog(); }
    xrCriticalSectionGuard guard(creationGuard);

    if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CResourceManager::_CreateRT: searching m_rtargets"); xrLogger::FlushLog(); }
    map_RT::iterator I = m_rtargets.find(N);
    if (I != m_rtargets.end()) return I->second;
    else
    {
        if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CResourceManager::_CreateRT: allocating new CRT"); xrLogger::FlushLog(); }
        CRT* RT = xr_new<CRT>();
        RT->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        m_rtargets.insert(mk_pair(RT->set_name(Name), RT));
        if (Device.b_is_Ready) {
            if (strstr(Core.Params, "-vkdebug")) { Msg("~ VK DEBUG CResourceManager::_CreateRT: calling RT->create"); xrLogger::FlushLog(); }
            RT->create(Name, w, h, f, SampleCount);
        }
        return RT;
    }
}

void CResourceManager::_DeleteRT(const CRT* RT)
{
    if (0 == (RT->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    LPSTR N = LPSTR(*RT->cName);
    xrCriticalSectionGuard guard(creationGuard);
    map_RT::iterator I = m_rtargets.find(N);
    if (I != m_rtargets.end())
    {
        m_rtargets.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find render-target '%s'", *RT->cName);
}

//--------------------------------------------------------------------------------------------------------------
void CResourceManager::DBG_VerifyGeoms() {}

// Inline FVF decoder — replaces D3DXDeclaratorFromFVF (d3dx9.h, not d3d9.h)
// Non-static so FVisual.cpp can call it under USE_VK (forward-declared there).
void vk_FVFToDecl(u32 FVF, D3DVERTEXELEMENT9* dcl)
{
    int i = 0; WORD off = 0;
    if (FVF & D3DFVF_XYZRHW)   { dcl[i++] = {0,off,D3DDECLTYPE_FLOAT4,  0,9 /*D3DDECLUSAGE_POSITIONT*/,0}; off+=16; }
    else if (FVF & D3DFVF_XYZ) { dcl[i++] = {0,off,D3DDECLTYPE_FLOAT3,  0,D3DDECLUSAGE_POSITION,0}; off+=12; }
    if (FVF & D3DFVF_NORMAL)   { dcl[i++] = {0,off,D3DDECLTYPE_FLOAT3,  0,D3DDECLUSAGE_NORMAL,  0}; off+=12; }
    if (FVF & D3DFVF_DIFFUSE)  { dcl[i++] = {0,off,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,   0}; off+= 4; }
    if (FVF & D3DFVF_SPECULAR) { dcl[i++] = {0,off,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,   1}; off+= 4; }
    u32 tc = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (u32 t = 0; t < tc; ++t) {
        u32 format = (FVF >> (16 + (t * 2))) & 0x3;
        BYTE type = D3DDECLTYPE_FLOAT2;
        WORD size = 8;
        if (format == 0)      { type = D3DDECLTYPE_FLOAT2; size = 8; }
        else if (format == 1) { type = D3DDECLTYPE_FLOAT3; size = 12; }
        else if (format == 2) { type = D3DDECLTYPE_FLOAT4; size = 16; }
        else if (format == 3) { type = D3DDECLTYPE_FLOAT1; size = 4; }
        
        dcl[i++] = {0, off, type, 0, D3DDECLUSAGE_TEXCOORD, (BYTE)t};
        off += size;
    }
    D3DVERTEXELEMENT9 end = D3DDECL_END(); dcl[i] = end;
}

SGeometry* CResourceManager::CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    xrCriticalSectionGuard guard(creationGuard);
    R_ASSERT(decl && vb);

    SDeclaration* dcl = _CreateDecl(decl);
    u32 vb_stride = vk_GetDeclVertexSize(decl, 0);

    for (u32 it = 0; it < v_geoms.size(); it++)
    {
        SGeometry& G = *(v_geoms[it]);
        if ((G.dcl == dcl) && (G.vb == vb) && (G.ib == ib) && (G.vb_stride == vb_stride)) return v_geoms[it];
    }

    SGeometry* Geom = xr_new<SGeometry>();
    Geom->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    Geom->dcl = dcl;
    Geom->vb = vb;
    Geom->vb_stride = vb_stride;
    Geom->ib = ib;
    v_geoms.push_back(Geom);
    return Geom;
}

SGeometry* CResourceManager::CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    D3DVERTEXELEMENT9 dcl[MAXD3DDECLLENGTH + 1];
    vk_FVFToDecl(FVF, dcl);
    return CreateGeom(dcl, vb, ib);
}

void CResourceManager::DeleteGeom(const SGeometry* Geom)
{
    if (0 == (Geom->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(v_geoms, Geom)) return;
    Msg("! ERROR: Failed to find compiled geometry-declaration");
}

//--------------------------------------------------------------------------------------------------------------
xr_task_group textures_load_tasks;
CTexture* CResourceManager::_CreateTexture(LPCSTR _Name)
{
    PROF_EVENT("_CreateTexture");
    if (0 == xr_strcmp(_Name, "null")) return 0;
    R_ASSERT(_Name && _Name[0]);
    string_path Name;
    xr_strcpy(Name, _Name);
    xrCriticalSectionGuard guard(creationGuard);
    fix_texture_name(Name);

    LPSTR N = LPSTR(Name);
    map_TextureIt I = m_textures.find(N);
    if (I != m_textures.end()) return I->second;
    else
    {
        CTexture* T = xr_new<CTexture>();
        T->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        m_textures.insert(mk_pair(T->set_name(Name), T));
        T->Preload();
        if (Device.b_is_Ready)
        {
            static DWORD this_thread_id = 0;
            this_thread_id = GetCurrentThreadId();
            if (strstr(Core.Params, "-vkdebug"))
                Msg("VK DEBUG Queuing texture load for: %s", *T->cName);

            textures_load_tasks.run([=]()
            {
                if (this_thread_id != GetCurrentThreadId()) { PROF_THREAD("X-Ray PPL Thread") }
                if (strstr(Core.Params, "-vkdebug"))
                    Msg("VK DEBUG Starting thread texture load for: %s", *T->cName);

                T->Load();
            });
        }
        return T;
    }
}

void CResourceManager::_DeleteTexture(const CTexture* T)
{
    //The Fix:
    //As outlined in the engine guidelines, if we capture a raw pointer in a background task,
    //the main thread must wait for the task queue to flush
    //before deleting the resource.
    //Added textures_load_tasks.wait(); to the top of _DeleteTexture() in
    //vk_ResourceManager_Resources.cpp
    textures_load_tasks.wait();
    if (0 == (T->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*T->cName);
    map_Texture::iterator I = m_textures.find(N);
    if (I != m_textures.end())
    {
        m_textures.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find texture surface '%s'", *T->cName);
}

#ifdef DEBUG
void CResourceManager::DBG_VerifyTextures()
{
    map_Texture::iterator I = m_textures.begin();
    map_Texture::iterator E = m_textures.end();
    for (; I != E; I++)
    {
        R_ASSERT(I->first);
        R_ASSERT(I->second);
        R_ASSERT(I->second->cName);
        R_ASSERT(0 == xr_strcmp(I->first, *I->second->cName));
    }
}
#endif

//--------------------------------------------------------------------------------------------------------------
CMatrix* CResourceManager::_CreateMatrix(LPCSTR Name)
{
    R_ASSERT(Name && Name[0]);
    if (0 == stricmp(Name, "$null")) return NULL;

    LPSTR N = LPSTR(Name);
    xrCriticalSectionGuard guard(creationGuard);
    map_Matrix::iterator I = m_matrices.find(N);
    if (I != m_matrices.end()) return I->second;
    else
    {
        CMatrix* M = xr_new<CMatrix>();
        M->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        M->dwReference.store(1, std::memory_order_relaxed);
        m_matrices.insert(mk_pair(M->set_name(Name), M));
        return M;
    }
}

void CResourceManager::_DeleteMatrix(const CMatrix* M)
{
    if (0 == (M->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*M->cName);
    map_Matrix::iterator I = m_matrices.find(N);
    if (I != m_matrices.end())
    {
        m_matrices.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find xform-def '%s'", *M->cName);
}

//--------------------------------------------------------------------------------------------------------------
CConstant* CResourceManager::_CreateConstant(LPCSTR Name)
{
    R_ASSERT(Name && Name[0]);
    if (0 == stricmp(Name, "$null")) return NULL;

    LPSTR N = LPSTR(Name);
    xrCriticalSectionGuard guard(creationGuard);
    map_Constant::iterator I = m_constants.find(N);
    if (I != m_constants.end()) return I->second;
    else
    {
        CConstant* C = xr_new<CConstant>();
        C->dwFlags |= xr_resource_flagged::RF_REGISTERED;
        C->dwReference.store(1, std::memory_order_relaxed);
        m_constants.insert(mk_pair(C->set_name(Name), C));
        return C;
    }
}

void CResourceManager::_DeleteConstant(const CConstant* C)
{
    if (0 == (C->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    LPSTR N = LPSTR(*C->cName);
    map_Constant::iterator I = m_constants.find(N);
    if (I != m_constants.end())
    {
        m_constants.erase(I);
        return;
    }
    Msg("! ERROR: Failed to find R1-constant-def '%s'", *C->cName);
}

//--------------------------------------------------------------------------------------------------------------
bool cmp_tl(const std::pair<u32, ref_texture>& _1, const std::pair<u32, ref_texture>& _2)
{
    return _1.first < _2.first;
}

STextureList* CResourceManager::_CreateTextureList(STextureList& L)
{
    xrCriticalSectionGuard guard(creationGuard);
    std::sort(L.begin(), L.end(), cmp_tl);
    for (u32 it = 0; it < lst_textures.size(); it++)
    {
        STextureList* base = lst_textures[it];
        if (L.equal(*base)) return base;
    }
    STextureList* lst = xr_new<STextureList>(L);
    lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    lst_textures.push_back(lst);
    return lst;
}

void CResourceManager::_DeleteTextureList(const STextureList* L)
{
    if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(lst_textures, L)) return;
    Msg("! ERROR: Failed to find compiled list of textures");
}

//--------------------------------------------------------------------------------------------------------------
SMatrixList* CResourceManager::_CreateMatrixList(SMatrixList& L)
{
    BOOL bEmpty = TRUE;
    for (u32 i = 0; i < L.size(); i++)
        if (L[i])
        {
            bEmpty = FALSE;
            break;
        }
    if (bEmpty) return NULL;

    xrCriticalSectionGuard guard(creationGuard);

    for (u32 it = 0; it < lst_matrices.size(); it++)
    {
        SMatrixList* base = lst_matrices[it];
        if (L.equal(*base)) return base;
    }
    SMatrixList* lst = xr_new<SMatrixList>(L);

    lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    lst_matrices.push_back(lst);
    return lst;
}

void CResourceManager::_DeleteMatrixList(const SMatrixList* L)
{
    if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(lst_matrices, L)) return;
    Msg("! ERROR: Failed to find compiled list of xform-defs");
}

//--------------------------------------------------------------------------------------------------------------
SConstantList* CResourceManager::_CreateConstantList(SConstantList& L)
{
    BOOL bEmpty = TRUE;
    for (u32 i = 0; i < L.size(); i++)
        if (L[i])
        {
            bEmpty = FALSE;
            break;
        }
    if (bEmpty) return NULL;

    xrCriticalSectionGuard guard(creationGuard);

    for (u32 it = 0; it < lst_constants.size(); it++)
    {
        SConstantList* base = lst_constants[it];
        if (L.equal(*base)) return base;
    }
    SConstantList* lst = xr_new<SConstantList>(L);

    lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    lst_constants.push_back(lst);
    return lst;
}

void CResourceManager::_DeleteConstantList(const SConstantList* L)
{
    if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
    xrCriticalSectionGuard guard(creationGuard);
    if (reclaim(lst_constants, L)) return;
    Msg("! ERROR: Failed to find compiled list of r1-constant-defs");
}
