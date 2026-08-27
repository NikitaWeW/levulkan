#include "Logging.hpp"
#include "resource/Resources.hpp"
#include "resource/Loaders.hpp"

#include "vk/vk.hpp"
#include "ECS.hpp"

// TODO: Move these somewhere idk

struct Transform {
    glm::vec3 position{0};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 scale{1};
    inline glm::mat4 getMat() const {
        return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(orientation) * glm::scale(glm::mat4{1.0f}, scale);
    };
};
struct ModelInstance {
    Entity eModel;
};

struct VulkanMaterial {
    ::Material::Properties properties;
    // Texture2D array indices
    struct Textures
    {
        uint32_t albedo;
        uint32_t metallic;
        uint32_t roughness;
        uint32_t ambient;
        uint32_t normal;
        uint32_t displacement;
    } textures;
};
struct VulkanModel {
    struct Mesh 
    {
        VulkanMaterial material;
        // TODO: https://www.youtube.com/watch?v=7bSzp-QildA
        struct Buffers
        {
            vk::Buffer pos;
            vk::Buffer uv;
            vk::Buffer norm;
            vk::Buffer tan;
            vk::Buffer idx;
        } buffers;
        size_t indexCount;
        size_t meshIndex;
    };

    // TODO: add animation support

    Entity eModel;
    std::vector<Mesh> meshes;
};
struct ResizeToSwapchain {};

/// Every processed image has this component.
struct ImageIndex {
    uint32_t index = 0;
};
class ResourceAllocator {
private:
    vk::AllocationCreateInfo mAllocInfo;
    VkCommandBuffer mCommandBuffer = nullptr;
    VkFence mFence = nullptr;
    VkQueue mQueue = nullptr;
    std::vector<Entity> mProcessedImages;
public:
    /// @brief The index in the #images array
    /// Every processed image has this component.
    struct ImageIndex {
        uint32_t index = 0;
    };
    ResourceAllocator() = default;
    ResourceAllocator(vk::AllocationCreateInfo const &allocInfo, VkCommandPool commandPool, VkQueue queue);
    ~ResourceAllocator();
    inline std::vector<Entity> getProcessedImages() const { return mProcessedImages; }

    /// Adds a vk::Image component.
    /// @returns The image index
    uint32_t processImage(Entity eImage);
    /// Adds a vk::Buffer component.
    void processModel(Entity eModel);
    void begin();
    void end();
};

struct ResourceDirty {};
class DescriptorManager {
private:
    std::map<std::pair<Entity, vk::DescriptorBinding>, std::vector<RestrictedAnyEntity<vk::Image, vk::Buffer>>> mResources;
    std::map<std::pair<Entity, vk::DescriptorBinding>, VkImageLayout> mLayouts;
public:
    void addResource(RestrictedEntity<vk::Pipeline> pipeline, vk::DescriptorBinding binding, RestrictedAnyEntity<vk::Image, vk::Buffer> resource, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);
    void addResource(RestrictedEntity<vk::Pipeline> pipeline, vk::DescriptorBinding binding, std::vector<RestrictedAnyEntity<vk::Image, vk::Buffer>> resources, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);
    void addResource(RestrictedEntity<vk::Pipeline> pipeline, vk::DescriptorBinding binding, std::vector<Entity> resources, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);
    void erase(Entity pipeline, vk::DescriptorBinding binding);
    void update(uint frame = 0);
};

class RenderPassBuilder;
template<typename T>
using RenderPassSetupCallback_t = void (RenderPassBuilder &);
class RenderGraphResult;
template<typename T>
using RenderPassPostCompileCallback_t = void (T &, RenderGraphResult const &);
template<typename T>
using RenderPassRenderCallback_t = void (T &, VkCommandBuffer);

struct ResourceTraits {
    struct {
        VkBufferUsageFlags2 usage = 0;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;

