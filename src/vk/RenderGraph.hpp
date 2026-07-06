/*
$$\    $$\ $$\   $$\   Vulkan helper functionality.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   
    \_/    \__|  \__|  Vulkan render graph.
*/
#pragma once
#include "Init.hpp"
#include "Resource.hpp"
#include "Shader.hpp"
#include "Pipeline.hpp"
#include "ECS.hpp" // SparseSet
#include <functional>

namespace vk {

struct ResourceTraits
{
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
struct RenderPass
{
    struct ResourceDependency
    {
        /// Could be vk::Image, vk::Buffer.
        /// The physical resource.
        RestrictedEntity<std::logical_or<>, vk::Image, vk::Buffer> eResource;
        /// The name of the pass that wrote to the resource.
        /// The "version" of a resource, e.g. this pass reads resource x from pass y.
        /// Could be used for resource aliasing.
        /// Leave empty to read from previous frame.
        std::string pass;

        ResourceTraits traits;

        inline uint32_t id() const { return eResource.id(); }
    };
    struct ResourceWrite
    {
        /// Could be vk::Image, vk::Buffer, vk::Swapchain.
        /// The physical resource.
        RestrictedEntity<std::logical_or<>, vk::Image, vk::Buffer, vk::Swapchain> eResource;
        ResourceTraits traits;

        inline uint32_t id() const { return eResource.id(); }
    };

    std::string                           name;
    std::vector<ResourceDependency>       reads;
    std::vector<ResourceWrite>            writes;
    VkQueueFlagBits                       queue = VK_QUEUE_GRAPHICS_BIT;
    std::function<void (vk::RenderPass const &, VkCommandBuffer)> callback;
    vk::Shader                            shader;
    vk::Pipeline                          pipeline;
};
struct Barrier
{
    struct Scope
    {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkBufferUsageFlags2 bufferUsage = 0;
        VkImageUsageFlags imageUsage = 0;

        uint32_t queueIndex = VK_QUEUE_FAMILY_IGNORED;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    };

    RestrictedEntity<std::logical_or<>, vk::Image, vk::Buffer> eResource;
    Scope src;
    Scope dst;
    
    VkImageSubresourceRange subresourceRange;
    VkDeviceSize offset;
    VkDeviceSize size;

    inline VkImageMemoryBarrier2 getImageBarrier() const {
        assert(eResource.has<vk::Image>());
        auto const &image = eResource.get<vk::Image>();
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src.access,
            .dstAccessMask = dst.access,
            .oldLayout = src.layout,
            .newLayout = dst.layout,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .image = image.image,
            .subresourceRange = subresourceRange
        };
    }
    inline VkBufferMemoryBarrier2 getBufferBarrier() const {
        assert(eResource.has<vk::Buffer>());
        auto const &buffer = eResource.get<vk::Buffer>();
        return {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = src.access,
            .dstAccessMask = dst.access,
            .srcQueueFamilyIndex = src.queueIndex,
            .dstQueueFamilyIndex = dst.queueIndex,
            .buffer = buffer.buffer,
            .offset = offset,
            .size = size
        };
    }
};

struct GraphvizSettings
{
    bool implicitDependencies = true; ///< If set to true, dashed arrows will point to implicit pass dependencies (read before next write).
    bool showHistory = true; ///< If set to true, dotted arrows will point from the last pass that wrote to the resource to the pass that reads the history.
    std::vector<std::string> graphAttributes = {"beautify=true", "nodesep=0.5", "ranksep=0.5", "rankdir=TB"};
    std::vector<std::string> nodeAttributes = {};
    std::vector<std::string> explicitEdgeAttributes = {"stype=solid",  "constraint=true",  "arrowhead=normal", "weight=2"};
    std::vector<std::string> implicitEdgeAttributes = {"style=dotted", "constraint=false", "arrowhead=empty",  "weight=1"};
    std::vector<std::string> historyEdgeAttributes  = {"stype=dotted", "constraint=false", "arrowhead=empty",  "weight=1"};
};
class RenderGraph
{
public:
    struct RenderGraphCreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        QueueFamilies queueFamilies;
    };
    struct Result
    {
        bool valid = false;
        SparseSet<RenderPass> passes;
        std::unordered_map<std::string, uint32_t> passNameToIndex;
        SparseSet<std::vector<Barrier>> barriers;
        std::vector<uint32_t> passStack; // The order the passes are executed in that obeys the dependencies
    };

    explicit RenderGraph();
    explicit RenderGraph(RenderGraphCreateInfo const &createInfo);
    RenderGraph(RenderGraph &&);
    RenderGraph &operator=(RenderGraph &&);
    ~RenderGraph();
    
    RenderGraph(RenderGraph const &) = delete;
    RenderGraph &operator=(RenderGraph const &) = delete;

    void addPass(RenderPass const &pass);
    /// @returns A pointer to the pass with the @p name, nullptr if no such pass exists
    RenderPass const *findPass(std::string const &name) const;
    /// @copydoc findPass 
    RenderPass *findPass(std::string const &name);
    void removePass(std::string const &name);

    void clear();

    /// @brief Build a render graph
    /// Please note that if there is a complicated dependency loop the algorithm will crash and burn horribly and destroy half of the world population in a terrible accident
    bool build();

    /// @brief Generate a DOT graph.
    /// @param indent If indent is nonnegative, then array elements and object members will be pretty-printed with that indent level. An indent level of 0 will only insert newlines. -1 (the default) selects the most compact representation.
    std::string dumpGraphviz(int indent = -1, GraphvizSettings settings = {}) const;

    Result getResult() const;
    bool isUpToDate() const;
private:
    struct ResourceUsage
    {
        struct Dependency
        {
            uint32_t pass = 0;
            uint32_t version = 0;
        };
        std::vector<Dependency> readInPasses; // 0 - history
        std::vector<uint32_t> writtenInPasses;

        /// If version of A is less than version of B, then A writes to the pass before B.
        /// Versions increase monotonically.
        struct {
            std::unordered_map<uint32_t, uint32_t> passToVersion; 
            std::unordered_map<uint32_t, uint32_t> versionToPass; 
            
            uint32_t nextVersion = 1;
            uint32_t lastPassWrite = 0; // Intermediate
            
            inline uint32_t getPrevPass(uint32_t pass) const { 
                auto version = passToVersion.at(pass);
                if(pass <= 1)
                    return 0; // First pass to write
                return versionToPass.at(version - 1); 
            }
            inline uint32_t getLastVersion() const {
                return nextVersion - 1;
            }
        } versions;
    };
    enum class NodeState { None = 0, Visited, Added };

    // 0 - invalid
    uint32_t mNextIndex = 1;
    std::unordered_map<Entity, ResourceUsage> mResourceUsage;
    std::unordered_map<uint32_t, NodeState> mNodeState;

    bool mUpToDate = false;
    bool mFailed = false;

    VkDevice mDevice = VK_NULL_HANDLE;
    SparseSet<VkCommandBuffer> mCommandBuffers;
    QueueFamilies mQueueFamilies;

    Result mResult;
    
    void validate(uint32_t index);
    void processPass(uint32_t index, bool backtrack = false); ///< Recursively process a pass and construct an #mPassStack that contains a valid pass order that obeys the strict dependencies
    void buildBarriers();
    void record();
};

} // namespace vk