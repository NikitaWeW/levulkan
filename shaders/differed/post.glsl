#version 460
#include "common/Extensions.glsl"

#stage "vertex"
#include "common/Fullscreen.vert"

#stage fragment

#include "common/Tonemap.glsl"

layout(location = 0) in vec2 uv;

layout(set = 0, binding = 0) uniform sampler2D uImage;
layout(location = 0) out vec4 oColor;

const vec3 uSunDir = vec3(0.5, -1, 1);

void main()
{
    vec3 color = texture(uImage, uv);

    // Tone mapping
    color = reinhard(color);
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    oColor = vec4(color, 1);
}
