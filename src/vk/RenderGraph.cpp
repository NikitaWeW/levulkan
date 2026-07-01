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
RenderGraph::RenderGraph(RenderGraphCreateInfo const &createInfo)
{
    assert(createInfo.device);
    assert(createInfo.commandPool);

    mDevice = createInfo.device;
    mQueueFamilies = createInfo.queueFamilies;
    std::vector<VkCommandBuffer> commandBuffers(mQueueFamilies.uniqueFamilies.size());
    VkCommandBufferAllocateInfo commandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = createInfo.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };

    CHECK_VK_RES(vkAllocateCommandBuffers(mDevice, &commandBufferAllocateInfo, commandBuffers.data()));

    for(auto familyIndex : mQueueFamilies.uniqueFamilies)
    {
        mCommandBuffers[familyIndex] = commandBuffers.back();
        commandBuffers.pop_back();
    }

    // VkSemaphoreTypeCreateInfo semaphoreTypeCI{
    //     .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    //     .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    //     .initialValue = 0,
    // };
    // VkSemaphoreCreateInfo semaphoreCI{
    //     .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    //     .pNext = &semaphoreTypeCI,
    // };
    // CHECK_VK_RES(vkCreateSemaphore(mDevice, &semaphoreCI, nullptr, &mTimelineSemaphore));
}
RenderGraph::RenderGraph(RenderGraph &&) = default;
RenderGraph &RenderGraph::operator=(RenderGraph &&) = default;
RenderGraph::~RenderGraph() 
{
    if(mDevice)
    {
        vkDeviceWaitIdle(mDevice);

        // vkDestroySemaphore(mDevice, mTimelineSemaphore, nullptr);

        // Command buffers will be destroyed with the pool
    }
}
void RenderGraph::addPass(RenderPass const &pass)
{
    if(mResult.passNameToIndex.contains(pass.name))
    {
        LOG_ERROR("Render graph already contains \"{}\" pass", pass.name);
        return;
    }

    auto index = (mResult.passNameToIndex[pass.name] = mNextIndex++);
    mResult.passes.emplace(index, pass);
    mUpToDate = false;
}
void RenderGraph::removePass(std::string const &name)
{
    if(!mResult.passNameToIndex.contains(name))
    {
        LOG_ERROR("Render graph does not contain \"{}\" pass", name);
        return;
    }

    mResult.passes.erase(mResult.passNameToIndex.at(name));
    mResult.passNameToIndex.erase(name);
    mUpToDate = false;
}
RenderPass const *RenderGraph::findPass(std::string const &name) const
{
    if(!mResult.passNameToIndex.contains(name))
        return nullptr;

    return &mResult.passes.get(mResult.passNameToIndex.at(name));
}
RenderPass *RenderGraph::findPass(std::string const &name)
{
    if(!mResult.passNameToIndex.contains(name))
        return nullptr;

    mUpToDate = false;
    return &mResult.passes.get(mResult.passNameToIndex.at(name));
}
void RenderGraph::clear()
{
    mResult.passes.clear();
    mResult.passNameToIndex.clear();
}

RenderGraph::Result RenderGraph::getResult() const
{
    return mResult;
}
bool RenderGraph::isUpToDate() const
{
    return mUpToDate;
}

