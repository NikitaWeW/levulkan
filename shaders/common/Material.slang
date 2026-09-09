struct Material {
    struct MaterialProperties {
        float3 ambient;
        float _pad0;
        float4 albedo;
        float3 specular;
        float _pad1;
        float3 emission;
        float _pad2;

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