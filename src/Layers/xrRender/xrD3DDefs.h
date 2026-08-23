#ifndef	xrD3DDefs_included
#define	xrD3DDefs_included
#pragma once

#if defined(USE_DX11) || defined(USE_DX10)

#	include "..\xrRenderDX10\DXCommonTypes.h"

#elif defined(USE_VK)

#include <vulkan/vulkan.h>

// â”€â”€ Shader wrappers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Store the SPIR-V blob pointer + size AND the live VkShaderModule handle.
// module is VK_NULL_HANDLE when only raw HLSL bytecode was stored (needs DXC).
struct VkVertexShaderWrapper   { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };
struct VkPixelShaderWrapper    { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };
struct VkGeometryShaderWrapper { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };
struct VkHullShaderWrapper     { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };
struct VkDomainShaderWrapper   { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };
struct VkComputeShaderWrapper  { void* pSPIRV; size_t size; VkShaderModule module = VK_NULL_HANDLE; char entryPoint[64] = "main"; char name[128] = ""; unsigned long Release() { return 0; } };

typedef VkVertexShaderWrapper    ID3DVertexShader;
typedef VkPixelShaderWrapper     ID3DPixelShader;
typedef VkGeometryShaderWrapper  ID3DGeometryShader;
typedef VkHullShaderWrapper      ID3DHullShader;
typedef VkDomainShaderWrapper    ID3DDomainShader;
typedef VkComputeShaderWrapper   ID3DComputeShader;


// â”€â”€ Input layout â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct VkInputLayoutWrapper    { void* pDesc; unsigned int count; unsigned long Release() { return 0; } };
typedef VkInputLayoutWrapper     ID3DInputLayout;

// â”€â”€ Buffer wrapper â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
enum D3D_USAGE {
    D3D_USAGE_DEFAULT = 0,
    D3D_USAGE_IMMUTABLE = 1,
    D3D_USAGE_DYNAMIC = 2,
    D3D_USAGE_STAGING = 3
};

#define D3D_BIND_VERTEX_BUFFER      0x1L
#define D3D_BIND_INDEX_BUFFER       0x2L
#define D3D_BIND_CONSTANT_BUFFER    0x4L
#define D3D_CPU_ACCESS_WRITE        0x10000L

struct D3D_BUFFER_DESC {
    unsigned int ByteWidth;
    D3D_USAGE Usage;
    unsigned int BindFlags;
    unsigned int CPUAccessFlags;
    unsigned int MiscFlags;
    unsigned int StructureByteStride;
};

struct VkBufferWrapper {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* alloc; // VmaAllocation (void* to avoid exposing VMA header everywhere)
    void* mapped_ptr;
    size_t size;
    long refcount = 1;

    int  Lock(unsigned int offset, unsigned int size_val, void** data, unsigned int flags) {
        return 0; // S_OK
    }
    int  Unlock() { return 0; } // S_OK — was void, must return HRESULT-compatible int
    unsigned long AddRef() { return ++refcount; }
    unsigned long Release();
    void GetDesc(D3D_BUFFER_DESC* desc) {
        if (desc) {
            desc->ByteWidth = (unsigned int)size;
            desc->Usage = D3D_USAGE_DYNAMIC;
            desc->BindFlags = 0;
            desc->CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
            desc->MiscFlags = 0;
            desc->StructureByteStride = 0;
        }
    }
};

typedef VkBufferWrapper          ID3DBuffer;
typedef VkBufferWrapper          ID3DVertexBuffer;
typedef VkBufferWrapper          ID3DIndexBuffer;

// â”€â”€ View wrappers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// currentLayout tracks the live Vulkan image layout so u_setrt() can supply
// the correct oldLayout in pipeline barriers (avoiding UNDEFINED discard every pass).
struct VkRTVWrapper            { VkImageView view; VkImage image; VkFormat format; VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED; u32 width = 0; u32 height = 0; };
struct VkDSVWrapper            { VkImageView view; VkImage image; VkFormat format; VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED; u32 width = 0; u32 height = 0; };
struct VkSRVWrapper            { VkImageView view; VkSampler sampler; VkFormat format; };
struct VkUAVWrapper            { VkImageView view; };

