#version 460
#include "common/Extensions.glsl"

#stage "vertex"
#include "common/Fullscreen.vert"

#stage fragment

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uAlbedo;
layout(set = 0, binding = 1) uniform sampler2D uPosition;
layout(set = 0, binding = 2) uniform sampler2D uNormal;
layout(set = 0, binding = 3) uniform sampler2D uTangent;
layout(set = 0, binding = 4) uniform sampler2D uPBR;

const vec3 uSunDir = vec3(0.5, -1, 1);

void main()
{

    // Temporary phong lighting
    vec3 N = texture(uNormal, uv).rgb;
    vec3 L = -normalize(uSunDir);
    vec3 V = vec3(0, 0, -1);
    vec3 R = reflect(-L, N);
    vec3 diffuse = vec3(max(dot(N, L), 0.0025));
    vec3 specular = vec3(pow(max(dot(R, V), 0.0), 16.0) * 0.75) * 0;
    vec3 color = texture(uAlbedo, uv).rgb;
    oColor = vec4(diffuse * color.rgb + specular, 1.0);
}
