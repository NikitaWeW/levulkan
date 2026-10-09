#include "common/Data.slang"
#include "common/Attributes.slang"

struct UniformBuffer {
    column_major float4x4 projMat;
    column_major float4x4 viewMat;
};
struct InstanceData {
    ModelData[] data;
};
struct PushConstants {
    UniformBuffer *uBuffer;
    InstanceData *instanceData;
};

struct VsOut {
    float4 clipPos : SV_Position;
    float3 viewPos;
    float2 uv;
    float3x3 tbn;
    nointerpolation uint instanceID;
};

[[shader("vertex")]]
VsOut vsMain(Attributes attr, uint instanceID : SV_INSTANCEID, uniform PushConstants pushConstants) {
    ModelData modelData = pushConstants.instanceData->data[instanceID];
    VsOut out;
    out.instanceID = instanceID;

    float3 position = attr.position;
    float3 normal = attr.normal;
    float3 tangent = attr.tangent;

    out.viewPos = mul(mul(pushConstants.uBuffer->viewMat, modelData.modelMat), float4(attr.position, 1)).xyz;
    out.clipPos = mul(pushConstants.uBuffer->projMat, float4(out.viewPos, 1));
    out.uv = attr.uv;
    
    normal  = normalize(mul(mul(pushConstants.uBuffer->viewMat, modelData.normMat), float4(normal, 0))).xyz;
    tangent = normalize(mul(mul(pushConstants.uBuffer->viewMat, modelData.normMat), float4(tangent, 0))).xyz;
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    float3 bitangent = cross(tangent, normal);
    out.tbn = float3x3(tangent, bitangent, normal);

    return out;
}

[[vk::binding(0, 0)]] 
uniform Sampler2D uTextures[];

struct FsOut {
    [[vk::location(0)]] float4 albedo;
    [[vk::location(1)]] float4 position;
    [[vk::location(2)]] float4 normal;
    [[vk::location(3)]] float4 pbr;
};

float4 texture(uint index, float2 uv) {
    return uTextures[index].Sample(uv);
}

[[shader("fragment")]]
FsOut fsMain(VsOut in, uniform PushConstants pushConstants) {
    ModelData modelData = pushConstants.instanceData->data[in.instanceID];

    in.tbn = transpose(in.tbn);
    var uv = in.uv;
    FsOut out;
    out.albedo = modelData.material.properties.albedo * texture(modelData.material.textures.albedo, uv);
    out.position = float4(in.viewPos, 1);

    out.normal.a = 1;
    out.normal.xyz = texture(modelData.material.textures.normal, uv).rgb * 2 - 1;
    out.normal.xyz = mul(in.tbn, out.normal.xyz);
    out.normal.xyz = normalize(out.normal.xyz) * 0.5 + 0.5;

    out.pbr.r = clamp(modelData.material.properties.metallic * texture(modelData.material.textures.metallic, uv).r, 0, 1);
    out.pbr.g = clamp(modelData.material.properties.roughness * texture(modelData.material.textures.roughness, uv).r, 0, 1);
    out.pbr.b = 0; // unused lol 
    out.pbr.a = 0; // unused lol

    out.albedo.a = 1;

    return out;
}
