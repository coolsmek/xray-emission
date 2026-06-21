// HW.h: interface for the CHW class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_HW_H__0E25CF4A_FFEC_11D3_B4E3_4854E82A090D__INCLUDED_)
#define AFX_HW_H__0E25CF4A_FFEC_11D3_B4E3_4854E82A090D__INCLUDED_
#pragma once
#include "optick-git/src/optick.h"

#if defined(USE_DX11)
#include <d3d11_4.h>
#include <dxgi1_4.h>
#elif defined(USE_VK)
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#endif

#include "hwcaps.h"

#include "../../build_config_defines.h"

#ifndef _MAYA_EXPORT
#include "stats_manager.h"
#endif

class CHW
#if defined(USE_DX10) || defined(USE_DX11)
	:	public pureAppActivate,
		public pureAppDeactivate
#endif	//	USE_DX10
{
	//	Functions section
public:
	int maxRefreshRate; //ECO_RENDER add
	CHW();
	~CHW();

	void CreateD3D();
	void DestroyD3D();
	void CreateDevice(HWND hw, bool move_window);

	void DestroyDevice();

	void Reset(HWND hw);

#if defined(USE_DX10) || defined(USE_DX11)
	IDXGIOutput* FindOutputOnCurrentAdapter(HMONITOR hMon);
#endif

	void selectResolution(u32& dwWidth, u32& dwHeight, BOOL bWindowed);
	D3DFORMAT selectDepthStencil(D3DFORMAT);
	u32 selectPresentInterval();
	u32 selectGPU();
	u32 selectRefresh(u32 dwWidth, u32 dwHeight, D3DFORMAT fmt);
	void updateWindowProps(HWND hw);
	BOOL support(D3DFORMAT fmt, DWORD type, DWORD usage);

#ifdef DEBUG
#if defined(USE_DX10) || defined(USE_DX11)
	void	Validate(void)	{};
#else	//	USE_DX10
	void	Validate(void)	{	VERIFY(pDevice); VERIFY(pD3D); };
#endif	//	USE_DX10
#else
	void Validate(void)
	{
	};
#endif

	//	Variables section
#if defined(USE_DX11)	//	USE_DX10
public:
    IDXGIFactory2*          m_pFactory; //  DXGI factory
	IDXGIAdapter1*			m_pAdapter;	//	pD3D equivalent
	IDXGIOutput*			m_pOutput;	//	the output we render to (belongs to m_pAdapter)
	ID3D11Device1*			pDevice;	//	combine with DX9 pDevice via typedef
	ID3D11DeviceContext1*   pContext;	//	combine with DX9 pDevice via typedef
	IDXGISwapChain1*        m_pSwapChain;
	ID3D11RenderTargetView*	pBaseRT;	//	combine with DX9 pBaseRT via typedef
	ID3D11DepthStencilView*	pBaseZB;
	ID3DUserDefinedAnnotation* pAnnotation;

	CHWCaps					Caps;

	D3D_DRIVER_TYPE					m_DriverType;	//	DevT equivalent
	DXGI_SWAP_CHAIN_DESC1			m_ChainDesc;	//	DevPP equivalent
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC m_ChainDescFullscreen;
    HWND                            m_hWnd;
	bool							m_bUsePerfhud;
	D3D_FEATURE_LEVEL				FeatureLevel;
	bool 							m_SupportsVRR; // whether we can use DXGI_PRESENT_ALLOW_TEARING etc.
#elif defined(USE_DX10)
public:
	IDXGIAdapter*			m_pAdapter;	//	pD3D equivalent
	IDXGIOutput*			m_pOutput;	//	the output we render to (belongs to m_pAdapter)
	ID3D10Device1*       	pDevice1;	//	combine with DX9 pDevice via typedef
	ID3D10Device*        	pDevice;	//	combine with DX9 pDevice via typedef
	ID3D10Device1*       	pContext1;	//	combine with DX9 pDevice via typedef
	ID3D10Device*        	pContext;	//	combine with DX9 pDevice via typedef
	IDXGISwapChain*         m_pSwapChain;
	ID3D10RenderTargetView*	pBaseRT;	//	combine with DX9 pBaseRT via typedef
	ID3D10DepthStencilView*	pBaseZB;

	CHWCaps					Caps;

	D3D10_DRIVER_TYPE		m_DriverType;	//	DevT equivalent
	DXGI_SWAP_CHAIN_DESC	m_ChainDesc;	//	DevPP equivalent
	HWND					m_hWnd;
	bool					m_bUsePerfhud;
	D3D_FEATURE_LEVEL		FeatureLevel;

#elif defined (USE_VK)
public:
    // ── Core Vulkan objects ───────────────────────────────────────────────────
    VkInstance              m_vkInstance            = VK_NULL_HANDLE;
    VkPhysicalDevice        m_vkPhysDevice          = VK_NULL_HANDLE;
    VkDevice                m_vkDevice              = VK_NULL_HANDLE;
    VkSurfaceKHR            m_vkSurface             = VK_NULL_HANDLE;

    // ── Queues ────────────────────────────────────────────────────────────────
    VkQueue                 m_vkGraphicsQueue       = VK_NULL_HANDLE;
    VkQueue                 m_vkPresentQueue        = VK_NULL_HANDLE;
    uint32_t                m_vkGraphicsQF          = UINT32_MAX;
    uint32_t                m_vkPresentQF           = UINT32_MAX;

    // ── Swapchain ─────────────────────────────────────────────────────────────
    VkSwapchainKHR          m_vkSwapchain           = VK_NULL_HANDLE;
    VkFormat                m_vkSCFormat            = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR         m_vkSCColorSpace        = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D              m_vkSCExtent            {};
    VkPresentModeKHR        m_vkPresentMode         = VK_PRESENT_MODE_FIFO_KHR;
    xr_vector<VkImage>      m_vkSCImages;           // owned by swapchain — do NOT free
    xr_vector<VkImageView>  m_vkSCImageViews;
    uint32_t                m_vkSCImageCount        = 0;
    uint32_t                m_vkCurrentFrame        = 0;  // index into per-frame resources

    // ── Depth buffer ──────────────────────────────────────────────────────────
    VkImage                 m_vkDepthImage          = VK_NULL_HANDLE;
    VkDeviceMemory          m_vkDepthMemory         = VK_NULL_HANDLE;
    VkImageView             m_vkDepthView           = VK_NULL_HANDLE;
    VkFormat                m_vkDepthFormat         = VK_FORMAT_D24_UNORM_S8_UINT;

    // ── Render pass ───────────────────────────────────────────────────────────
    VkRenderPass            m_vkRenderPass          = VK_NULL_HANDLE;
    xr_vector<VkFramebuffer> m_vkFramebuffers;

    // ── Command infrastructure ────────────────────────────────────────────────
    VkCommandPool           m_vkCmdPool             = VK_NULL_HANDLE;
    xr_vector<VkCommandBuffer> m_vkCmdBuffers;      // one per swapchain image

    // ── Synchronisation (double-buffered) ─────────────────────────────────────
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT  = 2;
    VkSemaphore             m_vkImageAvailable[MAX_FRAMES_IN_FLIGHT] {};
    VkSemaphore             m_vkRenderFinished[MAX_FRAMES_IN_FLIGHT] {};
    VkFence                 m_vkInFlightFences[MAX_FRAMES_IN_FLIGHT] {};

    // ── Pipeline cache (shared across all PSO compilations) ───────────────────
    VkPipelineCache         m_vkPipelineCache       = VK_NULL_HANDLE;

    // ── Physical device properties ────────────────────────────────────────────
    VkPhysicalDeviceProperties          m_vkDevProps    {};
    VkPhysicalDeviceFeatures            m_vkDevFeatures {};
    VkPhysicalDeviceMemoryProperties    m_vkMemProps    {};

    // ── Engine integration ────────────────────────────────────────────────────
    CHWCaps                 Caps;
    HWND                    m_hWnd                  = nullptr;
    bool                    m_SupportsVRR           = false;    // VK_EXT_present_id / FIFO relaxed

#else
private:
	HINSTANCE hD3D;

public:

	IDirect3D9* pD3D; // D3D
	IDirect3DDevice9* pDevice; // render device

	IDirect3DSurface9* pBaseRT;
	IDirect3DSurface9* pBaseZB;

	CHWCaps Caps;

	UINT DevAdapter;
	D3DDEVTYPE DevT;
	D3DPRESENT_PARAMETERS DevPP;
#endif	//	USE_DX10

#ifndef _MAYA_EXPORT
	stats_manager stats_manager;
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	void			UpdateViews();
	DXGI_RATIONAL	selectRefresh(u32 dwWidth, u32 dwHeight, DXGI_FORMAT fmt);

	virtual	void	OnAppActivate();
	virtual void	OnAppDeactivate();
#endif	//	USE_DX10

#if defined(USE_VK)
	// Swapchain management — called by Reset() and on window resize
	void			vk_CreateSwapchain();
	void			vk_DestroySwapchain();
	void			vk_RecreateSwapchain();

	// Per-frame helpers
	void			vk_CreateFramebuffers();
	void			vk_DestroyFramebuffers();
	void			vk_CreateCommandBuffers();
	void			vk_CreateSyncObjects();
	void			vk_DestroySyncObjects();

	// Depth buffer
	void			vk_CreateDepthBuffer();
	void			vk_DestroyDepthBuffer();

	// Memory helper
	uint32_t		vk_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

	// Present — called by CRender at end of frame
	VkResult		vk_Present();
#endif	//	USE_VK

private:
#if defined(USE_DX10) || defined(USE_DX11)
	void AcquireDefaultOutput();
#endif
#if defined(USE_DX11)
	void SelectAdapterAndOutput(HMONITOR hTargetMonitor);
#endif
#if defined(USE_VK)
	bool			vk_SelectPhysicalDevice();
	bool			vk_FindQueueFamilies();
	bool			vk_SelectSurfaceFormat(VkSurfaceFormatKHR& outFmt) const;
	VkPresentModeKHR vk_SelectPresentMode() const;
	VkExtent2D		vk_SelectSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const;
#endif	//	USE_VK
	bool m_move_window;
};

extern ECORE_API CHW HW;

#endif // !defined(AFX_HW_H__0E25CF4A_FFEC_11D3_B4E3_4854E82A090D__INCLUDED_)
