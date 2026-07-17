#include "common/Material.slang"
#include "common/MatrixData.slang"
#include "common/Attributes.slang"

struct UniformBuffer {
    Material         material;
    CameraMatrixData cameraMatrixData;
    ModelMatrixData  modelMatrixData;
};

[[vk::binding(0, 0)]]
uniform ConstantBuffer<UniformBuffer, ScalarDataLayout> uBuffer;

struct VsOut {
    float4 clipPos : SV_Position;
    float3 viewPos;
    float2 uv;
    float3x3 tbn;
};

[[shader("vertex")]]
VsOut vsMain(Attributes attr)
{
    VsOut out;
    float3 position = attr.position;
    float3 normal = attr.normal;
    float3 tangent = attr.tangent;

    out.viewPos = mul( mul(uBuffer.modelMatrixData.modelMat, uBuffer.cameraMatrixData.viewMat), float4(attr.position, 1) ).xyz;
    out.clipPos = mul( uBuffer.cameraMatrixData.projMat, float4(out.viewPos, 1) );
    out.uv = attr.uv;
    
    normal = normalize(mul( normal, float3x3(uBuffer.modelMatrixData.normMat) ));
    tangent = normalize(mul( tangent, float3x3(uBuffer.modelMatrixData.normMat) ));
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    float3 bitangent = cross(tangent, normal);
    out.tbn = float3x3(tangent, bitangent, normal);

    return out;
}

[[vk::binding(0, 1)]] 
uniform Sampler2D uTextures[];

struct FsOut {
    [[vk::location(0)]] float4 albedo;
    [[vk::location(1)]] float4 position;
    [[vk::location(2)]] float4 normal;
    [[vk::location(3)]] float4 pbr;
};

float4 texture(uint index, float2 uv)
{
    return uTextures[index].Sample(uv);
}

[[shader("fragment")]]
FsOut fsMain(VsOut in)
{
    var uv = in.uv;
    FsOut out;
    out.albedo = uBuffer.material.properties.albedo * float4(texture(uBuffer.material.textures.albedo, uv).rgb, 1.0);
    out.position = float4(in.viewPos, 1);
    out.normal.rgb = normalize(mul( (texture(uBuffer.material.textures.normal, uv).rgb * 2.0 - 1.0), in.tbn ));
    out.normal.a = 1;
    out.pbr.r = uBuffer.material.properties.metallic * texture(uBuffer.material.textures.metallic, uv).r;
    out.pbr.g = uBuffer.material.properties.roughness * texture(uBuffer.material.textures.roughness, uv).r;
    out.pbr.b = 0;
    out.pbr.a = 0;

    return out;
}
