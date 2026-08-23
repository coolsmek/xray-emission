#pragma once
// vk_ImGuiRender.h — No-op IImGuiRender for the Vulkan path.
// This prevents null-deref crashes during engine initialization.

#include "../../Include/xrRender/ImGuiRender.h"
#include <imgui.h>

class vkImGuiRender : public IImGuiRender
{
    bool m_fontsBuilt = false;
public:
    virtual ~vkImGuiRender() override = default;
    virtual void Copy(IImGuiRender& _in) override {}

    virtual void Frame() override
    {
        if (!m_fontsBuilt)
        {
            ImGui::GetIO().Fonts->Build();
            m_fontsBuilt = true;
        }
    }
    virtual void Render(ImDrawData* data) override {}

    virtual void OnDeviceCreate(ImGuiContext* context) override {}
    virtual void OnDeviceDestroy() override {}
    virtual void OnDeviceResetBegin() override {}
    virtual void OnDeviceResetEnd() override
    {
        m_fontsBuilt = false;   // rebuild once after a device reset
    }
};
