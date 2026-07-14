
#version 460
#include "common/Extensions.glsl"
#include "common/Material.glsl"
#include "common/MatrixData.glsl"

layout(set = 0, binding = 0, scalar) uniform UniformBuffer {
    Material uMaterial;
    CameraMatrixData uCameraMatrixData;
    ModelMatrixData uModelMatrixData;
};

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
