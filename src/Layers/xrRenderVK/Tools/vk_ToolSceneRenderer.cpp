// vk_ToolSceneRenderer.cpp
// Minimal Vulkan scene renderer for the XrayModelViewer tool.
// No descriptor sets, no uniform buffers — uses a 64-byte push constant for MVP.
#include "stdafx.h"

#include "vk_ToolSceneRenderer.h"
#include "../Resources/vk_Shader.h"       // vk_CompileHlslToSpirv / vk_CreateShaderModule
#include "../Resources/vk_BufferUtils.h"  // vk_GetDeclVertexSize
#include "../RenderTarget/vk_LayoutTransitions.h"
#include "../Managers/vk_MemoryManager.h" // MemoryManager (used by vk_BufferUtils)
#include "../xrRender/FBasicVisual.h"     // IRender_Mesh

#include <cmath>   // sinf / cosf / tanf
#include <cstring> // memcpy

namespace ToolScene {

// ─────────────────────────────────────────────────────────────────────────────
// Singleton instance
// ─────────────────────────────────────────────────────────────────────────────
vk_ToolSceneRenderer g_ToolSceneRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// Embedded HLSL shaders
// ─────────────────────────────────────────────────────────────────────────────
static const char kVS_HLSL[] = R"hlsl(
struct VSIn {
    float3 pos : POSITION;
};
struct VSOut {
    float4 sv_pos : SV_POSITION;
};

[[vk::push_constant]]
cbuffer PC {
    row_major float4x4 mvp;
};

VSOut main(VSIn v) {
    VSOut o;
    o.sv_pos = mul(float4(v.pos, 1.0f), mvp);
    return o;
}
)hlsl";

static const char kPS_HLSL[] = R"hlsl(
float4 main() : SV_TARGET {
    // Flat brownish-grey — placeholder until PBR shading is wired
    return float4(0.55f, 0.45f, 0.35f, 1.0f);
}
)hlsl";

// ─────────────────────────────────────────────────────────────────────────────
// Minimal 4x4 matrix helpers (row-major, right-hand coords → VK NDC via Y-flip)
// Fmatrix convention: row vectors, mul(v, M) — same as mul(float4(pos,1), mvp)
// ─────────────────────────────────────────────────────────────────────────────
struct Mat4 { float m[16]; }; // row-major, [row*4 + col]

static Mat4 Mat4_Identity()
{
    Mat4 r{};
    r.m[0]=1; r.m[5]=1; r.m[10]=1; r.m[15]=1;
    return r;
}

// Row-major multiply: C = A * B
static Mat4 Mat4_Mul(const Mat4& A, const Mat4& B)
{
    Mat4 C{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k)
                C.m[r*4+c] += A.m[r*4+k] * B.m[k*4+c];
    return C;
}

// Rotation about Y axis (radians) — row-major
static Mat4 Mat4_RotY(float rad)
{
    Mat4 r = Mat4_Identity();
    float c = cosf(rad), s = sinf(rad);
    r.m[0]  =  c;  r.m[2]  = -s;   // row 0: x-axis
    r.m[8]  =  s;  r.m[10] =  c;   // row 2: z-axis
    return r;
}

// View matrix: eye looking at origin, up=(0,1,0)
// Row-major lookAt (eye, center, up)
static Mat4 Mat4_LookAt(float ex, float ey, float ez,
                         float cx, float cy, float cz)
{
    // forward = normalize(center - eye)
    float fx = cx-ex, fy = cy-ey, fz = cz-ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx/=fl; fy/=fl; fz/=fl;
    // right = normalize(forward x up)  where up=(0,1,0)
    float rx = fz, ry = 0.0f, rz = -fx;
    float rl = sqrtf(rx*rx + ry*ry + rz*rz);
    rx/=rl; ry/=rl; rz/=rl;
    // up' = right x forward
    float ux = ry*fz - rz*fy;
    float uy = rz*fx - rx*fz;
    float uz = rx*fy - ry*fx;

    // Row-major view matrix (basis vectors as rows, translation as last row)
    Mat4 v{};
    v.m[0] = rx;  v.m[1] = ux;  v.m[2] = fx;  v.m[3] = 0;
    v.m[4] = ry;  v.m[5] = uy;  v.m[6] = fy;  v.m[7] = 0;
    v.m[8] = rz;  v.m[9] = uz;  v.m[10]= fz;  v.m[11]= 0;
    v.m[12]= -(rx*ex + ry*ey + rz*ez);
    v.m[13]= -(ux*ex + uy*ey + uz*ez);
    v.m[14]= -(fx*ex + fy*ey + fz*ez);
    v.m[15]= 1;
    return v;
}

