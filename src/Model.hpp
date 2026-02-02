// Source: github.com/nikitawew/breakout
#pragma once
#include "nicecs/ecs.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <vector>
#include <string>

constexpr ecs::entity INVALID_ENTITY = 0;

template<typename T>
struct Bitmap
{
    std::vector<T> pixels;
    unsigned numComponents;
    glm::uvec2 size;

    inline std::size_t getOffsetOf(glm::uvec2 pos) const { return numComponents * (pos.y * size.x + pos.x); }
};

struct Texture
{
    Bitmap<unsigned char> bitmap;
    bool srgb = false;
    unsigned numMipLevels = 0;
    std::string path;
};

struct Material
{
    /// @brief Contains entities with the Texture component, invalid if not present.
    struct Textures
    {
        ecs::entity albedo = INVALID_ENTITY;
        ecs::entity metallic = INVALID_ENTITY;
        ecs::entity roughness = INVALID_ENTITY;
        ecs::entity ambient = INVALID_ENTITY;
        ecs::entity normal = INVALID_ENTITY;
        ecs::entity displacement = INVALID_ENTITY;
    } textures;
    struct Properties
    {
        glm::vec3 ambient;
        glm::vec4 albedo;
        glm::vec3 specular;
        glm::vec3 emission;

        float shininess;
        float metallic;
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
        // hope 4 bones per vertex would be enough
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
