#include "RenderGraph.hpp"
#include "Utility.hpp"
#include "Init.hpp"
#include "Resource.hpp"
#include "Logging.hpp"
#include <algorithm>
using namespace vk;

#define RENDER_GRAPH_TRACE LOG_TRACE
#ifndef RENDER_GRAPH_TRACE
#define RENDER_GRAPH_TRACE(...) void()
#endif

RenderGraph::RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraphCreateInfo const &createInfo) {
    mQueueFamilies = createInfo.queueFamilies;
}
RenderGraph::RenderGraph(RenderGraph &&) = default;
RenderGraph &RenderGraph::operator=(RenderGraph &&) = default;
RenderGraph::~RenderGraph() = default;
void RenderGraph::addPass(RenderPass const &pass) {
    if(mPassNameToIndex.contains(pass.name))
    {
        LOG_ERROR("Render graph already contains \"{}\" pass", pass.name);
        return;
    }

    auto index = (mPassNameToIndex[pass.name] = mNextIndex++);
    mPasses.emplace(index, pass);
    mUpToDate = false;
}
void RenderGraph::removePass(std::string const &name) {
    if(!mPassNameToIndex.contains(name))
    {
        LOG_ERROR("Render graph does not contain \"{}\" pass", name);
        return;
    }

    mPasses.erase(mPassNameToIndex.at(name));
    mPassNameToIndex.erase(name);
    mUpToDate = false;
}
RenderPass const *RenderGraph::findPass(std::string const &name) const {
    if(!mPassNameToIndex.contains(name))
        return nullptr;

    return &mPasses.get(mPassNameToIndex.at(name));
}
RenderPass *RenderGraph::findPass(std::string const &name) {
    if(!mPassNameToIndex.contains(name))
        return nullptr;

    mUpToDate = false;
    return &mPasses.get(mPassNameToIndex.at(name));
}
void RenderGraph::clear() {
    mPasses.clear();
    mPassNameToIndex.clear();
    mResources.clear();
    mResourceNameToIndex.clear();
}

void RenderGraph::setResource(std::string const &name, RestrictedEntity_t<std::logical_or<>, vk::Image, vk::Buffer> resource) {
    auto index = mResourceNameToIndex.contains(name) ? mResourceNameToIndex.at(name) : (mResourceNameToIndex[name] = mNextIndex++);
    mResources[index] = resource;
}
Entity RenderGraph::findResource(std::string const &name) const {
    if(!mResourceNameToIndex.contains(name))
        return Entity();

    return mResources.get(mResourceNameToIndex.at(name));
}
void RenderGraph::removeResource(std::string const &name) {
    if(!mResourceNameToIndex.contains(name))
    {
        LOG_ERROR("Render graph does not contain \"{}\" resource", name);
        return;
    }

    mResources.erase(mResourceNameToIndex.at(name));
    mResourceNameToIndex.erase(name);
}

bool RenderGraph::isUpToDate() const { return mUpToDate; }
SparseSet<RenderPass> const &RenderGraph::getPasses() const { return mPasses; }
std::ranges::subrange<RenderPass *> RenderGraph::getPassesRange() { return mPasses.range(); }
SparseSet<Entity> const &RenderGraph::getResources() const { return mResources; }
std::ranges::subrange<Entity *> RenderGraph::getResourcesRange() { return mResources.range(); }
SparseSet<std::vector<Barrier>> const &RenderGraph::getBarriers() const { return mBarriers; }
std::vector<uint32_t> const &RenderGraph::getPassStack() const { return mPassStack; }

