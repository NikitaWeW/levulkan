#include "RenderGraph.hpp"
#include "Utility.hpp"
#include "Init.hpp"
#include "Resource.hpp"
#include "Logging.hpp"
#include <algorithm>
using namespace vk;

#ifndef RENDER_GRAPH_TRACE
#define RENDER_GRAPH_TRACE LOG_TRACE
#endif

RenderGraph::RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraphCreateInfo const &createInfo)
{
    assert(createInfo.device);
    assert(createInfo.commandPool);
    assert(createInfo.queues.size() > 0);

    mDevice = createInfo.device;
    std::vector<VkCommandBuffer> commandBuffers(createInfo.queues.size());
    VkCommandBufferAllocateInfo commandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = createInfo.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };

    CHECK_VK_RES(vkAllocateCommandBuffers(mDevice, &commandBufferAllocateInfo, commandBuffers.data()));

    for(auto [type, queue] : createInfo.queues)
    {
        mQueues.emplace(type, Queue{
            .queue = queue,
            .commandBuffer = commandBuffers.back()
        });

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
        for(auto const &queue : mQueues.dense())
            vkQueueWaitIdle(queue.queue);

        vkDestroySemaphore(mDevice, mTimelineSemaphore, nullptr);

        // Command buffers will be destroyed with the pool
    }
}
void RenderGraph::addPass(RenderPass const &pass)
{
    if(mPassNameToIndex.contains(pass.name))
    {
        LOG_ERROR("Render graph already contains \"{}\" pass", pass.name);
        return;
    }

    auto index = (mPassNameToIndex[pass.name] = mNextIndex++);
    mPasses.emplace(index, pass);
    mUpToDate = false;
}
void RenderGraph::removePass(std::string const &name)
{
    if(!mPassNameToIndex.contains(name))
    {
        LOG_ERROR("Render graph does not contain \"{}\" pass", name);
        return;
    }

    mPasses.erase(mPassNameToIndex.at(name));
    mPassNameToIndex.erase(name);
    mUpToDate = false;
}
RenderPass const *RenderGraph::findPass(std::string const &name) const
{
    if(!mPassNameToIndex.contains(name))
        return nullptr;

    return &mPasses.get(mPassNameToIndex.at(name));
}
RenderPass *RenderGraph::findPass(std::string const &name)
{
    if(!mPassNameToIndex.contains(name))
        return nullptr;

    mUpToDate = false;
    return &mPasses.get(mPassNameToIndex.at(name));
}
void RenderGraph::clear()
{
    mPasses.clear();
    mPassNameToIndex.clear();
}
void RenderGraph::validate(uint32_t passIndex)
{
    #define VALIDATION_ASSERT(x) if(!static_cast<bool>(x)) { LOG_ERROR("{}:{} Render graph validation assertion failed: {}", __FILE__, __LINE__, #x); mValidationFailed = true; }

    VALIDATION_ASSERT(passIndex != 0);
    auto const &pass = mPasses[passIndex];
    
    VALIDATION_ASSERT(!pass.name.empty());
    VALIDATION_ASSERT(!pass.callback);
    for(auto const &resource : pass.reads)
    {
        // VALIDATION_ASSERT(resource.eResource.valid());
        VALIDATION_ASSERT(resource.pass.empty() || mPassNameToIndex.contains(resource.pass));
    }
    for(auto const &resource : pass.writes)
    {
        // VALIDATION_ASSERT(resource.eResource.valid());
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
std::string RenderGraph::dumpGraphviz(int indent, RenderGraphGraphvizSettings settings) const
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

    for(auto index : mPassStack)
    {
        auto const &pass = mPasses.get(index);
        ss << newline(indent) << "/* pass " << pass.name << " */";
        ss << newline(indent) << pass.name << " " << nodeAttributes << ";";
        
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
                        ss << newline(indent) << mPasses.get(dependency).name << " -> " << pass.name << " [label=\"" << resource.id() << "\"]" << implicitEdgeAttributes << ";";
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
            auto const &version = history ? usage.versions.getLastVersion() : mPassNameToIndex.at(resource.pass);
            ss << newline(indent) << mPasses.get(version).name << " -> " << pass.name << " [label=\"" << resource.id() << "\"]" << (history ? historyEdgeAttributes : explicitEdgeAttributes) << ";";
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
static std::string printDependencies(RenderPass const &pass)
{
    std::string reads;
    for(auto resource : pass.reads)
        reads.append(fmt::format("{}{}; ", resource.pass, resource.id()));
    if(reads.empty())
        reads = "none";
    std::string writes;
    for(auto resource : pass.writes)
        writes.append(fmt::format("{}{}; ", pass.name, resource.id()));
    if(writes.empty())
        writes = "none";

    // return fmt::format("{:<36} -> {:<10} -> {:<36}", reads, pass.name, writes);
    return fmt::format("reads {} | writes {}", reads, writes);
}

// There gotta be a simpler way
// FIXME: spaghetti loop
void RenderGraph::processPass(uint32_t passIndex, bool backtrack)
{
    assert(passIndex != 0);
    
    auto const &pass = mPasses[passIndex];
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
            auto &usage = mResourceUsage.at(resource.eResource);
            // For future use
            auto version = usage.versions.nextVersion++;
            LOG_TRACE(": Adding pass {} to versions of resource {}, version {}, last write {}", pass.name, resource.id(), version, usage.versions.lastPassWrite == 0 ? "none" : mPasses.get(usage.versions.lastPassWrite).name);
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
    // should be updated at write AND read 
    struct ResourceState
    {
        Barrier::Scope lastScope;
    };
    std::unordered_map<uint32_t, ResourceState> resourceState;
    for(auto passIndex : mPassStack)
    {
        auto const &pass = mPasses.get(passIndex);

        for(auto const &resource : pass.reads) {
            bool history = resource.pass.empty();
            auto version = history ? 0 : mPassNameToIndex.at(resource.pass);
            auto const &prevPass = mPasses.get(version);
            auto &state = resourceState[resource.id()];
            auto e = resource.eResource;
            if(e.has<vk::Image>()) {
                auto const &image = e.get<vk::Image>();
            } else if(e.has<vk::Buffer>()) {
                auto const &image = e.get<vk::Image>();
            } else {
                LOG_ERROR("Resource e{} is read by pass {} -> {} and doesent have image nor buffer components!", e.id(), prevPass.name, pass.name);
            }
        }
        for(auto const &resource : pass.writes)
        {
            
        }
    }
}

bool RenderGraph::build()
{
    if(mUpToDate)
        return true;

    // Index resources
    mResourceUsage.clear();
    for(auto const &[index, pass] : mPasses)
    {
        for(auto const &resource : pass.reads)
        {
            auto &usage = mResourceUsage[resource.eResource];
            usage.readInPasses.emplace_back(index, resource.pass.empty() ? 0 : mPassNameToIndex.at(resource.pass));
        }
        for(auto const &resource : pass.writes)
        {
            auto &usage = mResourceUsage[resource.eResource];
            usage.writtenInPasses.emplace_back(index);
        }
    }

    // Validate passes
    // TODO: more validation
    mNodeState.clear();
    mValidationFailed = false;
    for(auto const &[index, pass] : mPasses)
    {
        if(pass.reads.empty())
            validate(index);
    }
    if(mValidationFailed)
        return false;

    // Recursively process every pass that has no dependencies
    mNodeState.clear();
    mPassStack.clear();
    for(auto const &[index, pass] : mPasses)
    {
        if(pass.reads.empty())
            processPass(index);
    }

    if(mNodeState.size() != mPasses.size())
        LOG_WARN("not all passes visited!");

	// Now, we have a linear list of passes to submit in-order which would obey the dependencies. (hopefully :D)

    RENDER_GRAPH_TRACE("Pass stack: {}", mPassStack);
    for(uint32_t i = 0; i < mPassStack.size(); ++i)
    {
        auto passIndex = mPassStack[i];
        auto const &pass = mPasses.get(passIndex);
        std::string reads;
        for(auto resource : pass.reads)
            reads.append(fmt::format("{}{}; ", resource.pass, resource.id()));
        std::string writes;
        for(auto resource : pass.writes)
            writes.append(fmt::format("{}{}; ", pass.name, resource.id()));

        RENDER_GRAPH_TRACE("{:>2}) {:<36} -> {:<10} -> {:<36}", i, reads, pass.name, writes);
    }

    // buildBarriers();
    
    mUpToDate = true;
    return true;
}
