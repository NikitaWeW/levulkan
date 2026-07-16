#include "lib/Fullscreen.slang"

[[vk::binding(0, 0)]] uniform Sampler2D uAlbedo;
[[vk::binding(1, 0)]] uniform Sampler2D uPosition;
[[vk::binding(2, 0)]] uniform Sampler2D uNormal;
[[vk::binding(3, 0)]] uniform Sampler2D uPBR;

[shader("fragment")]
float4 fsMain(VsOut input)
{
    // Temporary phong lighting
    const float3 sunDir = float3(0.5, -1, 1);
    float3 N = uNormal.Sample(input.uv).rgb;
    float3 L = -normalize(sunDir);
    float3 V = float3(0, 0, -1);
    float3 R = reflect(-L, N);
    float3 diffuse = float3(max(dot(N, L), 0.0025));
    float3 specular = float3(pow(max(dot(R, V), 0.0), 16.0) * 0.75) * 0;
    float3 albedo = uAlbedo.Sample(input.uv).rgb;
    float4 oColor = float4(diffuse * albedo.rgb + specular, 1.0);

    // Tone mapping
    // oColor.rgb = reinhard(oColor.rgb);
    // Gamma correction
    oColor.rgb = pow(oColor.rgb, float3(1.0/2.2));
    return oColor;
}