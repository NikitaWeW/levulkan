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