typedef VkRTVWrapper             ID3DRenderTargetView;
typedef VkDSVWrapper             ID3DDepthStencilView;
typedef VkSRVWrapper             ID3DShaderResourceView;
typedef VkUAVWrapper             ID3DUnorderedAccessView;

// â”€â”€ Texture wrappers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

struct VkTexture2DWrapper {
    VkImage        image       = VK_NULL_HANDLE;
    VkDeviceMemory memory      = VK_NULL_HANDLE;   // unused when alloc != nullptr (VMA path)
    void*          alloc       = nullptr;           // VmaAllocation — stored as void* to avoid VMA dep here
    VkFormat       format      = VK_FORMAT_UNDEFINED;
    VkImageView    imageView   = VK_NULL_HANDLE;   // SRV view created by vk_CreateTexture2D
    VkSampler      sampler     = VK_NULL_HANDLE;   // sampler  created by vk_CreateTexture2D
    unsigned int   width       = 0;
    unsigned int   height      = 0;
    unsigned int   mips        = 0;
    unsigned long Release() { return 0; }
    unsigned long AddRef()  { return 1; }
    // D3D9-style surface lock stubs (used by gifPlayer.cpp D3D9 fallback path)
    int LockRect(unsigned int /*level*/, D3DLOCKED_RECT* pR, const void*, unsigned int) {
        if (pR) { pR->Pitch = (int)(width * 4); pR->pBits = nullptr; }
        return 0;
    }
    int UnlockRect(unsigned int /*level*/) { return 0; }
    unsigned int GetType() const { return 1; } // D3DRTYPE_TEXTURE
};
struct VkTexture3DWrapper { VkImage image; VkDeviceMemory memory; VkFormat format; unsigned int width; unsigned int height; unsigned int depth; unsigned long Release() { return 0; } };

typedef VkTexture2DWrapper       ID3DTexture2D;
typedef VkTexture3DWrapper       ID3DTexture3D;
typedef VkTexture2DWrapper       ID3DBaseTexture; // cubemaps also use 2D image arrays

// â”€â”€ Blob / shader macro / include (DXC output compatible) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// ID3DBlob â€” reuse a simple byte buffer struct; DXC outputs these
struct VkBlobWrapper           { void* pData; size_t size; };
typedef VkBlobWrapper            ID3DBlob;

struct D3D_SHADER_MACRO        { const char* Name; const char* Definition; };
struct D3D_VIEWPORT            { float TopLeftX; float TopLeftY; float Width; float Height; float MinDepth; float MaxDepth; };
struct D3D_TEXTURE2D_DESC      { unsigned int Width; unsigned int Height; unsigned int MipLevels; unsigned int ArraySize; VkFormat Format; unsigned int SampleCount; unsigned int BindFlags; unsigned int Usage; };
struct D3D_SUBRESOURCE_DATA    { const void* pSysMem; unsigned int SysMemPitch; unsigned int SysMemSlicePitch; };

typedef void* ID3DInclude;

// â”€â”€ Pipeline state stubs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// VK uses monolithic pipeline objects; these are held in CHW's pipeline cache
typedef void* ID3DRasterizerState;
typedef void* ID3DBlendState;
typedef void* ID3DDepthStencilState;
typedef void* ID3DSamplerState;
struct ID3DState {
    void Apply() {}
    unsigned long Release() { return 0; }
    template <typename T>
    static ID3DState* Create(const T&) { return nullptr; }
};

