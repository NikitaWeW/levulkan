#pragma once
#include "engine/renderer/IRenderer.hpp"
#include "engine/renderer/RenderGraph.hpp"

namespace egn {

/// Renders the provided render graph
/// Render graph resource "target" is set to the render target and may not be defined.
class RenderGraphRenderer : public IRenderer {
private:
    RenderGraphResult mRenderGraph;
public:
    RenderGraphRenderer(RenderGraphBuilder &&builder);
    void render(RestrictedEntity<vk::Image> target, CommandBuffers &commandBuffers) override;

    inline void setResource(std::string_view name, Entity eResource) {
        mRenderGraph.setResource(name, eResource);
    }
    inline RenderGraphResult const &getRenderGraph() const {
        return mRenderGraph;
    }
    inline bool initSuccess() const {
        return getRenderGraph().success();
    }
};

}