        bool valid() const {
            return usage != 0 && size != 0;
        }
    } bufferTraits;
    struct {
        VkImageUsageFlags usage = 0;
        VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_NONE, 0, 1, 0, 1};
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool valid() const {
            return usage != 0 && layout != VK_IMAGE_LAYOUT_UNDEFINED;
        }
    } imageTraits;

    VkAccessFlags2 access = VK_ACCESS_NONE;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_NONE;

    bool valid() const {
        return access != VK_ACCESS_NONE && stages != VK_PIPELINE_STAGE_NONE && (bufferTraits.valid() || imageTraits.valid());
    }
};

class RenderGraphBuilder;
class RenderPassBuilder {
private:
    std::unordered_map<std::string, Entity> mExternalResources;
    std::unordered_map<std::string, vk::ImageCreateInfo::ImageInfo> mImageResources;
    std::unordered_map<std::string, uint32_t> mBufferResources;

    struct ResourceUsage {
        std::string name;
        ResourceTraits traits;
    };
    std::vector<ResourceUsage> mReads;
    std::vector<ResourceUsage> mWrites;

    friend RenderGraphResult buildRenderGraph(RenderGraphBuilder &&builder);
public:
    /// @brief Yup it adds an external resource.
    /// @param keep Keeps the contents of the resource from a previous frame (whether to set the barrier layout to undefined). Image only
    void addExternalResource(std::string_view name, RestrictedAnyEntity<vk::Image, vk::Buffer> eResource, bool keep = false);
    void addImageResource(std::string_view name, vk::ImageCreateInfo::ImageInfo info);
    void addBufferResource(std::string_view name, uint32_t size);

    void attachResourceRead(std::string_view resourceName, ResourceTraits traits);
    void attachResourceWrite(std::string_view resourceName, ResourceTraits traits);
};

class IRenderPassStorage {
public:
    virtual ~IRenderPassStorage() = default;
    virtual void setup(RenderPassBuilder &builder) = 0;
    virtual void postCompile(RenderGraphResult const &res) = 0;
    virtual void render(VkCommandBuffer cb) = 0;
};
template<typename T>
class RenderPassStorage : IRenderPassStorage {
private:
    T mData;
    std::function<RenderPassSetupCallback_t<T>>       mSetupCallback;
    std::function<RenderPassPostCompileCallback_t<T>> mPostCompileCallback;
    std::function<RenderPassRenderCallback_t<T>>      mRenderCallback;
public:
    RenderPassStorage() = default;
    RenderPassStorage(std::function<RenderPassSetupCallback_t<T>> setupCallback,
                      std::function<RenderPassPostCompileCallback_t<T>> postCompileCallback,
                      std::function<RenderPassRenderCallback_t<T>> renderCallback);

    inline void setup(RenderPassBuilder &builder) override {
        mSetupCallback(mData, builder);
    }
    inline void postCompile(RenderGraphResult const &res) override {
        mPostCompileCallback(mData, res);
    }
    inline void render(VkCommandBuffer cb) override {
        mRenderCallback(mData, cb);
    }
};

struct Barrier {
    struct Scope {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkBufferUsageFlags2 bufferUsage = 0;
        VkImageUsageFlags imageUsage = 0;

        uint32_t queueIndex = VK_QUEUE_FAMILY_IGNORED;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    };

    Entity resource;
    Scope src;
    Scope dst;
    
    VkImageSubresourceRange subresourceRange;
    VkDeviceSize offset;
    VkDeviceSize size;

    inline VkImageMemoryBarrier2 getImageBarrier(RestrictedEntity<vk::Image> eResource) const {
        auto const &image = eResource.get<vk::Image>();
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src.stages,
            .srcAccessMask = src.access,
            .dstStageMask = dst.stages,
            .dstAccessMask = dst.access,
            .oldLayout = src.layout,
            .newLayout = dst.layout,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .image = image.image,
            .subresourceRange = subresourceRange
        };
    }
    inline VkBufferMemoryBarrier2 getBufferBarrier(RestrictedEntity<vk::Buffer> eResource) const {
        auto const &buffer = eResource.get<vk::Buffer>();
        return {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = src.stages,
            .srcAccessMask = src.access,
            .dstStageMask = dst.stages,
            .dstAccessMask = dst.access,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .buffer = buffer.buffer,
            .offset = offset,
            .size = size
        };
    }
};
struct RenderPass {
    std::string name;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    std::vector<Barrier> barriers;
    VkQueueFlagBits queue = VK_QUEUE_GRAPHICS_BIT;

