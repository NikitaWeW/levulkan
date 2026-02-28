#version 460

#include "../extensions.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;

layout (buffer_reference, scalar) readonly buffer MatrixDataReference {
    mat4 uProjMat;
    mat4 uViewMat;
    mat4 uModelMat;
    mat4 uNormMat;
};
layout(push_constant) uniform PushConstants
{
	MatrixDataReference uMatrixDataReference;
};

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

    vs_out.pos = vec3(uModelMat * vec4(aPosition, 1));
    gl_Position = uMatrixDataReference.uProjMat * uMatrixDataReference.uViewMat * vec4(vs_out.pos, 1);
    vs_out.uv = aUV;
    
    normal = vec3(normalize(uNormMat * vec4(normal, 0)));
    tangent = vec3(normalize(uNormMat * vec4(tangent, 0)));
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal);
    vs_out.tbn = mat3(tangent, bitangent, normal);
}