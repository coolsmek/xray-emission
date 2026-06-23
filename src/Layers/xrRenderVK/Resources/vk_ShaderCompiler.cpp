/* vk_ShaderCompiler.cpp — Instead of running DX11's D3DCompile system at runtime, this file
will interface with an open-source runtime compiler like Shaderc or glslang to compile
GLSL/HLSL source paths into SPIR-V byte code binaries (.spv). Alternatively, it can just
safely stream raw .spv files directly off disk. */
