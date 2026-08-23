// white_triangle.frag.hlsl — Minimal fragment shader for Layer 2 tests
// Compile with: dxc -T ps_6_0 -E main -spirv -Fo white_triangle.frag.spv white_triangle.frag.hlsl
//
// Outputs pure white (1,1,1,1) for every pixel.
// No uniform buffers or textures — tests that reflection finds empty bindings.

float4 main() : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}

