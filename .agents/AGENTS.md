# X-Ray Engine Development Rules

## Overarching Project Goal
- **Non-Destructive Vulkan Integration:** The primary goal of this project is to incorporate a Vulkan-based renderer into the xray-monolith engine in the most non-destructive way possible.
- **Maintain Existing Renderers:** We must maintain the ability to build and run all other existing render layers (DX9, DX10, DX11) exactly as they are, without modifying them or the core engine whenever possible. Any changes to the core engine (like `xrGame` or `xrEngine`) must be renderer-agnostic and completely safe for legacy renderers.

## C++ Renderer Constraints (`xrGame.dll` & `xrRenderVK`)
- **NEVER** use renderer-specific preprocessor macros (e.g., `#if defined(USE_VK)`, `#if defined(USE_DX11)`) inside the `xrGame` project. The `xrGame` project is compiled renderer-agnostic, and these macros will evaluate to false, silently stripping the code.
- **ALWAYS** use runtime console checks to verify the renderer in `xrGame` (e.g., `if (xr_strcmp(Console->GetString("renderer"), "renderer_vk") == 0)`).
- **BEWARE OF STUBS:** When working with the `xrRenderVK` development branch, always proactively verify that the functions or methods you are calling are fully implemented. The engine contains many placeholder stubs (e.g., empty implementations or those returning default values) during Vulkan development.
- **Global Resource Lifecycle Scoping:** Never register persistent, global device resources (such as `RCache.Vertex` dynamic buffers or device-wide fallback textures) into per-level resource tracking containers that are purged during `level_Unload()`. Dynamic streams outlive level transitions and must be explicitly managed by their owning subsystems to prevent dangling pointers.

## Script Initialization (Anomaly)
- **NEVER** rely on `on_game_start()` to register callbacks for the initial boot Main Menu. The engine does not evaluate `on_game_start()` callbacks until the `ALifeSimulator` starts (i.e., when loading a save or starting a new game).
- For boot-level main menu modifications via script, you must either inject a call directly into `ui_main_menu.script` or wait for the player to load a game.

## UI Component Safety (`CUIWindow` / `CUIStatic`)
- When injecting purely visual components (like logos or backgrounds) dynamically in C++, **ALWAYS** call `Enable(false)` on the component (e.g., `vk_logo->Enable(false)`). 
- This prevents the component from intercepting `OnKeyboardAction` or `OnMouseAction` events and protects against notorious iterator invalidation crashes during `vid_restart` or Alt-Tabbing.

## Building and Testing
- Let the user build and test manually. Do not try to build the engine or run the game executable. The user will provide feedback and logs.
- When creating debug messages injected into the code, never put the opening bracket "[" and closing bracket "]" just leave as spaces. For example: "VK DEBUG CTexture::Load"

## The Vulkan Renderer X-ray Engine Developement Status (Not yet working)
- Currently the Vulkan renderer has never successfully run. Keep this in mind and never assume that the engine booted up properly or rendered properly. If the user provides a log assume that the game crashed or had some kind of error and if the log is not clear enough, ask the user for a breakdown of what happened on the last runtime test.

## Debugging Deadlocks & Logging Behavior
- **Buffered Logging Trap:** X-Ray's `xrLogger` buffers output via a background thread. If the engine deadlocks (freezes) and the process is killed forcefully via Task Manager, the final chunk of log output in memory is LOST, causing the log to truncate abruptly (sometimes mid-string). This makes it look like the engine crashed earlier than it actually did.
- **Forced Flushing:** When injecting fine-grained debug `Msg()` calls to isolate a deadlock or freeze, **ALWAYS** follow them immediately with a call to `xrLogger::FlushLog();`. This forces the background logger to synchronously flush the buffer to disk, ensuring the message is captured before the engine freezes.
- **Vulkan Handle Formatting:** Do NOT use `%p` in `Msg()` to format Vulkan non-dispatchable handles (like `VkCommandPool` or `VkBuffer`). On 32-bit builds, these handles are 64-bit integers (`uint64_t`), and passing them to `%p` (which expects a 32-bit pointer) will cause argument misalignment and potentially crash `xr_sprintf`. Cast them safely or use appropriate integer formatters.
- **VFS Pathing & Slashes:** When interacting with X-Ray's Virtual File System APIs (such as `FS.r_open`, `FS.update_path`, etc.), paths must contain Windows-style backslashes (`\`). If paths are modified to use forward slashes (`/`) for external tools/libraries (like `shaderc`), they must be converted back to backslashes before querying the VFS, otherwise VFS lookups will fail.

