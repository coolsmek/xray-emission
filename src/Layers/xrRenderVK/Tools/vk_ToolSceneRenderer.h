// vk_ToolSceneRenderer.h
// Self-contained minimal Vulkan renderer for the XrayModelViewer tool.
// Compiles two minimal HLSL shaders (position-only VS + flat-colour PS),
// creates a VkPipeline manually (bypassing PipelineCache), exposes
// Init / SetModel / Render / Shutdown / IsReady.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

// Forward-declared to keep this header free of xrRender pull-ins.
struct IRender_Mesh;

namespace ToolScene {

class vk_ToolSceneRenderer
{
public:
    vk_ToolSceneRenderer()  = default;
    ~vk_ToolSceneRenderer() { Shutdown(); }

    // Call once after VkDevice is ready (after OnDeviceCreate).
    //   colorFormat : VkFormat of rt_Color (e.g. VK_FORMAT_B8G8R8A8_UNORM)
    //   depthFormat : VkFormat of the HW depth buffer
    bool Init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);

    // Borrow geometry from an already-loaded IRender_Mesh.
    // vertexStride must equal the per-vertex byte size computed from the FVF.
    // Stride is auto-detected from mesh->rm_geom.stride() (SGeometry::vb_stride).
    void SetModel(IRender_Mesh* mesh);

    // Issue draw calls into cmd.  Must be called while a vkCmdBeginRendering block
    // that includes both a colour and depth attachment is active.
    void Render(VkCommandBuffer cmd, VkExtent2D extent);

    // Release all Vulkan pipeline / shader / layout objects.
    void Shutdown();

    bool IsReady()  const { return m_ready; }
    bool HasModel() const { return m_hasModel; }

    // Per-frame auto-rotation speed (degrees/frame, about Y axis).
    float autoRotateDegPerFrame = 0.06f;

private:
    bool BuildShaders();
    bool BuildPipelineLayout();
    bool BuildPipeline(VkFormat colorFormat, VkFormat depthFormat);

    VkDevice         m_device          = VK_NULL_HANDLE;
    VkPipeline       m_pipeline        = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout  = VK_NULL_HANDLE;
    VkShaderModule   m_vsModule        = VK_NULL_HANDLE;
    VkShaderModule   m_psModule        = VK_NULL_HANDLE;

    // Geometry (borrowed — owned by the model pool, do NOT vkDestroyBuffer here)
    VkBuffer  m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize m_vertexBufferSize = 0;
    VkBuffer  m_indexBuffer  = VK_NULL_HANDLE;
    uint32_t  m_indexCount   = 0;
    uint32_t  m_vertexStride = 0;
    uint32_t  m_vBase        = 0;  // first vertex offset in the shared VB (bytes)
    uint32_t  m_iBase        = 0;  // first index  offset in the shared IB (bytes)

    bool m_ready    = false;
    bool m_hasModel = false;

    // Running Y-rotation accumulator (degrees)
    float m_rotY = 0.0f;
};

// Singleton instance — defined in vk_ToolSceneRenderer.cpp.
extern vk_ToolSceneRenderer g_ToolSceneRenderer;

} // namespace ToolScene



