#pragma once

#include "engine/io/Logging.hpp"
#include "engine/vk/vk.hpp"
#include "engine/dsa/ECS.hpp"

namespace egn {

class RenderPassBuilder;
template <typename T> using RenderPassSetupCallback_t = void(RenderPassBuilder &, void *);
class RenderGraphResult;
template <typename T> using RenderPassPostCompileCallback_t = void(T &, RenderGraphResult const &, void *);
template <typename T> using RenderPassRenderCallback_t = void(T &, VkCommandBuffer, void *);

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
        VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_NONE, 0, 1, 0, 1 };
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

/// Resize to render graph resource.
// struct ResizeToResource {
//     inline bool operator==(ResizeToResource const &lhs) const {
//         return (name.empty() && lhs.name.empty()) ||
//                (name == lhs.name && glm::abs(ratio.x - lhs.ratio.x) <= 1e-6f && glm::abs(ratio.y - lhs.ratio.y) <= 1e-6f);
//     }
// };

// Component (for external resources)
struct ResizeToTarget {
    glm::vec2 ratio;
};
struct RenderGraphImageCreateInfo {
    vk::ImageCreateInfo::ImageInfo imageInfo;
    /// Whether to resize the image resource to the target image.
    /// When set to true the dimensions in imageInfo may be omitted.
    bool resizeToTarget = false;
    glm::vec2 resizeRatio{ 1.0f };
    bool keep = false;
};
struct RenderGraphBufferCreateInfo {
    VkDeviceSize size = 0;
};
struct RenderGraphExternalResourceInfo {
    Entity eResource;
    bool keep = false;
};
// Tag component
struct RenderGraphResource { };

class RenderGraphBuilder;
class RenderGraphResourceBuilder {
private:
    std::unordered_map<std::string, RenderGraphExternalResourceInfo> mExternalResources;
    std::unordered_map<std::string, RenderGraphImageCreateInfo> mImageResources;
    std::unordered_map<std::string, RenderGraphBufferCreateInfo> mBufferResources;
public:
    /// @brief Yup it adds an external resource.
    /// If a resource already exists it gets overridden.
    /// @param eResource Either valid entity containing vk::Image or vk::Buffer or invalid entity.
    /// @param keep Keeps the contents of the resource from a previous frame (whether to set the barrier layout to undefined). Image only
    /// @returns True if the resource was added successfully.
    bool addExternalResource(std::string const &name, RenderGraphExternalResourceInfo info);
    bool addImageResource(std::string const &name, RenderGraphImageCreateInfo info);
    bool addBufferResource(std::string const &name, RenderGraphBufferCreateInfo info);

    bool contains(std::string const &name) const;

    std::unordered_map<std::string, RenderGraphExternalResourceInfo> const &getExternal() const;
    std::unordered_map<std::string, RenderGraphImageCreateInfo> const &getImage() const;
    std::unordered_map<std::string, RenderGraphBufferCreateInfo> const &getBuffer() const;
};

namespace _impl {
    struct RenderGraphImpl;
    bool setupRenderGraph(::egn::RenderGraphBuilder &&, void *, RenderGraphImpl &);
}

class RenderPassBuilder {
private:
    struct ResourceUsage {
        std::string name;
        ResourceTraits traits;
    };
    std::vector<ResourceUsage> mReads;
    std::vector<ResourceUsage> mWrites;

    RenderGraphResourceBuilder *mResourceBuilder = nullptr;

    friend bool ::egn::_impl::setupRenderGraph(RenderGraphBuilder &&, void *, _impl::RenderGraphImpl &);
public:
    RenderPassBuilder(RenderGraphResourceBuilder *resourceBuilder);
    RenderGraphResourceBuilder *resource();
    inline RenderGraphResourceBuilder *operator->() {
        return resource();
    }

    void attachResourceRead(std::string_view resourceName, ResourceTraits traits);
    void attachResourceWrite(std::string_view resourceName, ResourceTraits traits);
};

class IRenderPassStorage {
public:
    virtual ~IRenderPassStorage() = default;
    virtual void setup(RenderPassBuilder &builder, void *data = nullptr) = 0;
    virtual void postCompile(RenderGraphResult const &res, void *data = nullptr) = 0;
    virtual void render(VkCommandBuffer cb, void *data = nullptr) = 0;

    virtual void const *data() const = 0;
    virtual void *data() = 0;

