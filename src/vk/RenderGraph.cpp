#include "RenderGraph.hpp"
#include "Utility.hpp"
#include "Init.hpp"
#include "Resource.hpp"
#include "Logging.hpp"
#include <algorithm>
using namespace vk;

void RenderGraph::addPass(RenderPass const &pass)
{
    if(mPassNameToIndex.contains(pass.name))
    {
        LOG_ERROR("Render graph already contains \"{}\" pass", pass.name);
        return;
    }

    auto index = (mPassNameToIndex[pass.name] = mNextIndex++);
    mPasses.get(index) = pass;
    for(auto const &dep : pass.reads)
        mResourceUsage[dep.eResource.id()].readInPasses.emplace_back(index);
    for(auto const &dep : pass.writes)
        mResourceUsage[dep.eResource.id()].writtenInPasses.emplace_back(index);
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
void RenderGraph::clear()
{
    mPasses.clear();
    mPassNameToIndex.clear();
    mResourceUsage.clear();
}
void RenderGraph::processPass(uint32_t index)
{
    assert(index != 0);
    mPassStack.emplace_back(index);

    auto const &pass = mPasses[index];
    for(auto const &dep : pass.reads)
    {
        auto const &usage = mResourceUsage.get(dep.eResource.id());
        if(!dep.pass.empty()) {
            assert(mPassNameToIndex.contains(dep.pass));
            processPass(mPassNameToIndex.at(dep.pass));
        } else if(usage.writtenInPasses.size() == 1) {
            processPass(usage.writtenInPasses[0]);
        } else {
            LOG_ERROR("Resource e{} used by pass \"{}\" has no explicit pass dependency. Make sure its written once and only once.", dep.eResource.id(), pass.name);
        } 

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
	list.erase(output_itr, end(list));
}

void RenderGraph::build()
{
    uint32_t swapchainPassIndex = 0;
    for(auto const &[name, passIndex] : mPassNameToIndex)
    {
        auto const &pass = mPasses[passIndex];
        // Validate
        for(auto const *v : {&pass.reads,&pass.writes})
        for(auto const &res : *v)
        {
            assert(res.eResource.valid());
            assert(res.access != VK_ACCESS_NONE);
            assert(res.stages != VK_PIPELINE_STAGE_NONE);
        }

        for(auto const &res : pass.writes)
        {
            if(res.eResource.has<vk::Swapchain>())
            {
                swapchainPassIndex = passIndex;
                break;
            }
        }
    }

    if(swapchainPassIndex == 0)
    {
        LOG_ERROR("No swapchain pass found!");
        assert(false);
        return;
    }

	// Work our way back from the backbuffer, and sort out all the dependencies.
    // Flatten the graph.
    // FIXME: dep.pass order breaks after removing duplicates
    // Maybe make sure all passes that need a specific version of a resource are grouped together and all execute after that pass
    // or have valid ordering before the next pass writes to that resource in any other way
    processPass(swapchainPassIndex);
    std::reverse(std::begin(mPassStack), std::end(mPassStack));
    removeDuplicates(mPassStack);

	// Now, reorder passes to extract better pipelining.
	// reorderPasses(mPassStack);

	// Now, we have a linear list of passes to submit in-order which would obey the dependencies.

}
