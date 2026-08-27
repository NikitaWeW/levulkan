#include "Renderer.hpp"

// TODO: RenderGraphResult

struct PleaseKeepTheImageContents {};
void RenderPassBuilder::addExternalResource(std::string_view name, RestrictedAnyEntity<vk::Image, vk::Buffer> eResource, bool keep) {
    mExternalResources[std::string(name)] = eResource;
    if(eResource.has<vk::Image>()) {
        if(keep && !eResource.has<PleaseKeepTheImageContents>()) {
            eResource.emplace<PleaseKeepTheImageContents>();
        } else if(!keep && eResource.has<PleaseKeepTheImageContents>()) {
            eResource.remove<PleaseKeepTheImageContents>();
        }
    }
}
void RenderPassBuilder::addImageResource(std::string_view name, vk::ImageCreateInfo::ImageInfo info) {
    mImageResources[std::string(name)] = info;
}
void RenderPassBuilder::addBufferResource(std::string_view name, uint32_t size) {
    mBufferResources[std::string(name)] = size;
}
void RenderPassBuilder::attachResourceRead(std::string_view resourceName, ResourceTraits traits) {
    mReads.emplace_back(std::string(resourceName), traits);
}
void RenderPassBuilder::attachResourceWrite(std::string_view resourceName, ResourceTraits traits) {
    mWrites.emplace_back(std::string(resourceName), traits);
}

struct RenderGraphResultImpl {

};
enum class NodeState { None = 0, Visited, Added };

struct Pass {
    uint id = 0;
    std::string name;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    VkQueueFlagBits queue;
    std::unique_ptr<IRenderPassStorage> storage;
};
struct ResourceInfo {
    Entity eResource;
    std::vector<uint> read;
    /*std::vector<>*/
    uint written;
};
// To pass intermediate data between functions
struct RenderGraph {
    std::unordered_set<Entity> resourcePool;
    std::unordered_map<uint, NodeState> nodeState;
    std::unordered_map<uint, Pass> passes;
    std::unordered_map<uint, ResourceInfo> resources;
    std::unordered_map<std::string, uint> passToIndex;
    std::unordered_map<std::string, uint> resourceToIndex;
    uint nextId = 1;
};

static void processPass(uint32_t passIndex, bool backtrack, RenderGraph &renderGraph) {
    assert(passIndex != 0);
    
    auto const &pass = renderGraph.passes.at(passIndex);

    if(renderGraph.nodeState[passIndex] != NodeState::None)
        return;

    // Process all implicit dependencies:
    // passes that read from the old versions of resources the current pass writes to
    // Never happens with the new implicit resource aliasing.
    // for(auto const &resource : pass.writes)
    // {
    //     auto &usage = mResourceUsage.get(mResourceNameToIndex.at(resource.resource));
    //     for(auto const &[dependency, version] : usage.readInPasses)
    //         if(dependency != passIndex && (version == usage.versions.lastPassWrite || version == 0))
    //         {
    //             processPass(dependency, true);
    //             RENDER_GRAPH_TRACE("Back to processing pass {}, backtrack {}", pass.name, backtrack);
    //         }
    // }

    for(auto const &resource : pass.reads)
    {
        if(!resource.pass.empty())
        {
            auto const &dependency = mPassNameToIndex.at(resource.pass);
            processPass(dependency, true);
            RENDER_GRAPH_TRACE("Back to processing pass {}, backtrack {}", pass.name, backtrack);
        }
    }

    if(mNodeState[passIndex] != NodeState::Added)
    {
        mNodeState[passIndex] = NodeState::Added;
        RENDER_GRAPH_TRACE(" Adding pass {}", pass.name);
        mPassStack.emplace_back(passIndex);

        RENDER_GRAPH_TRACE("  Adding pass {} to resource versions.", pass.name);
        for(auto const &resource : pass.writes)
        {
            auto &usage = mResourceUsage.get(mResourceNameToIndex.at(resource.resource));
            // For future use
            auto version = usage.versions.nextVersion++;
            RENDER_GRAPH_TRACE(": Adding pass {} to versions of resource {}, version {}, last write {}", pass.name, resource.resource, version, usage.versions.lastPassWrite == 0 ? "none" : mPasses.get(usage.versions.lastPassWrite).name);
            usage.versions.passToVersion[passIndex] = version; 
            usage.versions.versionToPass[version] = passIndex;
            usage.versions.lastPassWrite = passIndex;
        }

        if(backtrack)
            RENDER_GRAPH_TRACE(" Backtracking...");
        if(backtrack)
            return;
    }

    // Process all dependents
    RENDER_GRAPH_TRACE("  Process all dependents on the pass {}", pass.name);
    for(auto const &resource : pass.writes)
    {
        auto &usage = mResourceUsage.get(mResourceNameToIndex.at(resource.resource));
        for(auto const &[dependent, version] : usage.readInPasses)
            if(version == passIndex)
                processPass(dependent, backtrack);
    }
}

RenderGraphResult buildRenderGraph(RenderGraphBuilder &&builder) {
    RenderGraph renderGraph;
    for(auto &&[name, pass] : builder.mPasses) {
        if(renderGraph.passToIndex.contains(name)) {
            LOG_ERROR("Render graph already contains pass \"{}\"", name);
            return RenderGraphResult(nullptr);
        }

        auto index = renderGraph.passToIndex[name] = renderGraph.nextId++;
        auto &renderGraphPass = renderGraph.passes[index] = {
            .id = index,
            .name = std::move(pass.name),
            .queue = std::move(pass.queue),
            .storage = std::move(pass.storage),
        };

        RenderPassBuilder builder;
        renderGraphPass.storage->setup(builder);
        for(auto const &[name, eResource] : builder.mExternalResources) {
            if(renderGraph.resourceToIndex.contains(name)) {
                // Here
                // TODO: finish the rest of the fucking owl
                LOG_ERROR(); 
            }
        }
    }
}