void RenderGraph::validate(uint32_t passIndex) {
    #define VALIDATION_ASSERT(x) if(!static_cast<bool>(x)) { LOG_ERROR("{}:{} Render graph validation assertion failed: {}", __FILE__, __LINE__, #x); mFailed = true; return; }

    VALIDATION_ASSERT(passIndex != 0);
    auto const &pass = mPasses[passIndex];
    
    VALIDATION_ASSERT(!pass.name.empty());
    VALIDATION_ASSERT(pass.callback);
    for(auto const &dependency : pass.reads)
    {
        VALIDATION_ASSERT(dependency.pass.empty() || mPassNameToIndex.contains(dependency.pass));
        VALIDATION_ASSERT(dependency.traits.valid());
        
        auto resourceIndex = mResourceNameToIndex.at(dependency.resource);
        VALIDATION_ASSERT(mResourceNameToIndex.contains(dependency.resource));
        if(mResources.contains(resourceIndex))
        {
            auto const &resource = mResources.get(resourceIndex);
            VALIDATION_ASSERT(resource.valid());
            if(resource.contains<vk::Image>())
                VALIDATION_ASSERT(dependency.traits.imageTraits.valid());
            if(resource.contains<vk::Buffer>())
                VALIDATION_ASSERT(dependency.traits.bufferTraits.valid());
        }
    }
    for(auto const &dependent : pass.writes)
    {
        VALIDATION_ASSERT(dependent.traits.valid());

        auto resourceIndex = mResourceNameToIndex.at(dependent.resource);
        VALIDATION_ASSERT(mResourceNameToIndex.contains(dependent.resource));
        if(mResources.contains(resourceIndex))
        {
            auto const &resource = mResources.get(resourceIndex);
            VALIDATION_ASSERT(resource.valid());
            if(resource.contains<vk::Image>())
                VALIDATION_ASSERT(dependent.traits.imageTraits.valid());
            if(resource.contains<vk::Buffer>())
                VALIDATION_ASSERT(dependent.traits.bufferTraits.valid());
        }
    }

    // TODO: More sophisticated cycle detection.
    VALIDATION_ASSERT(mNodeState[passIndex] != NodeState::Added && "Cycle detected!");

    for(auto const &resource : pass.writes)
    {
        auto const &usage = mResourceUsage.at(mResourceNameToIndex.at(resource.resource));
        for(auto const &[pass, version] : usage.readInPasses)
            if(version == passIndex)
                validate(pass);
    }

    #undef VALIDATION_ASSERT
}
static std::string collapseAttributes(std::vector<std::string> const &attributes) {
    std::string res;
    for(auto const &attrib : attributes)
        res.append("[").append(attrib).append("]");
    return res;
}
std::string RenderGraph::dumpGraphviz(int indent, GraphvizSettings settings) const {
    assert(mUpToDate && "you need to build the render graph first!");
    std::stringstream ss;
    auto newline = [indent](int i){ return (indent >= 0) ? ("\n" + std::string(i, ' ')) : " "; };
    
    ss << "digraph RenderGraph {";
    for(auto const &attrib : settings.graphAttributes)
        ss << newline(indent) << attrib << ";";

    std::string nodeAttributes = collapseAttributes(settings.nodeAttributes);
    std::string implicitEdgeAttributes = collapseAttributes(settings.implicitEdgeAttributes);
    std::string explicitEdgeAttributes = collapseAttributes(settings.explicitEdgeAttributes);
    std::string historyEdgeAttributes = collapseAttributes(settings.historyEdgeAttributes);

    for(auto index : mPassStack)
    {
        auto const &pass = mPasses.get(index);
        auto name = pass.name;
        name.erase(std::remove(name.begin(), name.end(), '\"'), name.end());

        ss << newline(indent) << "\"" << name << "\" " << nodeAttributes << ";";
        
        // Implicit dependencies
        if(settings.implicitDependencies)
        {
            for(auto const &resource : pass.writes)
            {  
                auto resourceIndex = mResourceNameToIndex.at(resource.resource);
                auto &usage = mResourceUsage.get(resourceIndex);
                auto currentVersion = usage.versions.passToVersion.at(index);
                for(auto const &[dependency, passVersion] : usage.readInPasses) {
                    if(dependency == index)
                        continue;
                    auto version = passVersion ? usage.versions.passToVersion.at(passVersion) : 1;
                    if(currentVersion - version == 1)
                        ss << newline(indent) << fmt::format("\"{}\" -> \"{}\" [label=\"{} ({})\"]{}", mPasses.get(dependency).name, name, resource.resource, mResources.get(resourceIndex).id(), implicitEdgeAttributes);
                }
            }
        }
        
        // Dependencies
        for(auto const &resource : pass.reads)
        {
            bool history = resource.pass.empty();
            if(history && !settings.showHistory)
                continue;
            
            auto resourceIndex = mResourceNameToIndex.at(resource.resource);
            auto &usage = mResourceUsage.get(resourceIndex);
            auto const &version = history ? usage.versions.getLastVersion() : mPassNameToIndex.at(resource.pass);
            ss << newline(indent) << fmt::format("\"{}\" -> \"{}\" [label=\"{} ({})\"]{}", mPasses.get(version).name, name, resource.resource, mResources.get(resourceIndex).id(), history ? historyEdgeAttributes : explicitEdgeAttributes);
        }
    }

    std::string title;
    for(auto index : mPassStack)
        title.append(mPasses.get(index).name).append(" -> ");
    title.erase(title.size() - std::string_view(" -> ").size());
    ss << newline(indent) << newline(indent) << "label = \"" << title << "\";";

    ss << newline(0) << "}";

    return ss.str();
}
static std::string printDependencies(RenderPass const &pass) {
    std::string reads;
    for(auto dependency : pass.reads)
        reads.append(fmt::format("{}/{}; ", dependency.pass, dependency.resource));
    if(reads.empty())
        reads = "none";
    std::string writes;
    for(auto dependent : pass.writes)
        writes.append(fmt::format("{}; ", dependent.resource));
    if(writes.empty())
        writes = "none";

    return fmt::format("reads {:>40} | writes {:>40}", reads, writes);
}

