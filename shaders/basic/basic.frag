#version 460

layout(location = 0) in VS_OUT {
    vec2 uv;
    vec3 pos;
    mat3 tbn;
} fs_in;

layout(set = 0, binding = 0) uniform sampler2D textures[];

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


layout(location = 0) out vec4 oColor;

void main()
{
    // Phong lighting
    vec3 N = normalize(fs_in.tbn[2]);
    vec3 L = -normalize(uSunDir);
    vec3 V = normalize(-uViewMat[2].xyz);
    vec3 R = reflect(-L, N);
    vec3 diffuse = vec3(max(dot(N, L), 0.0025));
    vec3 specular = vec3(pow(max(dot(R, V), 0.0), 16.0) * 0.75);
    vec3 color = texture(textures[0], fs_in.uv).xyz;
    oColor = vec4(diffuse * color.rgb + specular, 1.0);
}
