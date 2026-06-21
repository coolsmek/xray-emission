#ifndef	xrD3DDefs_included
#define	xrD3DDefs_included
#pragma once

#if defined(USE_DX11) || defined(USE_DX10)

#	include "..\xrRenderDX10\DXCommonTypes.h"

#elif defined(USE_VK)

#include <vulkan/vulkan.h>

// ── Shader wrappers ───────────────────────────────────────────────────────────
// Store the SPIR-V blob pointer + size so the resource manager can create
// VkShaderModule on demand at pipeline creation time.
struct VkVertexShaderWrapper   { void* pSPIRV; size_t size; };
struct VkPixelShaderWrapper    { void* pSPIRV; size_t size; };
struct VkGeometryShaderWrapper { void* pSPIRV; size_t size; };
struct VkHullShaderWrapper     { void* pSPIRV; size_t size; };
struct VkDomainShaderWrapper   { void* pSPIRV; size_t size; };
struct VkComputeShaderWrapper  { void* pSPIRV; size_t size; };

typedef VkVertexShaderWrapper*   ID3DVertexShader;
typedef VkPixelShaderWrapper*    ID3DPixelShader;
typedef VkGeometryShaderWrapper* ID3DGeometryShader;
typedef VkHullShaderWrapper*     ID3DHullShader;
typedef VkDomainShaderWrapper*   ID3DDomainShader;
typedef VkComputeShaderWrapper*  ID3DComputeShader;

// ── Input layout ──────────────────────────────────────────────────────────────
struct VkInputLayoutWrapper    { void* pDesc; unsigned int count; };
typedef VkInputLayoutWrapper*    ID3DInputLayout;

// ── Buffer wrapper ────────────────────────────────────────────────────────────
struct VkBufferWrapper         { VkBuffer buffer; VkDeviceMemory memory; size_t size; };
typedef VkBufferWrapper*         ID3DBuffer;
typedef VkBufferWrapper*         ID3DVertexBuffer;
typedef VkBufferWrapper*         ID3DIndexBuffer;

// ── View wrappers ─────────────────────────────────────────────────────────────
struct VkRTVWrapper            { VkImageView view; VkImage image; VkFormat format; };
struct VkDSVWrapper            { VkImageView view; VkImage image; };
struct VkSRVWrapper            { VkImageView view; VkSampler sampler; VkFormat format; };
struct VkUAVWrapper            { VkImageView view; };

typedef VkRTVWrapper*            ID3DRenderTargetView;
typedef VkDSVWrapper*            ID3DDepthStencilView;
typedef VkSRVWrapper*            ID3DShaderResourceView;
typedef VkUAVWrapper*            ID3DUnorderedAccessView;

// ── Texture wrappers ──────────────────────────────────────────────────────────
struct VkTexture2DWrapper      { VkImage image; VkDeviceMemory memory; VkFormat format; unsigned int width; unsigned int height; unsigned int mips; };
struct VkTexture3DWrapper      { VkImage image; VkDeviceMemory memory; VkFormat format; unsigned int width; unsigned int height; unsigned int depth; };

typedef VkTexture2DWrapper*      ID3DTexture2D;
typedef VkTexture3DWrapper*      ID3DTexture3D;
typedef VkTexture2DWrapper*      ID3DBaseTexture; // cubemaps also use 2D image arrays

// ── Blob / shader macro / include (DXC output compatible) ────────────────────
// ID3DBlob — reuse a simple byte buffer struct; DXC outputs these
struct VkBlobWrapper           { void* pData; size_t size; };
typedef VkBlobWrapper*           ID3DBlob;

struct D3D_SHADER_MACRO        { const char* Name; const char* Definition; };
struct D3D_VIEWPORT            { float TopLeftX; float TopLeftY; float Width; float Height; float MinDepth; float MaxDepth; };
struct D3D_TEXTURE2D_DESC      { unsigned int Width; unsigned int Height; unsigned int MipLevels; unsigned int ArraySize; VkFormat Format; unsigned int SampleCount; unsigned int BindFlags; unsigned int Usage; };

typedef void* ID3DInclude;

// ── Pipeline state stubs ──────────────────────────────────────────────────────
// VK uses monolithic pipeline objects; these are held in CHW's pipeline cache
typedef void* ID3DRasterizerState;
typedef void* ID3DBlendState;
typedef void* ID3DDepthStencilState;
typedef void* ID3DSamplerState;
struct ID3DState { void Apply() {} }; // VK: pipeline state is implicit — no-op stub

// ── Context / device / query stubs ───────────────────────────────────────────
typedef void* ID3DDeviceContext;
typedef void* ID3DDevice;
typedef void* ID3DQuery;
typedef void* ID3DUserDefinedAnnotation;

// ── Compatibility macros ──────────────────────────────────────────────────────
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


#endif	//	xrD3DDefs_included