    std::unique_ptr<IRenderPassStorage> storage;

    inline void render(VkCommandBuffer cb) const {
        assert(storage);
        storage->render(cb);
    }
};

class RenderGraphBuilder {
private:
    struct Pass {
        std::string name;
        VkQueueFlagBits queue;
        std::unique_ptr<IRenderPassStorage> storage;
    };
    std::unordered_map<std::string, Pass> mPasses;
    friend RenderGraphResult buildRenderGraph(RenderGraphBuilder &&);
public:
    RenderGraphBuilder() = default;
    RenderGraphBuilder(RenderGraphBuilder const &) = delete;
    RenderGraphBuilder &operator=(RenderGraphBuilder const &) = delete;
    RenderGraphBuilder(RenderGraphBuilder &&) = default;
    RenderGraphBuilder &operator=(RenderGraphBuilder &&) = default;
    ~RenderGraphBuilder() = default;

    template<typename Data_t>
    void addPass(std::string_view name, VkQueueFlagBits queue,
        std::function<RenderPassSetupCallback_t<Data_t>> setup, 
        std::function<RenderPassPostCompileCallback_t<Data_t>> post, 
        std::function<RenderPassRenderCallback_t<Data_t>> render);
};

struct GraphvizSettings {
    bool implicitDependencies = true; ///< If set to true, dashed arrows will point to implicit pass dependencies (read before next write).
    bool showHistory = true; ///< If set to true, dotted arrows will point from the last pass that wrote to the resource to the pass that reads the history.
    std::vector<std::string> graphAttributes = {"beautify=true", "nodesep=0.5", "ranksep=0.5", "rankdir=TB"};
    std::vector<std::string> nodeAttributes = {};
    std::vector<std::string> edgeAttributes = {"stype=solid",  "constraint=true",  "arrowhead=normal"};
    // std::vector<std::string> explicitEdgeAttributes = {"stype=solid",  "constraint=true",  "arrowhead=normal", "weight=2"};
    // std::vector<std::string> implicitEdgeAttributes = {"style=dotted", "constraint=false", "arrowhead=empty",  "weight=1"};
    // std::vector<std::string> historyEdgeAttributes  = {"stype=dotted", "constraint=false", "arrowhead=empty",  "weight=1"};
};

class RenderGraphResult {
private:
    struct RenderGraphResultImpl *mImpl = nullptr;
public:
    RenderGraphResult(RenderGraphResultImpl *data);
    ~RenderGraphResult();

    bool success() const;

    // Just in case. Who knows... hehe...
    void setResource(std::string_view name, Entity eResource);
    Entity getResource(std::string_view name) const;

    // 0 - invalid
    uint findPass(std::string_view name) const;
    RenderPass const &getPass(uint id) const;

    /// @brief Get the execution order of the passes
    /// Contains pass id's
    std::vector<uint> const &getPassStack() const;

    /// @brief Generate a DOT graph.
    /// @param indent If indent is nonnegative, then elements will be pretty-printed with that indent level. 
    /// An indent level of 0 will only insert newlines. -1 (the default) selects the most compact representation.
    std::string dumpGraphviz(int indent = -1, GraphvizSettings settings = {}) const;
};

RenderGraphResult buildRenderGraph(RenderGraphBuilder &&builder);

// ===================================================================================

template<typename Data_t>
inline void RenderGraphBuilder::addPass(std::string_view name, VkQueueFlagBits queue,
  std::function<RenderPassSetupCallback_t<Data_t>> setup, 
  std::function<RenderPassPostCompileCallback_t<Data_t>> post, 
  std::function<RenderPassRenderCallback_t<Data_t>> render) {
    if(mPasses.contains(std::string(name))) {
        LOG_ERROR("Pass \"{}\" already exists!", name);
        return;
    }
    
    mPasses[std::string(name)] = Pass{
        .name = std::string(name),
        .queue = queue,
        .storage = std::unique_ptr<IRenderPassStorage>(new RenderPassStorage<Data_t>(setup, post, render))
    };
}