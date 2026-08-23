// white_triangle.vert.hlsl — Minimal vertex shader for Layer 2 tests
// Compile with: dxc -T vs_6_0 -E main -spirv -Fo white_triangle.vert.spv white_triangle.vert.hlsl
//
// Outputs a simple triangle positioned at three hard-coded locations.
// No uniform buffers or textures — tests that reflection finds empty bindings.

float4 main(uint vid : SV_VertexID) : SV_Position
{
    float2 positions[3] = { {-0.5, -0.5}, {0.5, -0.5}, {0.0, 0.5} };
    return float4(positions[vid % 3], 0.0, 1.0);
}

