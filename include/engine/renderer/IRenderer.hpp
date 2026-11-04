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
    virtual void preFrame(Registry &reg, uint frameIndex) { };

    /// Renders the registry of the #target entity the #target image.
    virtual void render(RestrictedEntity<vk::Image> target, CommandBuffers &commandBuffers) = 0;
};

}
