// Renderer logic, not the render graph

#pragma once
#include "engine/scene/Resources.hpp"
#include "engine/scene/Loaders.hpp"
#include "engine/vk/vk.hpp"

namespace egn {

struct VulkanMaterial {
    struct Properties {
        glm::vec3 ambient;
        glm::vec4 albedo{ 1.0f };
        glm::vec3 specular;
        glm::vec3 emission;

        float shininess = 32.0f;
        float metallic = 1.0f;
        float roughness = 1.0f;
        float ior = 1.0f;
    } properties;
    // Texture2D array indices
    struct Textures {
        uint32_t albedo = 0;
        uint32_t metallic = 0;
        uint32_t roughness = 0;
        uint32_t ambient = 0;
        uint32_t normal = 0;
        uint32_t displacement = 0;
    } textures;
};
struct VulkanModel {
    struct Mesh {
        VulkanMaterial material;
        // TODO: https://www.youtube.com/watch?v=7bSzp-QildA
        struct Buffers {
            vk::Buffer pos;
            vk::Buffer attr;
            vk::Buffer idx;
        } buffers;
        size_t indexCount;
        size_t meshIndex;
    };

    Entity eModel;
    std::vector<Mesh> meshes;
};

class ResourceAllocator {
private:
    vk::AllocationCreateInfo mAllocInfo;
    VkCommandBuffer mCommandBuffer = nullptr;
    VkFence mFence = nullptr;
    VkQueue mQueue = nullptr;
    std::vector<Entity> mProcessedImages;
    uint mProcessedCount = 0;
public:
    /// @brief The index in the images array.
    /// Every processed image has this component.
    struct ImageIndex {
        uint32_t index = 0;
    };
    ResourceAllocator() = default;
    ResourceAllocator(vk::AllocationCreateInfo const &allocInfo, VkCommandPool commandPool, VkQueue queue);
    ResourceAllocator(ResourceAllocator &&o);
    ResourceAllocator &operator=(ResourceAllocator &&o);
    ResourceAllocator(ResourceAllocator const &) = delete;
    ResourceAllocator &operator=(ResourceAllocator const &) = delete;
    ~ResourceAllocator();
    inline std::vector<Entity> getProcessedImages() const {
        return mProcessedImages;
    }

    /// Adds a vk::Image component.
    /// @returns The image index
    uint32_t processImage(Entity eImage);
    /// Adds a vk::Buffer component.
    void processModel(Entity eModel);
    void begin();
    /// @returns True if new resources were processed.
    bool end();
};

struct ResourceDirty { 
    uint updateCount = 0;
};
struct ImageDescriptorWrite {
    Entity resource;
    VkImageLayout layout;
};
struct BufferDescriptorWrite {
    Entity resource;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
};
struct DescriptorWrite {
    RestrictedEntity<vk::Pipeline> dstPipeline;
    uint dstSet = 0;
    uint dstBinding = 0;
    uint dstFrame = 0;
    uint dstElement = 0;

    std::vector<ImageDescriptorWrite> imageInfo;
    std::vector<BufferDescriptorWrite> bufferInfo;
};
class DescriptorManager {
private:
    std::vector<DescriptorWrite> mWrites;
    std::unordered_map<Entity, uint32_t> mPipelines;
public:
    void emplace(DescriptorWrite write);
    void erase(Entity pipeline, vk::DescriptorBinding binding, uint frame);
    void update(uint frame = 0, bool force = false);
};

} // namespace egn
