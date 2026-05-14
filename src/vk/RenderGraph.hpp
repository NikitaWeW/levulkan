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
    /// Could be vk::Image, vk::Buffer, vk::Swapchain.
    /// The physical resource.
    Entity eResource;
    /// The name of the pass that wrote to the resource
    /// The "version" of a resource
    std::string pass;

    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_NONE;
    VkAccessFlags access = VK_ACCESS_NONE;
};
struct RenderPass
{
    std::string name;
    std::function<void (VkCommandBuffer)> callback;
    std::vector<ResourceDependency> reads;
    std::vector<ResourceDependency> writes;
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
    void removePass(std::string const &name);
    void clear();
    void build();
    void execute(/* TODO */);
private:
    struct ResourceUsage
    {
        std::vector<uint32_t> readInPasses;
        std::vector<uint32_t> writtenInPasses;
    };
    // struct RenderGraphImpl *mImpl = nullptr;
    // 0 - invalid
    uint32_t mNextIndex = 1;
    SparseSet<RenderPass> mPasses;
    SparseSet<ResourceUsage> mResourceUsage;
    std::unordered_map<std::string, uint32_t> mPassNameToIndex;

    std::vector<uint32_t> mPassStack;
    void processPass(uint32_t index);
};

} // namespace vk