void RenderGraph::validate(uint32_t passIndex)
{
    #define VALIDATION_ASSERT(x) if(!static_cast<bool>(x)) { LOG_ERROR("{}:{} Render graph validation assertion failed: {}", __FILE__, __LINE__, #x); mFailed = true; }

    VALIDATION_ASSERT(passIndex != 0);
    auto const &pass = mResult.passes[passIndex];
    
    VALIDATION_ASSERT(!pass.name.empty());
    VALIDATION_ASSERT(!pass.callback);
    for(auto const &resource : pass.reads)
    {
        VALIDATION_ASSERT(resource.eResource.valid());
        VALIDATION_ASSERT(resource.pass.empty() || mResult.passNameToIndex.contains(resource.pass));
        VALIDATION_ASSERT(resource.traits.valid());
    }
    for(auto const &resource : pass.writes)
    {
        VALIDATION_ASSERT(resource.eResource.valid());
        VALIDATION_ASSERT(resource.traits.valid());
    }

    // TODO: More sophisticated cycle detection.
    VALIDATION_ASSERT(mNodeState[passIndex] != NodeState::Added && "Cycle detected!");

    for(auto const &resource : pass.writes)
    {
        auto const &usage = mResourceUsage.at(resource.eResource);
        for(auto const &[pass, version] : usage.readInPasses)
            if(version == passIndex)
                validate(pass);
    }

    #undef VALIDATION_ASSERT
}
static std::string collapseAttributes(std::vector<std::string> const &attributes)
{
    std::string res;
    for(auto const &attrib : attributes)
        res.append("[").append(attrib).append("]");
    return res;
}
std::string RenderGraph::dumpGraphviz(int indent, GraphvizSettings settings) const
{
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

    for(auto index : mResult.passStack)
    {
        auto const &pass = mResult.passes.get(index);
        auto name = pass.name;
        name.erase(std::remove(name.begin(), name.end(), '\"'), name.end());

        ss << newline(indent) << "\"" << name << "\" " << nodeAttributes << ";";
        
        // Implicit dependencies
        if(settings.implicitDependencies)
        {
            for(auto const &resource : pass.writes)
            {  
                auto &usage = mResourceUsage.at(resource.eResource);
                auto currentVersion = usage.versions.passToVersion.at(index);
                for(auto const &[dependency, passVersion] : usage.readInPasses) {
                    if(dependency == index)
                        continue;
                    auto version = passVersion ? usage.versions.passToVersion.at(passVersion) : 1;
                    if(currentVersion - version == 1)
                        ss << newline(indent) << "\"" << mResult.passes.get(dependency).name << "\" -> \"" << name << "\" [label=\"" << resource.id() << "\"]" << implicitEdgeAttributes << ";";
                }
            }
        }
        
        // Dependencies
        for(auto const &resource : pass.reads)
        {
            bool history = resource.pass.empty();
            if(history && !settings.showHistory)
                continue;
            
            auto const &usage = mResourceUsage.at(resource.eResource);
            auto const &version = history ? usage.versions.getLastVersion() : mResult.passNameToIndex.at(resource.pass);
            ss << newline(indent) << "\"" << mResult.passes.get(version).name << "\" -> \"" << name << "\" [label=\"" << resource.id() << "\"]" << (history ? historyEdgeAttributes : explicitEdgeAttributes) << ";";
        }
    }

    std::string title;
    for(auto index : mResult.passStack)
        title.append(mResult.passes.get(index).name).append(" -> ");
    title.erase(title.size() - std::string_view(" -> ").size());
    ss << newline(indent) << newline(indent) << "label = \"" << title << "\";";

    ss << newline(0) << "}";

    return ss.str();
}
static std::string printDependencies(RenderPass const &pass)
{
    std::string reads;
    for(auto resource : pass.reads)
        reads.append(fmt::format("{}/{}; ", resource.pass, resource.id()));
    if(reads.empty())
        reads = "none";
    std::string writes;
    for(auto resource : pass.writes)
        writes.append(fmt::format("{}/{}; ", pass.name, resource.id()));
    if(writes.empty())
        writes = "none";

    return fmt::format("    reads {:>40} | writes {:>40}", reads, writes);
}

// There gotta be a simpler way
void RenderGraph::processPass(uint32_t passIndex, bool backtrack)
{
    assert(passIndex != 0);
    
    auto const &pass = mResult.passes[passIndex];
    RENDER_GRAPH_TRACE("Processing pass {}, backtrack {}, {}", pass.name, backtrack, printDependencies(pass));

    if(mNodeState[passIndex] != NodeState::None)
        return;

    // Process all implicit dependencies
    // passes that read from the old versions of resources the current pass writes to
    RENDER_GRAPH_TRACE("  Processing all implicit dependencies of pass {}", pass.name);
    for(auto const &resource : pass.writes)
    {
        auto &usage = mResourceUsage.at(resource.eResource);
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
            auto const &dependency = mResult.passNameToIndex.at(resource.pass);
            processPass(dependency, true);
            RENDER_GRAPH_TRACE("Back to processing pass {}, backtrack {}", pass.name, backtrack);
        }
    }

    if(mNodeState[passIndex] != NodeState::Added)
    {
        mNodeState[passIndex] = NodeState::Added;
        RENDER_GRAPH_TRACE(" Adding pass {}", pass.name);
        mResult.passStack.emplace_back(passIndex);

        RENDER_GRAPH_TRACE("  Adding pass {} to resource versions.", pass.name);
        for(auto const &resource : pass.writes)
        {
            auto &usage = mResourceUsage.at(resource.eResource);
            // For future use
            auto version = usage.versions.nextVersion++;
            RENDER_GRAPH_TRACE(": Adding pass {} to versions of resource {}, version {}, last write {}", pass.name, resource.id(), version, usage.versions.lastPassWrite == 0 ? "none" : mResult.passes.get(usage.versions.lastPassWrite).name);
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
        auto &usage = mResourceUsage.at(resource.eResource);
        for(auto const &[dependent, version] : usage.readInPasses)
            if(version == passIndex)
                processPass(dependent, backtrack);
    }
}

