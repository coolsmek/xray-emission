// Implements the core CRender class inheriting from the abstract engine interface IRender_interface
#pragma once
#include "stdafx.h"
#include "../../xrEngine/Render.h"

// Forward declarations
class dxRender_Visual;

// Vulkan CRender — implements the engine's abstract IRender_interface.
// This is the primary frame driver for xrRenderVK.
class CRender : public IRender_interface, public pureFrame
{
public:
    CRender();
    virtual ~CRender();

    // ── IRender_interface — core frame pump ───────────────────────────────
    virtual void        Render();

    // ── Remaining pure-virtual stubs (fill in as systems are implemented) ─
    virtual void        set_HUD(BOOL V)                                 {}
    virtual BOOL        get_HUD()                                       { return FALSE; }
    virtual void        set_Invisible(BOOL V)                           {}
    virtual void        flush()                                         {}
    virtual void        set_Object(IRenderable* O)                      {}
    virtual void        add_Occluder(Fbox2& bb_screenspace)             {}
    virtual void        add_Visual(IRender_Visual* V)                   {}
    virtual void        add_Geometry(IRender_Visual* V)                 {}

    virtual IRender_Visual* model_Create(LPCSTR name, IReader* data = nullptr) { return nullptr; }
    virtual IRender_Visual* model_CreateChild(LPCSTR name, IReader* data)      { return nullptr; }
    virtual IRender_Visual* model_Duplicate(IRender_Visual* V)                 { return nullptr; }
    virtual void            model_Delete(IRender_Visual*& V, BOOL bDiscard)    {}
    virtual IRender_Visual* model_CreatePE(PS::CPEDef* def)                    { return nullptr; }
    virtual IRender_Visual* model_CreateParticles(PS::CPEDef* def)             { return nullptr; }

    virtual void        Screenshot(ScreenshotMode mode = SM_NORMAL, LPCSTR name = nullptr) {}

    virtual void        OnFrame()                                       {}

    virtual size_t      SectorsCount()                                  { return 0; }

protected:
    virtual void        ScreenshotImpl(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer) {}
};

extern CRender RImplementation;
