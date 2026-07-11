const vec3 positions[3] = vec3[](
    vec3(-1,-1, 0),
    vec3( 3,-1, 0),
    vec3(-1, 3, 0) 
);

layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 1);
    vTexCoord = gl_Position.xy * 0.5 + 0.5;
    vTexCoord.y *= -1;
}