/* vk_RenderPasses.cpp: Vulkan forces you to declare structural subpasses explicitly. This file outlines your
frame steps so the hardware knows how and when pixels transition from being written to a color target to being
read as a texture sampler (e.g., passing the G-Buffer over to the lighting deferred pass). */