    virtual IRenderPassStorage *clone() = 0;
};
template <typename T> class RenderPassStorage : public IRenderPassStorage {
private:
    T mData;
    std::function<RenderPassSetupCallback_t<T>> mSetupCallback;
    std::function<RenderPassPostCompileCallback_t<T>> mPostCompileCallback;
    std::function<RenderPassRenderCallback_t<T>> mRenderCallback;
public:
    RenderPassStorage() = default;
    inline RenderPassStorage(std::function<RenderPassSetupCallback_t<T>> setupCallback,
        std::function<RenderPassPostCompileCallback_t<T>>
            postCompileCallback,
        std::function<RenderPassRenderCallback_t<T>>
            renderCallback) {
        mSetupCallback = setupCallback;
        mPostCompileCallback = postCompileCallback;
        mRenderCallback = renderCallback;
    }

    inline void setup(RenderPassBuilder &builder, void *data) override {
        mSetupCallback(builder, data);
    }
    inline void postCompile(RenderGraphResult const &res, void *data) override {
        mPostCompileCallback(mData, res, data);
    }
    inline void render(VkCommandBuffer cb, void *data) override {
        mRenderCallback(mData, cb, data);
    }
    inline IRenderPassStorage *clone() override {
        return new RenderPassStorage<T>(*this);
    }
    inline void const *data() const {
        return &mData;
    }
    inline void *data() {
        return &mData;
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

    std::string resource;
    Scope src;
    Scope dst;

    VkImageSubresourceRange subresourceRange;
    VkDeviceSize offset;
    VkDeviceSize size;

    inline VkImageMemoryBarrier2 getImageBarrier(RestrictedEntity<vk::Image> eResource) const {
        auto const &image = eResource.get<vk::Image>();
        return { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src.stages,
            .srcAccessMask = src.access,
            .dstStageMask = dst.stages,
            .dstAccessMask = dst.access,
            .oldLayout = src.layout,
            .newLayout = dst.layout,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .image = image.image,
            .subresourceRange = subresourceRange };
    }
    inline VkBufferMemoryBarrier2 getBufferBarrier(RestrictedEntity<vk::Buffer> eResource) const {
        auto const &buffer = eResource.get<vk::Buffer>();
        return { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = src.stages,
            .srcAccessMask = src.access,
            .dstStageMask = dst.stages,
            .dstAccessMask = dst.access,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .buffer = buffer.buffer,
            .offset = offset,
            .size = size };
    }
};
struct ResourceUsage {
    std::string name;
    ResourceTraits traits;
};
struct RenderPass {
    std::string name;
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
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
    std::unordered_map<std::string, Pass> passes;
    std::optional<vk::AllocationCreateInfo> allocInfo;
    std::optional<vk::QueueFamilies> queueFamilies;
    RenderGraphResourceBuilder mResources;
    Registry *reg = nullptr;
    bool fresh = true;
    friend bool ::egn::_impl::setupRenderGraph(RenderGraphBuilder &&, void * , _impl::RenderGraphImpl &);
public:
    RenderGraphBuilder() = default;
    RenderGraphBuilder(RenderGraphBuilder const &) = delete;
    RenderGraphBuilder &operator=(RenderGraphBuilder const &) = delete;
    RenderGraphBuilder(RenderGraphBuilder &&) = default;
    RenderGraphBuilder &operator=(RenderGraphBuilder &&) = default;
    ~RenderGraphBuilder() = default;

    inline RenderGraphResourceBuilder &resource() {
        return mResources;
    }

    /// @brief Required for allocating stuff (optional)
    /// If not called only RenderPassBuilder::addExternalResource is allowed!
    void setAllocInfo(vk::AllocationCreateInfo const &allocInfo, Registry &reg);

    /// Required for barrier placement (not optional)
    void setQueueFamilies(vk::QueueFamilies const &queueFamilies);

    template <typename Data_t>
    void addPass(std::string const &name, VkQueueFlagBits queue, std::function<RenderPassSetupCallback_t<Data_t>> setup,
        std::function<RenderPassPostCompileCallback_t<Data_t>> post, std::function<RenderPassRenderCallback_t<Data_t>> render);

    void removePass(std::string const &name);
};

struct GraphvizSettings {
    std::vector<std::string> graphAttributes = { "beautify=true", "nodesep=0.5", "ranksep=0.5", "rankdir=TB" };
    std::vector<std::string> nodeAttributes = { };
    std::vector<std::string> edgeAttributes = { "stype=solid", "constraint=true", "arrowhead=normal" };
};

class RenderGraphResult {
private:
    struct RenderGraphResultImpl *mImpl = nullptr;
public:
    RenderGraphResult(RenderGraphResultImpl *data = nullptr); // data could be nullptr if failed
    RenderGraphResult(RenderGraphResult const &) = delete;
    RenderGraphResult &operator=(RenderGraphResult const &) = delete;
    RenderGraphResult(RenderGraphResult &&);
    RenderGraphResult &operator=(RenderGraphResult &&);
    ~RenderGraphResult();

    bool success() const;

    void setResource(std::string const &name, Entity eResource);
    Entity getResource(std::string const &name) const;

    RenderPass const *getPass(std::string const &name) const;

    /// @brief Get the execution order of the passes
    std::vector<std::string> const &getPassStack() const;

    /// @brief Generate a DOT graph.
    /// @param indent If indent is nonnegative, then elements will be pretty-printed with that indent level.
    /// An indent level of 0 will only insert newlines. -1 (the default) selects the most compact representation.
    std::string dumpGraphviz(int indent = -1, GraphvizSettings settings = { }) const;
};

RenderGraphResult buildRenderGraph(RenderGraphBuilder &&builder, void *userData = nullptr);

// ===================================================================================

template <typename Data_t>
inline void RenderGraphBuilder::addPass(std::string const &name, VkQueueFlagBits queue, std::function<RenderPassSetupCallback_t<Data_t>> setup,
    std::function<RenderPassPostCompileCallback_t<Data_t>> post, std::function<RenderPassRenderCallback_t<Data_t>> render) {
    if(passes.contains(std::string(name))) {
        LOG_ERROR("Pass \"{}\" already exists!", name);
        return;
    }

    passes[std::string(name)] = Pass{ .name = std::string(name),
        .queue = queue,
        .storage = std::unique_ptr<IRenderPassStorage>(new RenderPassStorage<Data_t>(setup, post, render)) };
}

} // namespace egn
