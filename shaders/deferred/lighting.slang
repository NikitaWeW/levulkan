#include "common/MatrixData.slang"

struct UniformBuffer {
    CameraMatrixData cameraMatrixData; // Inverted
};

[[vk::binding(0, 1)]]
uniform ConstantBuffer<UniformBuffer, ScalarDataLayout> uBuffer;

#include "lib/Fullscreen.slang"

[[vk::binding(0, 0)]] uniform Sampler2D uAlbedo;
[[vk::binding(1, 0)]] uniform Sampler2D uPosition;
[[vk::binding(2, 0)]] uniform Sampler2D uNormal;
[[vk::binding(3, 0)]] uniform Sampler2D uPBR;

T reinhard<T>(T x) where T : IArithmetic {
    return x / (T(1) + x);
}

[shader("fragment")]
float4 fsMain(VsOut in) {
    float3 albedo = uAlbedo.Sample(in.uv).rgb;
    float3 normal = 0;
    {
        float4 normalValue = uNormal.Sample(in.uv);
        if(!bool(normalValue.a))
            return 0;

        normal = normalValue.rgb * 2 - 1;
        normal = float4(normal, 0).xyz;
    }
    
    // Temporary phong lighting
    const float3 sunDir = mul(uBuffer.cameraMatrixData.viewMat, float4(0.5, -1, 1, 0)).xyz;
    float3 L = -normalize(sunDir);
    float3 V = float3(0, 0, -1);
    float3 R = reflect(-L, normal);
    float3 diffuse = float3(max(dot(normal, L), 0.0025));
    float3 specular = float3(pow(max(dot(R, V), 0.0), 16.0) * 0.75) * 0;
    float4 oColor = float4(diffuse * albedo.rgb + specular, 1.0);
    // oColor.rgb = normal;    

    // Tone mapping
    oColor.rgb = reinhard(oColor.rgb);
    // Gamma correction
    oColor.rgb = pow(oColor.rgb, float3(1.0/2.2));
    return oColor;
}