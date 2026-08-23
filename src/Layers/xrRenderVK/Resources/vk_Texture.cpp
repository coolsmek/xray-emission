// vk_Texture.cpp — DDS game texture upload for xrRenderVK.
// Parses DDS headers, uploads all mip levels via staging buffer,
// creates VkImageView and VkSampler.  See vk_Texture.h for API.
#include "stdafx.h"
#include "vk_Texture.h"
#include "vk_BufferUtils.h"
#include "../RenderTarget/vk_LayoutTransitions.h"
#include "../Managers/vk_MemoryManager.h"
#include "../r2_types.h"

// ─────────────────────────────────────────────────────────────────────────────
// DDS structure definitions
// ─────────────────────────────────────────────────────────────────────────────
#define DDS_MAGIC           0x20534444u  // 'DDS '
#define DDPF_FOURCC         0x00000004u
#define DDPF_RGB            0x00000040u
#define DDPF_ALPHA          0x00000002u
#define DDPF_ALPHAPIXELS    0x00000001u
#define DDSD_MIPMAPCOUNT    0x00020000u
#define DDSCAPS2_CUBEMAP    0x00000200u

#define FOURCC(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#define FOURCC_DXT1 FOURCC('D','X','T','1')
#define FOURCC_DXT3 FOURCC('D','X','T','3')
#define FOURCC_DXT5 FOURCC('D','X','T','5')
#define FOURCC_ATI1 FOURCC('A','T','I','1')  // BC4 single-channel (alt spelling)
#define FOURCC_ATI2 FOURCC('A','T','I','2')  // BC5 two-channel
#define FOURCC_BC4U FOURCC('B','C','4','U')
#define FOURCC_DX10 FOURCC('D','X','1','0')  // Extended DX10 header — requires DDS_HEADER_DXT10

// ─────────────────────────────────────────────────────────────────────────────
// DXGI_FORMAT subset — values used by Anomaly / modern DDS exporters
// ─────────────────────────────────────────────────────────────────────────────
enum DXGI_FORMAT_SUBSET : uint32_t
{
    DXGI_FORMAT_R16G16B16A16_FLOAT   = 10,
    DXGI_FORMAT_R32G32B32A32_FLOAT   = 2,
    DXGI_FORMAT_R8G8B8A8_UNORM       = 28,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB  = 29,
    DXGI_FORMAT_R8G8_UNORM           = 49,
    DXGI_FORMAT_R16_FLOAT            = 54,
    DXGI_FORMAT_R8_UNORM             = 61,
    DXGI_FORMAT_BC1_UNORM            = 71,
    DXGI_FORMAT_BC1_UNORM_SRGB       = 72,
    DXGI_FORMAT_BC2_UNORM            = 74,
    DXGI_FORMAT_BC2_UNORM_SRGB       = 75,
    DXGI_FORMAT_BC3_UNORM            = 77,
    DXGI_FORMAT_BC3_UNORM_SRGB       = 78,
    DXGI_FORMAT_BC4_UNORM            = 80,
    DXGI_FORMAT_BC4_SNORM            = 81,
    DXGI_FORMAT_BC5_UNORM            = 83,
    DXGI_FORMAT_BC5_SNORM            = 84,
    DXGI_FORMAT_B8G8R8A8_UNORM       = 87,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB  = 91,
    DXGI_FORMAT_BC6H_UF16            = 95,
    DXGI_FORMAT_BC6H_SF16            = 96,
    DXGI_FORMAT_BC7_UNORM            = 98,
    DXGI_FORMAT_BC7_UNORM_SRGB       = 99,
    DXGI_FORMAT_R32_FLOAT            = 41,
};

