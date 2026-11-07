#pragma once
#include "engine/dsa/ECS.hpp"
#include "engine/vk/Resource.hpp"
#include <unordered_set>

namespace egn {

/// Queue index to command buffer
struct CommandBuffers {
    std::unordered_map<uint32_t, VkCommandBuffer> commandBuffers;
    std::unordered_set<uint32_t> usedCommandBuffers;
    std::unordered_map<VkQueueFlagBits, uint32_t> queueIndices;

    inline VkCommandBuffer getCommandBuffer(uint32_t queueIndex) {
        assert(commandBuffers.contains(queueIndex));
        usedCommandBuffers.emplace(queueIndex);
        return commandBuffers.at(queueIndex);
    }
    inline uint32_t getQueue(VkQueueFlagBits queue) {
        assert(queueIndices.contains(queue));
        return queueIndices.at(queue);
    }
};

/// Who wanna rewrite the entire renderer?
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Prepares for the frame (update resources, etc.)
    virtual void preFrame([[maybe_unused]] RestrictedEntity<vk::Image> target, [[maybe_unused]] uint frameIndex) = 0;

    /// Renders the target given by preFrame.
    virtual void render(CommandBuffers &commandBuffers) = 0;
};

}
