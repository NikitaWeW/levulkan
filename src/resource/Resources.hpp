#pragma once
#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <vector>
#include <string>

template<typename T>
struct Bitmap
{
    std::vector<T> pixels;
    unsigned numComponents;
    glm::uvec2 size;

    inline std::size_t offsetOf(glm::uvec2 pos) const { return numComponents * (pos.y * size.x + pos.x); }
};

template<typename T = uint8_t>
struct Texture {
    Bitmap<T> bitmap;
    std::string path;
    bool srgb = false;
    bool linearSampling = false;
    unsigned numMipLevels = 1;
    enum class AddressMode {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge,
    } addressMode = AddressMode::Repeat;
};
using Texture2D = Texture<>;
struct Cubemap {
    std::array<Bitmap<float>, 6> faces;
    std::string path;
};

struct Material
{
    /// @brief Contains entities with the Texture component, invalid if not present.
    struct Textures
    {
        ecs::entity albedo = 0;
        ecs::entity metallic = 0;
        ecs::entity roughness = 0;
        ecs::entity ambient = 0;
        ecs::entity normal = 0;
        ecs::entity displacement = 0;
    } textures;
    struct Properties
    {
        glm::vec3 ambient;
        glm::vec4 albedo{1.0f};
        glm::vec3 specular;
        glm::vec3 emission;

        float shininess;
        float metallic = 1.0f;
        float roughness = 1.0f;
        float ior;
    } properties;
};
struct Animation
{
    struct PositionKey
    {
        glm::vec3 value;
        float timeTicks;
    };
    struct OrientationKey
    {
        glm::quat value;
        float timeTicks;
    };
    struct ScaleKey
    {
        glm::vec3 value;
        float timeTicks;
    };
    struct Keyframes
    {
        std::vector<PositionKey   > positions;
        std::vector<OrientationKey> orientations;
        std::vector<ScaleKey      > scales;
    };

    std::vector<Keyframes> bones;
    std::string name = "";
    float durationTicks = 0;
    float ticksPerSecond = 0;
};
struct Mesh
{
    struct Geometry
    {
        // guaranteed
        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> texCoords;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec3> tangents;
        std::vector<unsigned> indices;

        // optional
        std::vector<glm::vec4> boneIDs;
        std::vector<glm::vec4> weights;
    } geometry;
    
    Material material;
};
struct Model
{
    std::vector<Mesh> meshes;
    std::vector<Animation> animations;
    std::string path;

    std::vector<ecs::entity> lights;

    struct Skeleton
    {
        glm::mat4 globalInverseTransform;
        std::vector<glm::mat4> bindTransform;
        std::vector<glm::mat4> nodeTransform;
        std::vector<int> parents; // -1 if root
        std::unordered_map<std::string, unsigned> boneMap; // bone name to bone id
    } skeleton;
};
