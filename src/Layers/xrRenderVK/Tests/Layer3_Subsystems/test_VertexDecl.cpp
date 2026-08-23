#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "../vk_TestRunner.h"
#include "../../Resources/vk_BufferUtils.h"

// Standard Anomaly level geometry decl: POSITION(F3) NORMAL(F3) COLOR(D3DCOLOR) UV0(F2)
static const D3DVERTEXELEMENT9 kLevelDecl[] = {
    {0, 0,  D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
    {0, 24, D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
    {0, 28, D3DDECLTYPE_FLOAT2,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()
};

VK_TEST(VertexDecl, LevelDeclStride)
{
    // POSITION(12) + NORMAL(12) + D3DCOLOR(4) + UV(8) = 36 bytes
    uint32_t stride = vk_GetDeclVertexSize(kLevelDecl, 0);
    Msg("* [VK_TEST VertexDecl] computed stride = %u (expected 36)", stride);
    VK_EXPECT(stride == 36);
    return true;
}

VK_TEST(VertexDecl, LevelDeclLength)
{
    uint32_t len = vk_GetDeclLength(kLevelDecl);
    VK_EXPECT(len == 4);  // 4 elements before sentinel
    return true;
}

// Skinned model decl: POSITION(F3) WEIGHTS(F4) INDICES(UBYTE4) NORMAL(F3) UV0(F2)
static const D3DVERTEXELEMENT9 kSkinnedDecl[] = {
    {0, 0,  D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
    {0, 12, D3DDECLTYPE_FLOAT4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
    {0, 28, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
    {0, 32, D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
    {0, 44, D3DDECLTYPE_FLOAT2,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
    D3DDECL_END()
};

VK_TEST(VertexDecl, SkinnedDeclStride)
{
    // POSITION(12) + WEIGHTS(16) + INDICES(4) + NORMAL(12) + UV(8) = 52
    uint32_t stride = vk_GetDeclVertexSize(kSkinnedDecl, 0);
    Msg("* [VK_TEST VertexDecl] skinned stride = %u (expected 52)", stride);
    VK_EXPECT(stride == 52);
    return true;
}

// Multi-stream test: stream 0 = position only (12 bytes), stream 1 = UV only (8 bytes).
static const D3DVERTEXELEMENT9 kTwoStreamDecl[] = {
    {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {1, 0,  D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()
};

VK_TEST(VertexDecl, TwoStreamStrides)
{
    VK_EXPECT(vk_GetDeclVertexSize(kTwoStreamDecl, 0) == 12);
    VK_EXPECT(vk_GetDeclVertexSize(kTwoStreamDecl, 1) == 8);
    return true;
}

// Null decl must return 0 without crashing.
VK_TEST(VertexDecl, NullDeclSafe)
{
    VK_EXPECT(vk_GetDeclLength(nullptr) == 0);
    VK_EXPECT(vk_GetDeclVertexSize(nullptr, 0) == 0);
    return true;
}

#endif // VK_ENABLE_TESTS
