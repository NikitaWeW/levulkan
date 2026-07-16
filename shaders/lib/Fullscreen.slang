static const float3 positions[3] = {
    -1,-1, 0,
     3,-1, 0,
    -1, 3, 0 
};

struct VsOut
{
    float4 position : SV_Position;
    float2 uv;
};

[shader("vertex")]
VsOut vsMain(uint index : SV_VertexID)
{
    VsOut out;
    out.position = float4(positions[index], 1);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y *= -1;
}