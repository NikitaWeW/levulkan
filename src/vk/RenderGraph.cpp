#include "RenderGraph.hpp"
#include "Utility.hpp"
#include "Init.hpp"
#include "Resource.hpp"
#include "Logging.hpp"
#include <algorithm>
using namespace vk;

RenderGraph::RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraph const &) = default;
RenderGraph &RenderGraph::operator=(RenderGraph const &) = default;
RenderGraph::~RenderGraph() = default;
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
void RenderGraph::processValidation(uint32_t index)
{
    assert(index != 0);
    auto const &pass = mPasses[index];
    
    assert(!pass.name.empty());
    assert(!pass.callback);
    for(auto const &resource : pass.reads)
    {
        assert(resource.eResource.valid());
        assert(!resource.pass.empty() && mPassNameToIndex.contains(resource.pass));
    }
    for(auto const &resource : pass.writes)
    {
        assert(resource.eResource.valid());
    }

    assert(mNodeState[index] != NodeState::Added && "Cycle detected!");

    for(auto const &resource : pass.writes)
    {
        auto const &usage = mResourceUsage.get(resource.eResource.id());
        for(auto const &[pass, version] : usage.readInPasses)
            if(version == index)
                processValidation(pass);
    }
}
std::string RenderGraph::dump(int indent, bool implicitDependencies) const
{
    assert(mUpToDate && "you need to build the render graph first!");
    std::stringstream ss;
    auto newline = [indent](int i){return (indent >= 0) ? ("\n" + std::string(i, ' ')) : " "; };
    
    ss << "digraph RenderGraph {";

    for(auto const &[index, pass] : mPasses)
    {
        ss << newline(indent) << "/* pass " << pass.name << " */";

        if(implicitDependencies)
        {
            // Process all implicit dependencies
            for(auto const &resource : pass.writes)
            {
                auto &usage = mResourceUsage.get(resource.eResource.id());
                for(auto const &[dependency, version] : usage.readInPasses)
                    if(dependency != index && usage.passVersions.at(version) < usage.passVersions.at(index))
                        ss << newline(indent) << mPasses.get(dependency).name << " -> " << pass.name << " [label=\"" << resource.eResource.id() << "\"]" << "[style=dashed];";
            }
        }
        
        // Process all dependencies
        for(auto const &resource : pass.reads)
        {
            auto const &version = mPassNameToIndex.at(resource.pass);
            auto const &usage = mResourceUsage.get(resource.eResource.id());
            for(auto const &dependency : usage.writtenInPasses)
                if(dependency == version)
                    ss << newline(indent) << mPasses.get(dependency).name << " -> " << pass.name << " [label=\"" << resource.eResource.id() << "\"]" << "[style=solid];";
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

void RenderGraph::processPass(uint32_t index, bool backtrack)
{
    assert(index != 0);
    if(mNodeState[index] == NodeState::Added)
        return;
    
    auto const &pass = mPasses[index];
    LOG_TRACE("Processing pass {}, backtrack {}", pass.name, backtrack);

    // Process all implicit dependencies
    // passes that read from the old versions of resources the current pass writes to
    LOG_TRACE("  Process all implicit dependencies of pass {}", pass.name);
    for(auto const &resource : pass.writes)
    {
        auto &usage = mResourceUsage.get(resource.eResource.id());
        for(auto const &[dependency, version] : usage.readInPasses)
            if(dependency != index && usage.lastPassWrite == version)
                processPass(dependency, true);
        usage.lastPassWrite = index;
        usage.passVersions[index] = usage.passVersions.size(); // For future use
    }

    // Process all dependencies
    LOG_TRACE("  Process all dependencies of pass {}", pass.name);
    for(auto const &resource : pass.reads)
    {
        auto const &version = mPassNameToIndex.at(resource.pass);
        auto const &usage = mResourceUsage.get(resource.eResource.id());
        for(auto const &dependency : usage.writtenInPasses)
            if(dependency == version)
                processPass(dependency, true);
    }

    if(mNodeState[index] != NodeState::Added)
    {
        mNodeState[index] = NodeState::Added;
        LOG_TRACE(" Adding pass {}", pass.name);
        mPassStack.emplace_back(index);

        if(backtrack)
            LOG_TRACE(" Backtracking...");
        if(backtrack)
            return;
    }

    // Process all dependents
    LOG_TRACE("  Process all dependents on the pass {}", pass.name);
    for(auto const &resource : pass.writes)
    {
        auto const &usage = mResourceUsage.get(resource.eResource.id());
        for(auto const &[dependent, version] : usage.readInPasses)
            if(version == index)
                processPass(dependent, backtrack);
    }
}

void RenderGraph::build()
{
    if(mUpToDate)
        return;

    // Index resources
    mResourceUsage.clear();
    for(auto const &[index, pass] : mPasses)
    {
        for(auto const &resource : pass.reads)
        {
            auto &usage = mResourceUsage[resource.eResource.id()];
            usage.readInPasses.emplace_back(index, mPassNameToIndex.at(resource.pass));
        }
        for(auto const &resource : pass.writes)
        {
            auto &usage = mResourceUsage[resource.eResource.id()];
            usage.writtenInPasses.emplace_back(index);
        }
    }

    // Validate passes
    // TODO: more validation
    // mNodeState.clear();
    // for(auto const &[index, pass] : mPasses)
    // {
    //     if(pass.reads.empty())
    //         processValidation(index);
    // }

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

    LOG_TRACE("Pass stack: {}", mPassStack);
    for(uint32_t i = 0; i < mPassStack.size(); ++i)
    {
        auto passIndex = mPassStack[i];
        auto const &pass = mPasses.get(passIndex);
        std::string reads;
        for(auto resource : pass.reads)
            reads.append(fmt::format("{}{}; ", resource.pass, resource.eResource.id()));
        std::string writes;
        for(auto resource : pass.writes)
            writes.append(fmt::format("{}{}; ", pass.name, resource.eResource.id()));

        LOG_TRACE("{:>2}) {:<36} -> {:<10} -> {:<36}", i, reads, pass.name, writes);
    }
    
    mUpToDate = true;
}