#pragma pack(push, 1)
struct DDS_HEADER_DXT10 {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

static VkFormat DxgiFormatToVk(uint32_t dxgi)
{
    switch (dxgi)
    {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:      return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:      return VK_FORMAT_BC2_UNORM_BLOCK;
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:      return VK_FORMAT_BC3_UNORM_BLOCK;
    case DXGI_FORMAT_BC4_UNORM:           return VK_FORMAT_BC4_UNORM_BLOCK;
    case DXGI_FORMAT_BC4_SNORM:           return VK_FORMAT_BC4_SNORM_BLOCK;
    case DXGI_FORMAT_BC5_UNORM:           return VK_FORMAT_BC5_UNORM_BLOCK;
    case DXGI_FORMAT_BC5_SNORM:           return VK_FORMAT_BC5_SNORM_BLOCK;
    case DXGI_FORMAT_BC6H_UF16:           return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case DXGI_FORMAT_BC6H_SF16:           return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:      return VK_FORMAT_BC7_UNORM_BLOCK;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8_UNORM:            return VK_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R8G8_UNORM:          return VK_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
    case DXGI_FORMAT_R16_FLOAT:           return VK_FORMAT_R16_SFLOAT;
    case DXGI_FORMAT_R32_FLOAT:           return VK_FORMAT_R32_SFLOAT;
    default:
        Msg("! DxgiFormatToVk: unhandled DXGI format %u", dxgi);
        return VK_FORMAT_UNDEFINED;
    }
}

#pragma pack(push, 1)
struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t        dwSize;           // must be 124
    uint32_t        dwFlags;
    uint32_t        dwHeight;
    uint32_t        dwWidth;
    uint32_t        dwPitchOrLinearSize;
    uint32_t        dwDepth;
    uint32_t        dwMipMapCount;
    uint32_t        dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        dwCaps;
    uint32_t        dwCaps2;
    uint32_t        dwCaps3;
    uint32_t        dwCaps4;
    uint32_t        dwReserved2;
};
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// Format mapping
// ─────────────────────────────────────────────────────────────────────────────
static VkFormat GetVkFormatFromDDS(const DDS_PIXELFORMAT& pf,
                                    const DDS_HEADER_DXT10* dx10Hdr)
{
    if (pf.dwFlags & DDPF_FOURCC)
    {
        switch (pf.dwFourCC)
        {
        case FOURCC_DXT1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case FOURCC_DXT3: return VK_FORMAT_BC2_UNORM_BLOCK;
        case FOURCC_DXT5: return VK_FORMAT_BC3_UNORM_BLOCK;
        case FOURCC_ATI1: return VK_FORMAT_BC4_UNORM_BLOCK;
        case FOURCC_ATI2: return VK_FORMAT_BC5_UNORM_BLOCK;
        case FOURCC_BC4U: return VK_FORMAT_BC4_UNORM_BLOCK;
        case FOURCC_DX10:
            if (dx10Hdr)
                return DxgiFormatToVk(dx10Hdr->dxgiFormat);
            return VK_FORMAT_UNDEFINED;
        default:
            Msg("! GetVkFormatFromDDS: unhandled FourCC %08X", pf.dwFourCC);
            return VK_FORMAT_UNDEFINED;
        }
    }
    if (pf.dwFlags & DDPF_RGB)
    {
        if (pf.dwRGBBitCount == 32)
        {
            // A8R8G8B8 -> BGRA; A8B8G8R8 -> RGBA
            if (pf.dwRBitMask == 0x00FF0000u)
                return VK_FORMAT_B8G8R8A8_UNORM;
            if (pf.dwRBitMask == 0x000000FFu)
                return VK_FORMAT_R8G8B8A8_UNORM;
        }
        else if (pf.dwRGBBitCount == 24)
        {
            // We will expand 24-bit to 32-bit during upload.
            if (pf.dwRBitMask == 0x00FF0000u)
                return VK_FORMAT_B8G8R8A8_UNORM;
            if (pf.dwRBitMask == 0x000000FFu)
                return VK_FORMAT_R8G8B8A8_UNORM;
            return VK_FORMAT_B8G8R8A8_UNORM;
        }
    }
    if ((pf.dwFlags & DDPF_ALPHA) && pf.dwRGBBitCount == 8)
    {
        return VK_FORMAT_R8G8B8A8_UNORM; // We will manually byte-expand to this
    }
    return VK_FORMAT_UNDEFINED;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mip size helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool IsBlockCompressed(VkFormat fmt)
{
    return fmt == VK_FORMAT_BC1_RGBA_UNORM_BLOCK
        || fmt == VK_FORMAT_BC2_UNORM_BLOCK
        || fmt == VK_FORMAT_BC3_UNORM_BLOCK
        || fmt == VK_FORMAT_BC4_UNORM_BLOCK
        || fmt == VK_FORMAT_BC4_SNORM_BLOCK
        || fmt == VK_FORMAT_BC5_UNORM_BLOCK
        || fmt == VK_FORMAT_BC5_SNORM_BLOCK
        || fmt == VK_FORMAT_BC6H_UFLOAT_BLOCK
        || fmt == VK_FORMAT_BC6H_SFLOAT_BLOCK
        || fmt == VK_FORMAT_BC7_UNORM_BLOCK;
}

static uint32_t BlockSize(VkFormat fmt)
{
    // BC1 and BC4 = 8 bytes per 4×4 block; all others = 16 bytes
    if (fmt == VK_FORMAT_BC1_RGBA_UNORM_BLOCK
     || fmt == VK_FORMAT_BC4_UNORM_BLOCK
     || fmt == VK_FORMAT_BC4_SNORM_BLOCK)
        return 8;
    return 16;
}

static VkDeviceSize MipDataSize(VkFormat fmt, uint32_t w, uint32_t h)
{
    if (IsBlockCompressed(fmt))
    {
        uint32_t bw = (w + 3) / 4;
        uint32_t bh = (h + 3) / 4;
        return (VkDeviceSize)bw * bh * BlockSize(fmt);
    }
    // Uncompressed — derive bytes-per-pixel from format
    uint32_t bpp = 4; // default: 4 bytes (RGBA8, BGRA8, etc.)
    switch (fmt)
    {
    case VK_FORMAT_R8_UNORM:              bpp = 1; break;
    case VK_FORMAT_R8G8_UNORM:           bpp = 2; break;
    case VK_FORMAT_R16_SFLOAT:           bpp = 2; break;
    case VK_FORMAT_R16G16_SFLOAT:        bpp = 4; break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:  bpp = 8; break;
    case VK_FORMAT_R32_SFLOAT:           bpp = 4; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:  bpp = 16; break;
    default: bpp = 4; break;
    }
    return (VkDeviceSize)w * h * bpp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Removed BeginOneShot, EndOneShot, and s_transferPool since transfers are now queued to the main thread.

void vk_Texture_Cleanup()
{
}

// ─────────────────────────────────────────────────────────────────────────────
// vk_CreateTexture2D
// ─────────────────────────────────────────────────────────────────────────────
VkResult vk_CreateTexture2D(
    const void*         ddsData,
    size_t              ddsSize,
    VkTexture2DWrapper& out,
    uint32_t            mip_skip)
{
    // Zero-initialise output
    out = VkTexture2DWrapper{};

    if (!ddsData || ddsSize < sizeof(uint32_t) + sizeof(DDS_HEADER) + 4)
    {
        Msg("! vk_CreateTexture2D: buffer too small");
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    const uint8_t* p = static_cast<const uint8_t*>(ddsData);

    // Validate magic
    uint32_t magic = *reinterpret_cast<const uint32_t*>(p);
    if (magic != DDS_MAGIC)
    {
        Msg("! vk_CreateTexture2D: not a DDS file (magic=%08X)", magic);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    p += sizeof(uint32_t);

    const DDS_HEADER* hdr = reinterpret_cast<const DDS_HEADER*>(p);
    if (hdr->dwSize != 124)
    {
        Msg("! vk_CreateTexture2D: unexpected DDS header size %u", hdr->dwSize);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    p += sizeof(DDS_HEADER);

    // If FourCC == 'DX10', the DX10 extended header immediately follows the main header.
    // We MUST advance p past it so the pointer lands on actual pixel data.
    const DDS_HEADER_DXT10* dx10Hdr = nullptr;
    if ((hdr->ddspf.dwFlags & DDPF_FOURCC) && hdr->ddspf.dwFourCC == FOURCC_DX10)
    {
        dx10Hdr = reinterpret_cast<const DDS_HEADER_DXT10*>(p);
        p += sizeof(DDS_HEADER_DXT10);
    }

    VkFormat fmt = GetVkFormatFromDDS(hdr->ddspf, dx10Hdr);
    if (fmt == VK_FORMAT_UNDEFINED)
    {
        Msg("! vk_CreateTexture2D: unsupported DDS format (FourCC=%08X, flags=%08X)",
            hdr->ddspf.dwFourCC, hdr->ddspf.dwFlags);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    uint32_t w    = hdr->dwWidth;
    uint32_t h    = hdr->dwHeight;
    uint32_t mips = (hdr->dwFlags & DDSD_MIPMAPCOUNT) && hdr->dwMipMapCount > 0
                          ? hdr->dwMipMapCount : 1;
    const bool isCubemap = (hdr->dwCaps2 & DDSCAPS2_CUBEMAP) != 0;
    const uint32_t arrayLayers = isCubemap ? 6 : 1;

    uint32_t originalMips = mips;
    uint32_t originalW = w;
    uint32_t originalH = h;
    
    // Apply mip_skip
    if (mips > 1 && mip_skip > 0)
    {
        uint32_t actual_skip = mip_skip;
        if (actual_skip >= mips) actual_skip = mips - 1;

        int reduce_count = actual_skip;
        int l = mips;
        while ((l > 1) && reduce_count)
        {
            w /= 2;
            h /= 2;
            l -= 1;
            reduce_count--;
        }
        if (w < 1) w = 1;
        if (h < 1) h = 1;

        mip_skip = actual_skip - reduce_count; 
        mips -= mip_skip;
    }
    else
    {
        mip_skip = 0;
    }

    // ── Compute total data size and collect per-mip offsets/sizes ────────────
    struct MipSlice { VkDeviceSize size; uint32_t w; uint32_t h; };
    xr_vector<MipSlice> slices;
    slices.reserve(mips);
    VkDeviceSize faceBytes = 0;
    VkDeviceSize skipBytesPerFace = 0;
    VkDeviceSize originalFaceBytes = 0;
    {
        uint32_t mw = originalW, mh = originalH;
        for (uint32_t m = 0; m < originalMips; ++m)
        {
            VkDeviceSize s = MipDataSize(fmt, mw, mh);
            originalFaceBytes += s;
            
            if (m < mip_skip)
            {
                skipBytesPerFace += s;
            }
            else
            {
                slices.push_back({ s, mw, mh });
                faceBytes += s;
            }
            
            if (mw > 1) mw >>= 1;
            if (mh > 1) mh >>= 1;
        }
    }
    VkDeviceSize totalBytes = faceBytes * arrayLayers;

    // ── Create device-local VkImage ──────────────────────────────────────────
    VkImageCreateInfo imgCI{};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.flags         = isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    imgCI.format        = fmt;
    imgCI.extent        = { w, h, 1 };
    imgCI.mipLevels     = mips;
    imgCI.arrayLayers   = arrayLayers;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage       img   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult res = MemoryManager.CreateImage(&imgCI, &allocCI, &img, &alloc);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_CreateTexture2D: vmaCreateImage failed (%d)", (int)res);
        return res;
    }

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    void* stagingPtr = nullptr;
    res = vk_CreateStagingBuffer(totalBytes, stagingBuf, stagingAlloc, stagingPtr);
    if (res != VK_SUCCESS)
    {
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateTexture2D: staging buffer alloc failed (%d)", (int)res);
        Msg("! --> totalBytes=%llu, originalMips=%u, mips=%u, mip_skip=%u, fmt=%d", 
            (unsigned long long)totalBytes, originalMips, mips, mip_skip, (int)fmt);
        Msg("! --> w=%u, h=%u, arrayLayers=%u, originalFaceBytes=%llu",
            w, h, arrayLayers, (unsigned long long)originalFaceBytes);
        return res;
    }

    // Support legacy 8-bit DDPF_ALPHA expanding to 32-bit RGBA
    bool is8BitAlpha = (hdr->ddspf.dwFlags & DDPF_ALPHA) && hdr->ddspf.dwRGBBitCount == 8;
    bool is24BitRGB  = (hdr->ddspf.dwFlags & DDPF_RGB)   && hdr->ddspf.dwRGBBitCount == 24;

    uint32_t srcBppMult = 4;
    if (is8BitAlpha) srcBppMult = 1;
    else if (is24BitRGB) srcBppMult = 3;

    uint8_t* dest = static_cast<uint8_t*>(stagingPtr);
    
    for (uint32_t layer = 0; layer < arrayLayers; ++layer)
    {
        // For each layer, `p` advances by `originalFaceBytes` total, but we only copy starting after `skipBytesPerFace`
        // We must adjust the source offset because originalFaceBytes/skipBytesPerFace are sized for the destination VK_FORMAT (4bpp)
        VkDeviceSize srcOriginalFaceBytes = originalFaceBytes * srcBppMult / 4;
        VkDeviceSize srcSkipBytesPerFace = skipBytesPerFace * srcBppMult / 4;
        
        const uint8_t* src_layer = p + (layer * srcOriginalFaceBytes) + srcSkipBytesPerFace;
        uint8_t* dest_layer = dest + (layer * faceBytes);
        
        if (is8BitAlpha)
        {
            const size_t pixelCount = (size_t)(faceBytes / 4);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                dest_layer[i * 4 + 0] = 255;
                dest_layer[i * 4 + 1] = 255;
                dest_layer[i * 4 + 2] = 255;
                dest_layer[i * 4 + 3] = src_layer[i];
            }
        }
        else if (is24BitRGB)
        {
            const size_t pixelCount = (size_t)(faceBytes / 4);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                dest_layer[i * 4 + 0] = src_layer[i * 3 + 0];
                dest_layer[i * 4 + 1] = src_layer[i * 3 + 1];
                dest_layer[i * 4 + 2] = src_layer[i * 3 + 2];
                dest_layer[i * 4 + 3] = 255;
            }
        }
        else
        {
            memcpy(dest_layer, src_layer, faceBytes);
        }
    }

    // ── Queue the transfer for the main thread ───────────────────────────────
    // Copy each mip slice for all faces
    xr_vector<VkBufferImageCopy> copies;
    copies.reserve(mips * arrayLayers);
    for (uint32_t layer = 0; layer < arrayLayers; ++layer)
    {
        VkDeviceSize currentOffset = layer * faceBytes;
        for (uint32_t m = 0; m < mips; ++m)
        {
            const MipSlice& sl = slices[m];
            VkBufferImageCopy r{};
            r.bufferOffset                    = currentOffset;
            r.bufferRowLength                 = 0;   // tightly packed
            r.bufferImageHeight               = 0;
            r.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.mipLevel       = m;
            r.imageSubresource.baseArrayLayer = layer;
            r.imageSubresource.layerCount     = 1;
            r.imageOffset                     = { 0, 0, 0 };
            r.imageExtent                     = { sl.w, sl.h, 1 };
            copies.push_back(r);

            currentOffset += sl.size;
        }
    }

    HW.QueueTransfer(
        [=, copies = std::move(copies)](VkCommandBuffer cmd) {
            // UNDEFINED → TRANSFER_DST (all mips)
            vk_TransitionImageLayout(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                0, mips);

            vkCmdCopyBufferToImage(cmd, stagingBuf, img,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                (uint32_t)copies.size(), copies.data());

            // TRANSFER_DST → SHADER_READ_ONLY (all mips)
            vk_TransitionImageLayout(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                0, mips);
        },
        [=]() {
            vk_DestroyBuffer(stagingBuf, stagingAlloc);
        }
    );

    // ── VkImageView ──────────────────────────────────────────────────────────
    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = img;
    viewCI.viewType                        = isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = fmt;
    viewCI.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY };
    viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel   = 0;
    viewCI.subresourceRange.levelCount     = mips;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount     = arrayLayers;

    VkImageView view = VK_NULL_HANDLE;
    res = vkCreateImageView(HW.m_vkDevice, &viewCI, nullptr, &view);
    if (res != VK_SUCCESS)
    {
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateTexture2D: vkCreateImageView failed (%d)", (int)res);
        return res;
    }

    // ── VkSampler ─────────────────────────────────────────────────────────────
    VkPhysicalDeviceProperties devProps{};
    vkGetPhysicalDeviceProperties(HW.m_vkPhysDevice, &devProps);

    VkSamplerCreateInfo sampCI{};
    sampCI.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCI.magFilter               = VK_FILTER_LINEAR;
    sampCI.minFilter               = VK_FILTER_LINEAR;
    sampCI.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampCI.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.mipLodBias              = 0.0f;
    sampCI.anisotropyEnable        = VK_TRUE;
    sampCI.maxAnisotropy           = devProps.limits.maxSamplerAnisotropy;
    sampCI.compareEnable           = VK_FALSE;
    sampCI.compareOp               = VK_COMPARE_OP_ALWAYS;
    sampCI.minLod                  = 0.0f;
    sampCI.maxLod                  = (float)mips;
    sampCI.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampCI.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    res = vkCreateSampler(HW.m_vkDevice, &sampCI, nullptr, &sampler);
    if (res != VK_SUCCESS)
    {
        vkDestroyImageView(HW.m_vkDevice, view, nullptr);
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateTexture2D: vkCreateSampler failed (%d)", (int)res);
        return res;
    }

    // ── Fill output wrapper ───────────────────────────────────────────────────
    out.image     = img;
    out.alloc     = alloc;
    out.format    = fmt;
    out.imageView = view;
    out.sampler   = sampler;
    out.width     = w;
    out.height    = h;
    out.mips      = mips;

    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// vk_CreateDummyTexture
// ─────────────────────────────────────────────────────────────────────────────
VkResult vk_CreateDummyTexture(VkTexture2DWrapper& out)
{
    out = VkTexture2DWrapper{};
    out.width  = 1;
    out.height = 1;
    out.mips   = 1;
    out.format = VK_FORMAT_B8G8R8A8_UNORM;

    VkImageCreateInfo imgCI{};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = (VkFormat)out.format;
    imgCI.extent        = { 1, 1, 1 };
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage       img   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult res = MemoryManager.CreateImage(&imgCI, &allocCI, &img, &alloc);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_CreateDummyTexture: vmaCreateImage failed (%d)", (int)res);
        return res;
    }

    // Allocate 1x1 magenta pixel staging buffer (B8G8R8A8 format = Blue, Green, Red, Alpha)
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    void* stagingPtr = nullptr;
    res = vk_CreateStagingBuffer(4, stagingBuf, stagingAlloc, stagingPtr);
    if (res == VK_SUCCESS)
    {
        uint8_t* pixels = (uint8_t*)stagingPtr;
        pixels[0] = 0xFF; // B
        pixels[1] = 0x00; // G
        pixels[2] = 0xFF; // R
        pixels[3] = 0xFF; // A

        HW.QueueTransfer(
            [=](VkCommandBuffer cmd) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = img;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {1, 1, 1};

                vkCmdCopyBufferToImage(cmd, stagingBuf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            },
            [=]() {
                vk_DestroyBuffer(stagingBuf, stagingAlloc);
            }
        );
    }

    // Create view
    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = img;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = (VkFormat)out.format;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    res = vkCreateImageView(HW.m_vkDevice, &viewCI, nullptr, &view);
    if (res != VK_SUCCESS)
    {
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateDummyTexture: vkCreateImageView failed (%d)", (int)res);
        return res;
    }

    // Create sampler
    VkSamplerCreateInfo sampCI{};
    sampCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCI.magFilter = VK_FILTER_NEAREST;
    sampCI.minFilter = VK_FILTER_NEAREST;
    sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    VkSampler sampler = VK_NULL_HANDLE;
    res = vkCreateSampler(HW.m_vkDevice, &sampCI, nullptr, &sampler);
    if (res != VK_SUCCESS)
    {
        vkDestroyImageView(HW.m_vkDevice, view, nullptr);
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateDummyTexture: vkCreateSampler failed (%d)", (int)res);
        return res;
    }

    out.image     = img;
    out.alloc     = alloc;
    out.imageView = view;
    out.sampler   = sampler;

    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// vk_CreateMaterialLUT
// ─────────────────────────────────────────────────────────────────────────────
VkResult vk_CreateMaterialLUT(VkTexture2DWrapper& out)
{
    out = VkTexture2DWrapper{};
    out.width  = TEX_material_LdotN;
    out.height = TEX_material_LdotH;
    out.mips   = 1;
    out.format = VK_FORMAT_R8G8_UNORM;

    VkImageCreateInfo imgCI{};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_3D;
    imgCI.format        = (VkFormat)out.format;
    imgCI.extent        = { TEX_material_LdotN, TEX_material_LdotH, TEX_material_Count };
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage       img   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult res = MemoryManager.CreateImage(&imgCI, &allocCI, &img, &alloc);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_CreateMaterialLUT: vmaCreateImage failed (%d)", (int)res);
        return res;
    }

    size_t dataSize = TEX_material_LdotN * TEX_material_LdotH * TEX_material_Count * 2; // 2 bytes per texel
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    void* stagingPtr = nullptr;
    res = vk_CreateStagingBuffer(dataSize, stagingBuf, stagingAlloc, stagingPtr);
    if (res == VK_SUCCESS)
    {
        u16* tempData = (u16*)stagingPtr;
        for (u32 slice = 0; slice < TEX_material_Count; slice++)
        {
            for (u32 y = 0; y < TEX_material_LdotH; y++)
            {
                for (u32 x = 0; x < TEX_material_LdotN; x++)
                {
                    u16* p = tempData + (slice * (TEX_material_LdotH * TEX_material_LdotN)) + (y * TEX_material_LdotN) + x;
                    float ld = float(x) / float(TEX_material_LdotN - 1);
                    float ls = float(y) / float(TEX_material_LdotH - 1) + EPS_S;
                    ls *= powf(ld, 1 / 32.f);
                    float fd, fs;

                    switch (slice)
                    {
                    case 0:
                        fd = powf(ld, 0.75f); 
                        fs = powf(ls, 16.f) * .5f;
                        break;
                    case 1:
                        fd = powf(ld, 0.90f); 
                        fs = powf(ls, 24.f);
                        break;
                    case 2:
                        fd = ld; 
                        fs = powf(ls * 1.01f, 128.f);
                        break;
                    case 3:
                        {
                            float s0 = _abs(1 - _abs(0.05f * _sin(33.f * ld) + ld - ls));
                            float s1 = _abs(1 - _abs(0.05f * _cos(33.f * ld * ls) + ld - ls));
                            float s2 = _abs(1 - _abs(ld - ls));
                            fd = ld; 
                            fs = powf(_max(_max(s0, s1), s2), 24.f);
                            fs *= powf(ld, 1 / 7.f);
                        }
                        break;
                    default:
                        fd = fs = 0;
                    }
                    s32 _d = clampr(iFloor(fd * 255.5f), 0, 255);
                    s32 _s = clampr(iFloor(fs * 255.5f), 0, 255);
                    if ((y == (TEX_material_LdotH - 1)) && (x == (TEX_material_LdotN - 1)))
                    {
                        _d = 255;
                        _s = 255;
                    }
                    *p = u16(_s * 256 + _d);
                }
            }
        }

        HW.QueueTransfer(
            [=](VkCommandBuffer cmd) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = img;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = { TEX_material_LdotN, TEX_material_LdotH, TEX_material_Count };

                vkCmdCopyBufferToImage(cmd, stagingBuf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            },
            [=]() {
                vk_DestroyBuffer(stagingBuf, stagingAlloc);
            }
        );
    }

    // Create view
    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = img;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewCI.format = (VkFormat)out.format;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    res = vkCreateImageView(HW.m_vkDevice, &viewCI, nullptr, &view);
    if (res != VK_SUCCESS)
    {
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateMaterialLUT: vkCreateImageView failed (%d)", (int)res);
        return res;
    }

    // Create sampler
    VkSamplerCreateInfo sampCI{};
    sampCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCI.magFilter = VK_FILTER_LINEAR;
    sampCI.minFilter = VK_FILTER_LINEAR;
    sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    VkSampler sampler = VK_NULL_HANDLE;
    res = vkCreateSampler(HW.m_vkDevice, &sampCI, nullptr, &sampler);
    if (res != VK_SUCCESS)
    {
        vkDestroyImageView(HW.m_vkDevice, view, nullptr);
        MemoryManager.DestroyImage(img, alloc);
        Msg("! vk_CreateMaterialLUT: vkCreateSampler failed (%d)", (int)res);
        return res;
    }

    out.image     = img;
    out.alloc     = alloc;
    out.imageView = view;
    out.sampler   = sampler;

    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// vk_DestroyTexture2D
// ─────────────────────────────────────────────────────────────────────────────
void vk_DestroyTexture2D(VkTexture2DWrapper& wrapper)
{
    VkDevice dev = HW.m_vkDevice;
    if (dev == VK_NULL_HANDLE) return;

    HW.QueueTransfer(
        [](VkCommandBuffer) {},
        [=]() {
            if (wrapper.sampler   != VK_NULL_HANDLE)
                vkDestroySampler(dev, wrapper.sampler, nullptr);
            if (wrapper.imageView != VK_NULL_HANDLE)
                vkDestroyImageView(dev, wrapper.imageView, nullptr);
            if (wrapper.image != VK_NULL_HANDLE && wrapper.alloc != nullptr)
                MemoryManager.DestroyImage(wrapper.image, static_cast<VmaAllocation>(wrapper.alloc));
        }
    );

    wrapper = VkTexture2DWrapper{};
}

