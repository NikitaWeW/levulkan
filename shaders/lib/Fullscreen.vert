const vec3 positions[3] = vec3[](
    vec3(-1, 2, 0),
    vec3(-1, 0, 0),
    vec3( 2, 0, 0),
);

layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = positions[gl_VertexIndex];
    vTexCoord = gl_Position.xy * 0.5 + 0.5;
}