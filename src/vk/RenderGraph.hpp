/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
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
#include <unordered_set>
#include <limits>

namespace vk {

struct RenderResource
{
    static constexpr uint32_t Unused = std::numeric_limits<uint32_t>::max();
    std::string name = "";
    uint32_t index = Unused;
    uint32_t physicalIndex = Unused;
    VkQueueFlags usedQueues = 0;
	std::unordered_set<uint32_t> writtenInPasses;
	std::unordered_set<uint32_t> readInPasses;
};
struct ImageRenderResource : public RenderResource, public Image {};
struct BufferRenderResource : public RenderResource, public Buffer {};

struct AttachmentInfo
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    ImageCreateInfo::Dimensions dimensions;
    VkImageUsageFlags usage = 0;
};
struct RenderPass
{
    std::string mName = "";
	std::vector<std::string> mColorInputs;
	std::vector<std::string> mColorOutputs;
	std::vector<std::string> mBufferInputs;
	std::vector<std::string> mBufferOutputs;
};
class RenderGraph
{
private:
public:
    RenderGraph() = default;
    ~RenderGraph();
    RenderGraph(RenderGraph &&) = default;
    RenderGraph &operator=(RenderGraph &&) = default;
    RenderGraph(RenderGraph const &) = delete;
    RenderGraph &operator=(RenderGraph const &) = delete;
    
    /// @brief Construct a valid render graph.
    // RenderGraph(InitResult);
};

} // namespace vk