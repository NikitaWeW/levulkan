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
#include "Resource.hpp"
#include "ECS.hpp" // SparseSet
#include <unordered_set>
#include <limits>
#include <functional>

namespace vk {

struct RenderPass
{
    struct ResourceDependency
    {
        /// Could be vk::Image, vk::Buffer.
        /// The physical resource.
        Entity eResource;
        /// The name of the pass that wrote to the resource.
        /// The "version" of a resource, e.g. this pass reads resource x from pass y.
        /// Could be used for resource aliasing.
        /// Leave empty to read from previous frame.
        std::string pass;

        VkImageUsageFlags usage;
        VkAccessFlags access;

        inline uint32_t id() const { return eResource.id(); }
    };
    struct ResourceWrite
    {
        /// Could be vk::Image, vk::Buffer, vk::Swapchain.
        /// The physical resource.
        Entity eResource;

        inline uint32_t id() const { return eResource.id(); }
    };

    std::string name;
    std::function<void (VkCommandBuffer)> callback;
    std::vector<ResourceDependency> reads;
    std::vector<ResourceWrite> writes;
    VkQueueFlagBits queue = VK_QUEUE_GRAPHICS_BIT;
};
struct RenderGraphGraphvizSettings
{
    bool implicitDependencies = true;
    bool showHistory = true;
    std::vector<std::string> graphAttributes = {"beautify=true", "nodesep=0.5", "ranksep=0.5", "rankdir=TB"};
    std::vector<std::string> nodeAttributes = {};
    std::vector<std::string> implicitEdgeAttributes = {"style=dotted", "constraint=false", "arrowhead=empty", "weight=1"};
    std::vector<std::string> explicitEdgeAttributes = {"stype=solid", "constraint=true", "arrowhead=normal", "weight=2"};
    std::vector<std::string> historyEdgeAttributes = {"stype=dotted", "constraint=false", "arrowhead=empty", "weight=1"};
};
// Good resource: https://www.gdcvault.com/play/1024045/FrameGraph-Extensible-Rendering-Architecture-in
class RenderGraph
{
public:
    struct RenderGraphCreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::unordered_map<VkQueueFlagBits, VkQueue> queues;
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
    /// Please note that if there is a complicated dependency loop the algorithm will crash and burn horribly and destroy half of the population in a terrible accident
    bool build();
    void execute(/* TODO */);
    /// @brief Generate a DOT graph.
    /// @param indent If indent is nonnegative, then array elements and object members will be pretty-printed with that indent level. An indent level of 0 will only insert newlines. -1 (the default) selects the most compact representation.
    /// @param implicitDependencies If set to true, dashed arrows will point to implicit pass dependencies (read before next write).
    /// @param showHistory If set to true, dotted arrows will point from the last pass that wrote to the resource to the pass that reads the history
    /// TODO: move these flags to struct
    std::string dumpGraphviz(int indent = -1, RenderGraphGraphvizSettings settings = {}) const;
private:
    struct ResourceUsage
    {
        struct Usage
        {
            uint32_t pass = 0;
            uint32_t version = 0;
        };
        std::vector<Usage> readInPasses; // 0 - history
        std::vector<uint32_t> writtenInPasses;

        /// If version of A is less than version of B, then A writes to the pass before B.
        /// Versions increase monotonically.
        struct {
            std::unordered_map<uint32_t, uint32_t> passToVersion; 
            std::unordered_map<uint32_t, uint32_t> versionToPass; 
            
            uint32_t nextVersion = 1; // Intermediate
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
    struct Barrier
    {
        struct Scope
        {
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkAccessFlags access = VK_ACCESS_NONE;
            VkPipelineStageFlags stages = VK_PIPELINE_STAGE_NONE;
        };

        Scope src;
        Scope dst;
        Entity eResource;
    };
    struct Queue
    {
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    };

    // 0 - invalid
    uint32_t mNextIndex = 1;
    SparseSet<RenderPass> mPasses;
    std::unordered_map<std::string, uint32_t> mPassNameToIndex;
    std::unordered_map<Entity, ResourceUsage> mResourceUsage;
    std::unordered_map<uint32_t, NodeState> mNodeState;
    
    SparseSet<Barrier> mBarriers;
    std::vector<uint32_t> mPassStack; // The order the passes are executed in that obeys the dependencies
    bool mUpToDate = false;
    bool mValidationFailed = false;

    VkDevice mDevice = VK_NULL_HANDLE;
    VkSemaphore mTimelineSemaphore = VK_NULL_HANDLE;
    SparseSet<Queue> mQueues;
    
    void validate(uint32_t index);
    void processPass(uint32_t index, bool backtrack = false); ///< Recursively process a pass and construct an #mPassStack that contains a valid pass order that obeys the strict dependencies
    void buildBarriers();
    void record();
};

} // namespace vk