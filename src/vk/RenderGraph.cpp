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

    return &mPasses.get(mPassNameToIndex.at(name));
}
void RenderGraph::clear()
{
    mPasses.clear();
    mPassNameToIndex.clear();
    mResourceUsage.clear();
}
void RenderGraph::processPass(uint32_t index)
{
    assert(index != 0);
    if(mVisitedPasses.contains(index))
        return;

    auto const &pass = mPasses[index];
    LOG_TRACE("Processing pass {}", pass.name);

    // Process passes that read from the old versions of resources passes the pass writes to
    for(auto const &dep : pass.writes)
    {
        auto &usage = mResourceUsage.get(dep.eResource.id());
        for(auto const &[pass, version] : usage.readInPasses)
            if(version == usage.lastPassWrite)
                processPass(pass);
        usage.lastPassWrite = index;
    }
    
    // Process all dependencies
    for(auto const &dep : pass.reads)
    {
        auto const &usage = mResourceUsage.get(dep.eResource.id());
        for(auto const &pass : usage.writtenInPasses)
            if(mPassNameToIndex.at(dep.pass) == pass)
                processPass(pass);
    }

    LOG_TRACE("Adding pass {} {}", index, pass.name);
    mVisitedPasses.emplace(index);
    mPassStack.emplace_back(index);

    // Process all passes that read resources from this pass
    for(auto const &dep : pass.writes)
    {
        auto const &usage = mResourceUsage.get(dep.eResource.id());
        for(auto const &[pass, version] : usage.readInPasses)
            if(version == index)
                processPass(pass);
    }
}

// Remove duplicates from unsorted std::vector
template<typename T>
static void removeDuplicates(std::vector<T> &list)
{
	std::unordered_set<T> seen;

	auto output_itr = std::begin(list);
	for(auto itr = std::begin(list); itr != std::end(list); ++itr)
	{
		if(!seen.count(*itr))
		{
			*output_itr = *itr;
			seen.insert(*itr);
			++output_itr;
		}
	}
	list.erase(output_itr, std::end(list));
}

void RenderGraph::build()
{
    // TODO: more validation
    // Validate passes
    for(auto const &[passIndex, pass] : mPasses)
    {
        assert(passIndex != 0);
        assert(!pass.name.empty());
        assert(!pass.callback);
        for(auto const &dep : pass.reads)
        {
            // assert(dep.eResource.valid());
            assert(!dep.pass.empty() && mPassNameToIndex.contains(dep.pass));
        }
        for(auto const &dep : pass.writes)
        {
            // assert(dep.eResource.valid());
        }
    }

    mResourceUsage.clear();
    for(auto const &[index, pass] : mPasses)
    {
        for(auto const &dep : pass.reads)
        {
            auto &usage = mResourceUsage[dep.eResource.id()];
            usage.readInPasses.emplace_back(index, mPassNameToIndex.at(dep.pass));
        }
        for(auto const &dep : pass.writes)
        {
            auto &usage = mResourceUsage[dep.eResource.id()];
            usage.writtenInPasses.emplace_back(index);
        }
    }


    for(auto const &[index, pass] : mPasses)
    {
        // Recursively process pass if it has no dependencies
        if(pass.reads.empty())
            processPass(index);
    }

    if(mVisitedPasses.size() != mPasses.size())
        LOG_WARN("not all passes visited!");

    LOG_TRACE("Pass stack: {}", mPassStack);
    for(uint32_t i = 0; i < mPassStack.size(); ++i)
    {
        auto passIndex = mPassStack[i];
        auto const &pass = mPasses.get(passIndex);
        std::string reads;
        for(auto dep : pass.reads)
            reads.append(fmt::format("{}{}; ", dep.pass, dep.eResource.id()));
        std::string writes;
        for(auto dep : pass.writes)
            writes.append(fmt::format("{}{}; ", pass.name, dep.eResource.id()));

        LOG_TRACE("{:>2}) {:<36} -> {:<10} -> {:<36}", i, reads, pass.name, writes);
    }

	// Now, reorder passes to extract better pipelining.
	// reorderPasses(mPassStack);

	// Now, we have a linear list of passes to submit in-order which would obey the dependencies.

}
