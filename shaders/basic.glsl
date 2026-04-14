#version 460
#include "extensions.glsl"

struct MatrixData
{
    mat4 projMat;
    mat4 viewMat;
    mat4 modelMat;
    mat4 normMat;
};
struct MaterialProperties
{
    vec3 ambient;
    vec4 albedo;
    vec3 specular;
    vec3 emission;

    float shininess;
    float metallic;
    float ior;
};
struct MaterialTextures
{
    uint albedo;
    uint metallic;
    uint roughness;
    uint ambient;
    uint normal;
    uint displacement;
};
struct Material
{
    MaterialProperties properties;
    MaterialTextures textures;
};

layout(set = 0, binding = 0, scalar) uniform UniformBuffer {
    Material uMaterial;
    MatrixData uMatrixData;
};

#stage vertex

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;
// layout(push_constant) uniform PushConstants
// {
// };

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

    vs_out.pos = vec3(uMatrixData.modelMat * vec4(aPosition, 1));
    gl_Position = uMatrixData.projMat * uMatrixData.viewMat * vec4(vs_out.pos, 1);
    vs_out.uv = aUV;
    
    normal = vec3(normalize(uMatrixData.normMat * vec4(normal, 0)));
    tangent = vec3(normalize(uMatrixData.normMat * vec4(tangent, 0)));
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    vec3 bitangent = cross(tangent, normal);
    vs_out.tbn = mat3(tangent, bitangent, normal);
}

#stage fragment

layout(location = 0) in VS_OUT {
    vec2 uv;
    vec3 pos;
    mat3 tbn;
} fs_in;

layout(set = 0, binding = 1) uniform sampler2D textures[];

layout(location = 0) out vec4 oColor;

const vec3 uSunDir = vec3(0.5, -1, 1);

void main()
{
    // Phong lighting
    vec3 N = normalize(fs_in.tbn[2]);
    vec3 L = -normalize(uSunDir);
    vec3 V = normalize(-uMatrixData.viewMat[2].xyz);
    vec3 R = reflect(-L, N);
    vec3 diffuse = vec3(max(dot(N, L), 0.0025));
    vec3 specular = vec3(pow(max(dot(R, V), 0.0), 16.0) * 0.75);
    vec3 color = texture(textures[uMaterial.textures.albedo], fs_in.uv).xyz;
    oColor = vec4(diffuse * color.rgb + specular, 1.0);
}