// Perspective projection — Vulkan clip space (depth [0,1], Y-down)
// Row-major: mul(rowVec, P)
static Mat4 Mat4_PerspectiveVK(float fovYRad, float aspect, float zNear, float zFar)
{
    float f = 1.0f / tanf(fovYRad * 0.5f);
    Mat4 p{};
    p.m[0]  = f / aspect;
    p.m[5]  = -f;                               // flip Y for Vulkan
    p.m[10] = zFar / (zFar - zNear);
    p.m[11] = 1.0f;
    p.m[14] = -(zFar * zNear) / (zFar - zNear);
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init — compile shaders, build pipeline layout and pipeline
// ─────────────────────────────────────────────────────────────────────────────
bool vk_ToolSceneRenderer::Init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
{
    if (m_ready) return true; // already initialised
    m_device = device;

    if (!BuildShaders())         return false;
    if (!BuildPipelineLayout())  return false;
    if (!BuildPipeline(colorFormat, depthFormat)) return false;

    m_ready = true;
    Msg("* [ToolScene] vk_ToolSceneRenderer::Init OK (colorFmt=%d depthFmt=%d)",
        (int)colorFormat, (int)depthFormat);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SetModel — borrow geometry from an already-loaded IRender_Mesh
// ─────────────────────────────────────────────────────────────────────────────
void vk_ToolSceneRenderer::SetModel(IRender_Mesh* mesh)
{
    if (!mesh)
    {
        m_hasModel = false;
        return;
    }

    if (!mesh->p_rm_Vertices || mesh->p_rm_Vertices->buffer == VK_NULL_HANDLE)
    {
        Msg("! [ToolScene] SetModel: p_rm_Vertices is null or has no VkBuffer");
        m_hasModel = false;
        return;
    }
    if (!mesh->p_rm_Indices  || mesh->p_rm_Indices->buffer == VK_NULL_HANDLE)
    {
        Msg("! [ToolScene] SetModel: p_rm_Indices is null or has no VkBuffer");
        m_hasModel = false;
        return;
    }

    // Read the true per-vertex stride from SGeometry::vb_stride — set by
    // rm_geom.create() for both OGF_GCONTAINER (shared VB) and OGF_VERTICES paths.
    // Fall back to 32 only if rm_geom is not yet bound.
    m_vertexStride = (mesh->rm_geom) ? mesh->rm_geom.stride() : 32u;

    m_vertexBuffer = mesh->p_rm_Vertices->buffer;
    m_vertexBufferSize = mesh->p_rm_Vertices->size;
    m_indexBuffer  = mesh->p_rm_Indices->buffer;
    m_indexCount   = mesh->iCount;
    m_vBase        = static_cast<uint32_t>(mesh->vBase) * m_vertexStride; // byte offset
    m_iBase        = static_cast<uint32_t>(mesh->iBase) * sizeof(uint16_t); // byte offset
    m_hasModel     = true;

    Msg("* [ToolScene] SetModel: VB=%p IB=%p iCount=%u stride=%u vBase=%u iBase=%u",
        (void*)m_vertexBuffer, (void*)m_indexBuffer,
        m_indexCount, m_vertexStride, m_vBase, m_iBase);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render — called from inside an active vkCmdBeginRendering block
// ─────────────────────────────────────────────────────────────────────────────
void vk_ToolSceneRenderer::Render(VkCommandBuffer cmd, VkExtent2D extent)
{
    if (!m_ready || !m_hasModel) return;

    // ── Dynamic viewport / scissor ────────────────────────────────────────────
    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(extent.width);
    vp.height   = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sci{};
    sci.offset = {0, 0};
    sci.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &sci);

    // ── Build MVP push constant ───────────────────────────────────────────────
    m_rotY += autoRotateDegPerFrame;
    if (m_rotY > 360.0f) m_rotY -= 360.0f;

    float aspect = (extent.height > 0)
                 ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                 : 1.0f;

    Mat4 model = Mat4_RotY(m_rotY * (3.14159265f / 180.0f));
    // Eye at (0, 1.2, -4) looking at origin — shows box_wood_01 roughly centred
    Mat4 view  = Mat4_LookAt(0.0f, 1.2f, -4.0f,  0.0f, 0.0f, 0.0f);
    Mat4 proj  = Mat4_PerspectiveVK(0.7854f /*45°*/, aspect, 0.1f, 500.0f);
    Mat4 mvp   = Mat4_Mul(Mat4_Mul(model, view), proj);

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(mvp.m), mvp.m);

    // ── Bind pipeline ─────────────────────────────────────────────────────────
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // ── Bind vertex buffer (dynamic stride via g_vkCmdBindVertexBuffers2) ─────
    VkDeviceSize vbOffset  = static_cast<VkDeviceSize>(m_vBase);
    VkDeviceSize vbSize    = (m_vertexBufferSize > vbOffset) ? (m_vertexBufferSize - vbOffset) : m_vertexBufferSize;
    VkDeviceSize vbStride  = static_cast<VkDeviceSize>(m_vertexStride);
    g_vkCmdBindVertexBuffers2(cmd, 0, 1, &m_vertexBuffer, &vbOffset, &vbSize, &vbStride);

    // ── Bind index buffer ─────────────────────────────────────────────────────
    VkDeviceSize ibOffset = static_cast<VkDeviceSize>(m_iBase);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, ibOffset, VK_INDEX_TYPE_UINT16);

    // ── Draw ──────────────────────────────────────────────────────────────────
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void vk_ToolSceneRenderer::Shutdown()
{
    if (m_device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_device);

    if (m_pipeline      != VK_NULL_HANDLE) { vkDestroyPipeline     (m_device, m_pipeline,       nullptr); m_pipeline       = VK_NULL_HANDLE; }
    if (m_pipelineLayout!= VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_vsModule      != VK_NULL_HANDLE) { vkDestroyShaderModule  (m_device, m_vsModule,        nullptr); m_vsModule       = VK_NULL_HANDLE; }
    if (m_psModule      != VK_NULL_HANDLE) { vkDestroyShaderModule  (m_device, m_psModule,        nullptr); m_psModule       = VK_NULL_HANDLE; }

    m_ready    = false;
    m_hasModel = false;
    m_device   = VK_NULL_HANDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildShaders — compile embedded HLSL to SPIR-V via vk_CompileHlslToSpirv
// ─────────────────────────────────────────────────────────────────────────────
bool vk_ToolSceneRenderer::BuildShaders()
{
    // Vertex shader
    auto vsSpv = vk_CompileHlslToSpirv(kVS_HLSL, sizeof(kVS_HLSL)-1,
                                        "vs", "main", "tool_scene_vs");
    if (vsSpv.empty())
    {
        Msg("! [ToolScene] Failed to compile tool_scene_vs");
        return false;
    }
    m_vsModule = vk_CreateShaderModule(m_device, vsSpv.data(), vsSpv.size() * sizeof(uint32_t));
    if (m_vsModule == VK_NULL_HANDLE) { Msg("! [ToolScene] vk_CreateShaderModule VS failed"); return false; }

    // Pixel shader
    auto psSpv = vk_CompileHlslToSpirv(kPS_HLSL, sizeof(kPS_HLSL)-1,
                                        "ps", "main", "tool_scene_ps");
    if (psSpv.empty())
    {
        Msg("! [ToolScene] Failed to compile tool_scene_ps");
        return false;
    }
    m_psModule = vk_CreateShaderModule(m_device, psSpv.data(), psSpv.size() * sizeof(uint32_t));
    if (m_psModule == VK_NULL_HANDLE) { Msg("! [ToolScene] vk_CreateShaderModule PS failed"); return false; }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPipelineLayout — push constant: 64 bytes (float4x4) in VS
// ─────────────────────────────────────────────────────────────────────────────
bool vk_ToolSceneRenderer::BuildPipelineLayout()
{
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = 64; // sizeof(float[16])

    VkPipelineLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pcRange;

    VkResult res = vkCreatePipelineLayout(m_device, &ci, nullptr, &m_pipelineLayout);
    if (res != VK_SUCCESS)
    {
        Msg("! [ToolScene] vkCreatePipelineLayout failed (%d)", (int)res);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPipeline — manual VkGraphicsPipeline (not via PipelineCache)
// Vertex input: binding 0, only the first attribute (POSITION = float3)
// ─────────────────────────────────────────────────────────────────────────────
bool vk_ToolSceneRenderer::BuildPipeline(VkFormat colorFormat, VkFormat depthFormat)
{
    // ── Shader stages ─────────────────────────────────────────────────────────
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vsModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_psModule;
    stages[1].pName  = "main";

    // ── Vertex input: binding 0 ───────────────────────────────────────────────
    // stride=0 here — overridden each draw call via VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE
    // so that models with different vertex formats (32, 40, 56 bytes…) are all correct.
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = 0; // dynamic — set per-draw via g_vkCmdBindVertexBuffers2
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding  = 0;
    attr.format   = VK_FORMAT_R32G32B32_SFLOAT; // float3 position
    attr.offset   = 0;

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions    = &attr;

    // ── Input assembly ────────────────────────────────────────────────────────
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ── Viewport / scissor (both dynamic) ────────────────────────────────────
    VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    // ── Rasterization ─────────────────────────────────────────────────────────
    VkPipelineRasterizationStateCreateInfo rast = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_BACK_BIT;
    rast.frontFace   = VK_FRONT_FACE_CLOCKWISE; // X-Ray convention
    rast.lineWidth   = 1.0f;

    // ── Multisample (1x) ──────────────────────────────────────────────────────
    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Depth / stencil ───────────────────────────────────────────────────────
    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    // ── Colour blend (write all, no blending) ─────────────────────────────────
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    // ── Dynamic states ────────────────────────────────────────────────────────
    VkDynamicState dynStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE  // override stride at draw time
    };
    VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates    = dynStates;

    // ── Dynamic rendering (VkPipelineRenderingCreateInfo — no render pass) ────
    VkPipelineRenderingCreateInfo prc{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    prc.colorAttachmentCount    = 1;
    prc.pColorAttachmentFormats = &colorFormat;
    prc.depthAttachmentFormat   = depthFormat;

    // ── Assemble ──────────────────────────────────────────────────────────────
    VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    ci.pNext               = &prc;
    ci.stageCount          = 2;
    ci.pStages             = stages;
    ci.pVertexInputState   = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState      = &vps;
    ci.pRasterizationState = &rast;
    ci.pMultisampleState   = &ms;
    ci.pDepthStencilState  = &ds;
    ci.pColorBlendState    = &cb;
    ci.pDynamicState       = &dyn;
    ci.layout              = m_pipelineLayout;
    ci.renderPass          = VK_NULL_HANDLE; // dynamic rendering — no renderpass

    VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline);
    if (res != VK_SUCCESS)
    {
        Msg("! [ToolScene] vkCreateGraphicsPipelines failed (%d)", (int)res);
        return false;
    }
    Msg("* [ToolScene] Pipeline created OK");
    return true;
}

} // namespace ToolScene
