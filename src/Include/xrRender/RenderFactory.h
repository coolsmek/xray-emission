#ifndef RenderFactory_included
#define RenderFactory_included
#pragma once

class IWallMarkArray;

#ifdef DEBUG
	class IObjectSpaceRender;
#endif // DEBUG

class IFontRender;
class IApplicationRender;
class IEnvDescriptorRender;
class IEnvDescriptorMixerRender;
class IFlareRender;
class ILensFlareRender;
class IRainRender;
class IThunderboltRender;
class IEnvironmentRender;
class IStatsRender;
class IRenderDeviceRender;
class IImGuiRender;
class IThunderboltDescRender;
class IStatGraphRender;
class IConsoleRender;
class IUIShader;
class IUISequenceVideoItem;

// Original commented-out interface kept for reference.
// Under USE_VK the interface is defined below via the #if defined(USE_VK) block.
#if defined(USE_VK)
// ---------------------------------------------------------------------------
// IRenderFactory — abstract interface required by the Vulkan renderer.
// DX builds never need this (dxRenderFactory is a standalone concrete class).
// ---------------------------------------------------------------------------
#define RENDER_FACTORY_INTERFACE(Class) \
virtual I##Class* Create##Class() = 0; \
virtual void Destroy##Class(I##Class *pObject) = 0;


class IRenderFactory
{
public:
    virtual ~IRenderFactory() = default;
#ifndef _EDITOR
	// virtual IStatsRender* CreateStatsRender() = 0;
	// virtual void DestroyStatsRender(IStatsRender *pObject) = 0;

	RENDER_FACTORY_INTERFACE(UISequenceVideoItem)
	RENDER_FACTORY_INTERFACE(UIShader)
	RENDER_FACTORY_INTERFACE(StatGraphRender)
	RENDER_FACTORY_INTERFACE(ConsoleRender)
	RENDER_FACTORY_INTERFACE(RenderDeviceRender)
#	ifdef DEBUG
		RENDER_FACTORY_INTERFACE(ObjectSpaceRender)
#	endif // DEBUG
	RENDER_FACTORY_INTERFACE(ApplicationRender)
	RENDER_FACTORY_INTERFACE(WallMarkArray)
	RENDER_FACTORY_INTERFACE(StatsRender)
#endif // _EDITOR

#ifndef _EDITOR
	RENDER_FACTORY_INTERFACE(EnvironmentRender)
	RENDER_FACTORY_INTERFACE(EnvDescriptorMixerRender)
	RENDER_FACTORY_INTERFACE(EnvDescriptorRender)
	RENDER_FACTORY_INTERFACE(RainRender)
	RENDER_FACTORY_INTERFACE(LensFlareRender)
	RENDER_FACTORY_INTERFACE(ThunderboltRender)
	RENDER_FACTORY_INTERFACE(ThunderboltDescRender)
	RENDER_FACTORY_INTERFACE(FlareRender)
#endif // _EDITOR
	RENDER_FACTORY_INTERFACE(FontRender)
	RENDER_FACTORY_INTERFACE(ImGuiRender)
protected:
	//virtual IEnvDescriptorRender *CreateEnvDescriptorRender() = 0;
	//virtual void DestroyEnvDescriptorRender(IEnvDescriptorRender *pObject) = 0;
};
#undef RENDER_FACTORY_INTERFACE
#endif // USE_VK

#endif	//	RenderFactory_included