// There gotta be a simpler way
void RenderGraph::processPass(uint32_t passIndex, bool backtrack) {
    assert(passIndex != 0);
    
    auto const &pass = mPasses[passIndex];
    RENDER_GRAPH_TRACE("Processing pass {}, backtrack {},     {}", pass.name, backtrack, printDependencies(pass));

    if(mNodeState[passIndex] != NodeState::None)
        return;

    // Process all implicit dependencies
    // passes that read from the old versions of resources the current pass writes to
    RENDER_GRAPH_TRACE("  Processing all implicit dependencies of pass {}", pass.name);
    for(auto const &resource : pass.writes)
    {
        auto &usage = mResourceUsage.get(mResourceNameToIndex.at(resource.resource));
        for(auto const &[dependency, version] : usage.readInPasses)
            if(dependency != passIndex && (version == usage.versions.lastPassWrite || version == 0))
            {
                processPass(dependency, true);
                RENDER_GRAPH_TRACE("Back to processing pass {}, backtrack {}", pass.name, backtrack);
            }
    }

    RENDER_GRAPH_TRACE("  Processing all dependencies of pass {}", pass.name);
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

void RenderGraph::buildBarriers() {
    std::unordered_map<uint32_t, Barrier::Scope> resourceState;
    for(auto passIndex : mPassStack)
    {
        auto const &pass = mPasses.get(passIndex);
        uint32_t queue = VK_QUEUE_FAMILY_IGNORED;
        if(!mQueueFamilies.indices.contains(pass.queue)) {
            LOG_ERROR("Pass {} needs queue {} which is not in queue families provided to the frame graph!", pass.name, string_VkQueueFlagBits(pass.queue));
            mFailed = true;
            return;
        } else {
            queue = mQueueFamilies.indices.at(pass.queue);
        }


        for(auto const &dependency : pass.reads) {
            bool history = dependency.pass.empty();

            auto resourceIndex = mResourceNameToIndex.at(dependency.resource);
            auto const &usage = mResourceUsage.get(resourceIndex);
            auto version = history ? usage.versions.getLastVersion() : mPassNameToIndex.at(dependency.pass);
            auto &state = resourceState[resourceIndex];
            auto prevState = state;

            if(version != 1)
            {
                state = {
                    .layout = dependency.traits.imageTraits.layout,
                    .bufferUsage = dependency.traits.bufferTraits.usage,
                    .imageUsage = dependency.traits.imageTraits.usage,
                    .queueIndex = queue,
                    .access = dependency.traits.access,
                    .stages = dependency.traits.stages,

                };
            }
            mBarriers[passIndex].emplace_back(Barrier{
                .resourceIndex = resourceIndex,
                .src = prevState,
                .dst = state,
                .subresourceRange = dependency.traits.imageTraits.subresourceRange,
                .offset = dependency.traits.bufferTraits.offset,
                .size = dependency.traits.bufferTraits.size,
            });
        }
        for(auto const &dependent : pass.writes)
        {
            auto resourceIndex = mResourceNameToIndex.at(dependent.resource);
            auto &state = resourceState[resourceIndex];
            auto prevState = state;
            
            state = {
                .layout = dependent.traits.imageTraits.layout,
                .bufferUsage = dependent.traits.bufferTraits.usage,
                .imageUsage = dependent.traits.imageTraits.usage,
                .queueIndex = queue,
                .access = dependent.traits.access,
                .stages = dependent.traits.stages,
            };
            mBarriers[passIndex].emplace_back(Barrier{
                .resourceIndex = resourceIndex,
                .src = prevState,
                .dst = state,
                .subresourceRange = dependent.traits.imageTraits.subresourceRange,
                .offset = dependent.traits.bufferTraits.offset,
                .size = dependent.traits.bufferTraits.size,
            });
            
        }
    }
}

bool RenderGraph::build() {
    if(mUpToDate)
        return true;

    // Very optimistic
    mFailed = false;

    // Index resources
    RENDER_GRAPH_TRACE("Indexing resources");
    mResourceUsage.clear();
    for(auto const &[index, pass] : mPasses)
    {
        for(auto const &resource : pass.reads)
        {
            // Maybe it will be set after building
            if(!mResourceNameToIndex.contains(resource.resource))
                mResourceNameToIndex[resource.resource] = mNextIndex++;
            auto &usage = mResourceUsage[mResourceNameToIndex.at(resource.resource)];
            usage.readInPasses.emplace_back(index, resource.pass.empty() ? 0 : mPassNameToIndex.at(resource.pass));
        }
        for(auto const &resource : pass.writes)
        {
            if(!mResourceNameToIndex.contains(resource.resource))
                mResourceNameToIndex[resource.resource] = mNextIndex++;
            auto &usage = mResourceUsage[mResourceNameToIndex.at(resource.resource)];
            usage.writtenInPasses.emplace_back(index);
        }
    }

    // Validate passes
    // TODO: more validation
    RENDER_GRAPH_TRACE("Validating passes");
    mNodeState.clear();
    for(auto const &[index, pass] : mPasses)
    {
        if(pass.reads.empty())
            validate(index);
    }
    if(mFailed)
        return false;

    // Recursively process every pass that has no dependencies
    RENDER_GRAPH_TRACE("Processing passes");
    mNodeState.clear();
    mPassStack.clear();
    for(auto const &[index, pass] : mPasses)
    {
        if(pass.reads.empty())
            processPass(index);
    }

    if(mNodeState.size() != mPasses.size())
        LOG_WARN("not all passes visited!");

    RENDER_GRAPH_TRACE("Pass stack: {}", mPassStack);
    for(uint32_t i = 0; i < mPassStack.size(); ++i)
    {
        auto passIndex = mPassStack[i];
        auto const &pass = mPasses.get(passIndex);
        RENDER_GRAPH_TRACE("{:>2}) {:>15}: {}", i, pass.name, printDependencies(pass));
    }

    RENDER_GRAPH_TRACE("Building barriers");
    buildBarriers();
    if(mFailed == true)
        return false;

    mUpToDate = true;
    return true;
}
