struct Material {
    struct MaterialProperties {
        float3 ambient;
        float4 albedo;
        float3 specular;
        float3 emission;

        float shininess;
        float metallic;
        float roughness;
        float ior;
    };
    struct MaterialTextures {
        uint albedo;
        uint metallic;
        uint roughness;
        uint ambient;
        uint normal;
        uint displacement;
    };

    MaterialProperties properties;
    MaterialTextures textures;
};