void RenderGraph::buildBarriers()
{
    std::unordered_map<Entity, Barrier::Scope> resourceState;
    for(auto passIndex : mResult.passStack)
    {
        auto const &pass = mResult.passes.get(passIndex);
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

            auto const &usage = mResourceUsage.at(dependency.eResource);
            auto version = history ? usage.versions.getLastVersion() : mResult.passNameToIndex.at(dependency.pass);
            auto &state = resourceState[dependency.eResource];
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
            mResult.barriers[passIndex].emplace_back(Barrier{
                .eResource = dependency.eResource,
                .src = prevState,
                .dst = state,
                .subresourceRange = dependency.traits.imageTraits.subresourceRange,
                .offset = dependency.traits.bufferTraits.offset,
                .size = dependency.traits.bufferTraits.size,
            });
        }
        for(auto const &dependent : pass.writes)
        {
            auto &state = resourceState[dependent.eResource];
            auto prevState = state;
            
            state = {
                .layout = dependent.traits.imageTraits.layout,
                .bufferUsage = dependent.traits.bufferTraits.usage,
                .imageUsage = dependent.traits.imageTraits.usage,
                .queueIndex = queue,
                .access = dependent.traits.access,
                .stages = dependent.traits.stages,

            };
            mResult.barriers[passIndex].emplace_back(Barrier{
                .eResource = dependent.eResource,
                .src = prevState,
                .dst = state,
                .subresourceRange = dependent.traits.imageTraits.subresourceRange,
                .offset = dependent.traits.bufferTraits.offset,
                .size = dependent.traits.bufferTraits.size,
            });
            
        }
    }
}

bool RenderGraph::build()
{
    if(mUpToDate)
        return true;

    // Very optimistic
    mFailed = false;
    mResult.valid = false;

    // Index resources
    RENDER_GRAPH_TRACE("Indexing resources");
    mResourceUsage.clear();
    for(auto const &[index, pass] : mResult.passes)
    {
        for(auto const &resource : pass.reads)
        {
            auto &usage = mResourceUsage[resource.eResource];
            usage.readInPasses.emplace_back(index, resource.pass.empty() ? 0 : mResult.passNameToIndex.at(resource.pass));
        }
        for(auto const &resource : pass.writes)
        {
            auto &usage = mResourceUsage[resource.eResource];
            usage.writtenInPasses.emplace_back(index);
        }
    }

    // Validate passes
    // TODO: more validation
    RENDER_GRAPH_TRACE("Validating passes");
    mNodeState.clear();
    for(auto const &[index, pass] : mResult.passes)
    {
        if(pass.reads.empty())
            validate(index);
    }
    if(mFailed)
        return false;

    // Recursively process every pass that has no dependencies
    RENDER_GRAPH_TRACE("Processing passes");
    mNodeState.clear();
    mResult.passStack.clear();
    for(auto const &[index, pass] : mResult.passes)
    {
        if(pass.reads.empty())
            processPass(index);
    }

    if(mNodeState.size() != mResult.passes.size())
        LOG_WARN("not all passes visited!");

    RENDER_GRAPH_TRACE("Pass stack: {}", mResult.passStack);
    for(uint32_t i = 0; i < mResult.passStack.size(); ++i)
    {
        auto passIndex = mResult.passStack[i];
        auto const &pass = mResult.passes.get(passIndex);
        RENDER_GRAPH_TRACE("{:>2}) {:>15}: {}", i, pass.name, printDependencies(pass));
    }

    RENDER_GRAPH_TRACE("Building barriers");
    buildBarriers();
    if(mFailed == true)
        return false;

    mUpToDate = true;
    mResult.valid = true;
    return true;
}
