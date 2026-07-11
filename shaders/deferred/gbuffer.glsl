#version 460
#include "common/Extensions.glsl"
#include "common/Material.glsl"
#include "common/MatrixData.glsl"

layout(set = 0, binding = 0, scalar) uniform UniformBuffer {
    Material uMaterial;
    CameraMatrixData uCameraMatrixData;
    ModelMatrixData uModelMatrixData;
};

/////////////
#stage "vertex"
/////////////

#include "common/Attributes.glsl"

layout(location = 0) out VS_OUT {
    vec2 uv;
    vec3 pos;
    mat3 tbn;
} vs_out;

void main()
{
    // Separated for future skinning support
    vec3 position = aPosition;
    vec3 normal = aNormal;
    vec3 tangent = aTangent;

    vs_out.pos = vec3(uCameraMatrixData.viewMat * uModelMatrixData.modelMat * vec4(aPosition, 1));
    gl_Position = uCameraMatrixData.projMat * vec4(vs_out.pos, 1);
    vs_out.uv = aUV;
    
    normal = normalize(mat3(uModelMatrixData.normMat) * normal);
    tangent = normalize(mat3(uModelMatrixData.normMat) * tangent);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal);
    vs_out.tbn = mat3(tangent, bitangent, normal);
}

///////////////
#stage fragment
///////////////

layout(location = 0) in VS_OUT {
    vec2 uv;
    vec3 pos;
    mat3 tbn;
} fs_in;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oPosition;
layout(location = 2) out vec4 oNormal;
layout(location = 3) out vec4 oPBR;

vec4 sampleTexture(uint index)
{
    return texture(textures[index], fs_in.uv);
}

void main()
{
    oAlbedo = uMaterial.properties.albedo * vec4(sampleTexture(uMaterial.textures.albedo).rgb, 1.0);
    oPosition = vec4(fs_in.pos, 1);
    oNormal.rgb = normalize(fs_in.tbn * (sampleTexture(uMaterial.textures.normal).rgb * 2.0 - 1.0));
    oNormal.a = 1;
    oPBR.r = uMaterial.properties.metallic * sampleTexture(uMaterial.textures.metallic).r;
    oPBR.g = uMaterial.properties.roughness * sampleTexture(uMaterial.textures.roughness).r;
    oPBR.b = 0;
    oPBR.a = 0;
}