## Async Tasks & Lambda Captures
- **Dangerous Async Resource Destructors & Use-After-Free:** NEVER capture X-Ray Engine smart pointers (like `ref_texture`, `ref_geom`, or any `resptr_core` objects) by value in background thread lambdas (e.g. PPL tasks). Doing so takes ownership of the reference count on the worker thread, causing thread-unsafe engine destruction (like `shared_str` pool corruption) when the lambda finishes. 
  - **The Fix:** Always capture raw pointers (e.g. `CTexture* T`) in background tasks to ensure resource destruction strictly happens on the main thread. 
  - **The Catch (Use-After-Free):** If you capture a raw pointer, you **MUST** ensure the main thread waits for or flushes the background task queue (e.g. calling `textures_load_tasks.wait();`) *before* it deletes that resource. Failing to do so causes the background thread to write to freed memory that the engine has recycled for other objects, resulting in catastrophic heap corruption and seemingly unrelated Access Violations.
- **MSVC Default Capture Syntax:** When defining C++ lambdas with default copy capture `[=]`, do NOT explicitly enumerate variables captured by value (e.g. `[=, var]`), as MSVC will fail compilation with `C3489`. Use `[=]` alone.

## Renderer Architecture & Geometry Queuing (`IDSGraphManager`)
- **Composition over Inheritance:** In X-Ray's rendering backend (R1-R4, VK), `CRender` does NOT implement `add_Static` or `add_Dynamic` via `IRender_interface`. Instead, these methods belong to `IDSGraphManager` and are implemented by `CDSGraphManager`.
- **Traversal Flow:** `CRender` uses a composed `CDSGraphManager GMBase`. When `GMBase.traverse()` is called, it recursively traverses spatial sectors and internally pushes visible geometry into render queues (e.g., `r_dsgraph`).
- **Debugging Missing Geometry:** If geometry is missing but UI draws successfully, the drop point is typically:
  1. **CPU Culling:** `HOM.MT_RENDER()` (software occlusion) culling everything due to invalid depth, or `pLastSector` being invalid.
  2. **GPU Dispatch:** The queued geometry is skipped in `RCache.Render()` due to an invalid render pass state (`vk_EnsureRenderPassActive`) or pipeline compilation failure.

## Windows Search Constraints
- **PowerShell `Select-String`:** Do not use `Select-String -Recurse` with wildcard paths (e.g., `src\*.cpp -Recurse`), as it will fail in PowerShell. Always use the built-in `grep_search` tool for global codebase searches.

## Vulkan Porting Pitfalls & Silent Failures
- **The `nullptr` Stub Trap (UI & Video):** The `xrRenderVK` factory classes (e.g., `dxRenderFactory`) currently contain many hardcoded `return nullptr;` stubs for UI elements, fonts, and videos. If the engine boots to a completely black screen, it is often because the engine is playing an intro video or waiting at the main menu, but the UI renderers are stubbed out and silently skipped.
- **Silent Shader Failures (Black Screen Geometry):** If `shaderc` fails to compile a Pixel Shader (often due to legacy DX9 HLSL syntax/macros), the Vulkan backend catches this and silently creates a **Vertex-Only (Depth) Pipeline**. In RenderDoc, the draw calls will appear, textures will be bound, and the "Mesh Output" will show the geometry perfectly, but the render target will remain completely black because no color is rasterized. Always verify the Fragment Shader stage exists in RenderDoc's Pipeline State.
- **Missing Engine Boilerplate (Sector Tracking):** Unlike DX11, Vulkan's `CRender::Calculate()` may be missing crucial per-frame engine state updates, such as `detectLastSector(Device.vCameraPosition)`. Without this, the engine defaults to the global outdoor sector (`Sectors[0]`), which causes the HOM/Portal system to aggressively cull all interior building geometry.
- **Render Pass Lifecycle (`CRender::Calculate`):** The Vulkan backend uses Dynamic Rendering which strictly requires closing passes (`vkCmdEndRendering`) before opening new ones. The Vulkan port's render loop is currently malformed compared to DX11 (e.g., duplicate `phase_scene_begin` calls, or calling `render_forward()` outside of any active pass). Always cross-reference the render loop order directly with DX11 (`r4_R_render.cpp`).