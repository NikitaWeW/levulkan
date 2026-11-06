#include "engine/renderer/RenderGraphRenderer.hpp"

void egn::RenderGraphRenderer::build(RenderGraphBuilder &&builder) {
    mRenderGraph = buildRenderGraph(std::move(builder), mData);
    if(!mRenderGraph.success()) {
        return;
    }
    if(mRenderGraph.getResource("target")) {
        LOG_WARN("Render graph resource name \"target\" is reserved by RenderGraphRenderer!");
    }
}

void egn::RenderGraphRenderer::preFrame(RestrictedEntity<vk::Image> target, uint) {
    mRenderGraph.setResource("target", target);
    for(auto const &passName : mRenderGraph.getPassStack()) {
        auto const &pass = mRenderGraph.getPass(passName);
        pass->storage->postCompile(mRenderGraph, mData);
    }
}
void egn::RenderGraphRenderer::render(CommandBuffers &commandBuffers) {
    for(auto passIndex : mRenderGraph.getPassStack()) {
        auto const &pass = mRenderGraph.getPass(passIndex);
        assert(pass);

        struct Barriers {
            std::vector<VkImageMemoryBarrier2> imageBarriers;
            std::vector<VkBufferMemoryBarrier2> bufferBarriers;
            inline void emplace(egn::Barrier const &barrier, egn::RenderGraphResult const &rg) {
                auto eResource = rg.getResource(barrier.resource);
                if(eResource.contains<egn::vk::Image>())
                    imageBarriers.emplace_back(barrier.getImageBarrier(eResource));
                if(eResource.contains<egn::vk::Buffer>())
                    bufferBarriers.emplace_back(barrier.getBufferBarrier(eResource));
            }
        };
        // For each queue for release/acquire operations
        egn::SparseSet<Barriers> queueBarriers;
        for(auto barrier : pass->barriers) {
            if(barrier.src.queueIndex == VK_QUEUE_FAMILY_IGNORED)
                barrier.src.queueIndex = barrier.dst.queueIndex;

            if(barrier.src.queueIndex != barrier.dst.queueIndex)
                queueBarriers[barrier.src.queueIndex].emplace(barrier, mRenderGraph);

            queueBarriers[barrier.dst.queueIndex].emplace(barrier, mRenderGraph);
        }

        auto passQueue = commandBuffers.getQueue(pass->queue);
        auto passCB = commandBuffers.getCommandBuffer(passQueue);

        for(auto [queue, barrierss] : queueBarriers) {
            VkDependencyInfo dependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = static_cast<uint32_t>(barrierss.bufferBarriers.size()),
                .pBufferMemoryBarriers = barrierss.bufferBarriers.data(),
                .imageMemoryBarrierCount = static_cast<uint32_t>(barrierss.imageBarriers.size()),
                .pImageMemoryBarriers = barrierss.imageBarriers.data() };
            vkCmdPipelineBarrier2(passCB, &dependencyInfo);
        }

        pass->render(passCB);
    }
}
