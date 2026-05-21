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

struct ResourceDependency
{
    /// Could be vk::Image, vk::Buffer.
    /// The physical resource.
    Entity eResource;
    /// The name of the pass that wrote to the resource.
    /// The "version" of a resource, e.g. this pass reads resource x from pass y.
    std::string pass;
};
struct ResourceWrite
{
    /// Could be vk::Image, vk::Buffer, vk::Swapchain.
    /// The physical resource.
    Entity eResource;
};
struct RenderPass
{
    std::string name;
    std::function<void (VkCommandBuffer)> callback;
    std::vector<ResourceDependency> reads;
    std::vector<ResourceWrite> writes;
};
class RenderGraph
{
public:
    explicit RenderGraph();
    RenderGraph(RenderGraph const &);
    RenderGraph &operator=(RenderGraph const &);
    ~RenderGraph();

    RenderGraph(RenderGraph &&) = delete;
    RenderGraph &operator=(RenderGraph &&) = delete;

    void addPass(RenderPass const &pass);
    /// @returns A pointer to the pass with the @p name, nullptr if no such pass exists
    RenderPass const *findPass(std::string const &name) const;
    /// @copydoc findPass 
    RenderPass *findPass(std::string const &name);
    void removePass(std::string const &name);
    void clear();
    void build();
    void execute(/* TODO */);
    /// @brief Generate a DOT graph.
    /// @param indent If indent is nonnegative, then array elements and object members will be pretty-printed with that indent level. An indent level of 0 will only insert newlines. -1 (the default) selects the most compact representation.
    // TODO: https://en.wikipedia.org/wiki/DOT_(graph_description_language)
    std::string dump(int indent = -1) const;
private:
    struct ResourceUsage
    {
        struct Usage
        {
            uint32_t pass = 0;
            uint32_t version = 0;
        };
        std::vector<Usage> readInPasses;
        std::vector<uint32_t> writtenInPasses;
        uint32_t lastPassWrite = 0; // Intermediate. The last pass that wrote to resource.
    };
    enum class NodeState { None = 0, Visited, Added };
    // 0 - invalid
    uint32_t mNextIndex = 1;
    SparseSet<RenderPass> mPasses;
    SparseSet<ResourceUsage> mResourceUsage;
    std::unordered_map<uint32_t, NodeState> mNodeState;
    std::unordered_map<std::string, uint32_t> mPassNameToIndex;

    std::vector<uint32_t> mPassStack;
    void processPass(uint32_t index, bool backtrack = false);
    void processUsage(uint32_t passIndex);
};

} // namespace vk