// â”€â”€ Context / device / query stubs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct VkDeviceWrapper {
    int CreateBuffer(const D3D_BUFFER_DESC* desc, const void* subData, VkBufferWrapper** ppBuffer);
    // D3D9-style vertex/index buffer creation stubs used by FVisual/FSkinned (DX9 fallback path).
    // These return S_OK with a heap-allocated wrapper; the VkBuffer inside is null.
    // Real geometry creation in VK path goes through vk_ResourceManager::CreateVertexBuffer.
    int CreateVertexBuffer(UINT Length, DWORD /*Usage*/, DWORD /*FVF*/, DWORD /*Pool*/,
                           VkBufferWrapper** ppVB, HANDLE* /*pSharedHandle*/) {
        if (ppVB) { *ppVB = new VkBufferWrapper{VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, nullptr, Length}; }
        return 0; // S_OK
    }
    int CreateIndexBuffer(UINT Length, DWORD /*Usage*/, DWORD /*Format*/, DWORD /*Pool*/,
                          VkBufferWrapper** ppIB, HANDLE* /*pSharedHandle*/) {
        if (ppIB) { *ppIB = new VkBufferWrapper{VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, nullptr, Length}; }
        return 0; // S_OK
    }
    // D3D9 CreateTexture stub (gifPlayer.cpp D3D9 fallback path)
    int CreateTexture(UINT W, UINT H, UINT Mips, DWORD, DWORD, DWORD,
                      VkTexture2DWrapper** ppTex, HANDLE*) {
        if (ppTex) {
            *ppTex = new VkTexture2DWrapper{};
            (*ppTex)->format = VK_FORMAT_R8G8B8A8_UNORM;
            (*ppTex)->width  = W;
            (*ppTex)->height = H;
            (*ppTex)->mips   = Mips;
        }
        return 0;
    }
};

typedef void* ID3DDeviceContext;
typedef VkDeviceWrapper* ID3DDevice;
struct VkQueryWrapper {
    unsigned long Release() { return 0; }
};
typedef VkQueryWrapper* ID3DQuery;

typedef void* IDXGISwapChain;
typedef void* ID3DBaseShader;
typedef void* ID3DUserDefinedAnnotation;
typedef void* ID3DShaderReflectionConstantBuffer;
typedef void* ID3DShaderReflection;
typedef int D3D_CBUFFER_TYPE;
typedef int D3D_PRIMITIVE_TOPOLOGY;
struct D3D_SHADER_TYPE_DESC { int dummy; };
struct D3D_INPUT_ELEMENT_DESC { int dummy; };

#define D3D_COMPARISON_NEVER 1
#define D3D_COMPARISON_LESS 2
#define D3D_COMPARISON_EQUAL 3
#define D3D_COMPARISON_LESS_EQUAL 4
#define D3D_COMPARISON_GREATER 5
#define D3D_COMPARISON_NOT_EQUAL 6
#define D3D_COMPARISON_GREATER_EQUAL 7
#define D3D_COMPARISON_ALWAYS 8

// â”€â”€ Map D3D11 names used in R_Backend.h to our Vulkan wrappers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
typedef ID3DHullShader           ID3D11HullShader;
typedef ID3DDomainShader         ID3D11DomainShader;
typedef ID3DComputeShader        ID3D11ComputeShader;
typedef ID3DUnorderedAccessView  ID3D11UnorderedAccessView;

// â”€â”€ Compatibility macros â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#define DX10_ONLY(expr)  ((void)0)
#define DX11_ONLY(expr)  ((void)0)

#else	//	DX9 fallback

typedef IDirect3DVertexShader9 ID3DVertexShader;
typedef IDirect3DPixelShader9 ID3DPixelShader;
typedef ID3DXBuffer ID3DBlob;
typedef D3DXMACRO D3D_SHADER_MACRO;
typedef IDirect3DQuery9 ID3DQuery;
typedef D3DVIEWPORT9 D3D_VIEWPORT;
typedef ID3DXInclude ID3DInclude;
typedef IDirect3DTexture9 ID3DTexture2D;
typedef IDirect3DSurface9 ID3DRenderTargetView;
typedef IDirect3DSurface9 ID3DDepthStencilView;
typedef IDirect3DBaseTexture9 ID3DBaseTexture;
typedef D3DSURFACE_DESC D3D_TEXTURE2D_DESC;
typedef IDirect3DVertexBuffer9 ID3DVertexBuffer;
typedef IDirect3DIndexBuffer9 ID3DIndexBuffer;
typedef IDirect3DVolumeTexture9 ID3DTexture3D;
typedef IDirect3DStateBlock9 ID3DState;

#endif	//	USE_DX10 / USE_VK / DX9

#endif  // xrD3DDefs_included

