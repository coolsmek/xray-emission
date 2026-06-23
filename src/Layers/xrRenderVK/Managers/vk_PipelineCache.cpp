/* vk_PipelineCache.cpp: This is a critical file. It tracks a std::unordered_map that pairs a state signature
(Blend state, Depth/Stencil state, Primitive topology, and Active Shaders) to a concrete, pre-compiled VkPipeline.
When the backend wants to draw, it requests a matching pipeline layout from this cache. */
