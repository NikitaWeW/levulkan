#include "Renderer.hpp"

enum class NodeState { None = 0, Visited, Added };

struct Pass {
    std::string name;
    uint index = 0; // Number in the pass stack
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
    std::vector<Barrier> barriers;
    VkQueueFlagBits queue;
    std::unique_ptr<IRenderPassStorage> storage;
};
struct ResourcePoolAllocation {
    VkImageUsageFlags imageUsage = 0;
    VkBufferUsageFlags bufferUsage = 0;
    VkDeviceSize bufferSize = 0;
};
struct ResourceCreateInfo {
    enum Type { External, Image, Buffer } type;
    union {
        // Entity external; // Already an entity
        RenderGraphImageCreateInfo image;
        RenderGraphBufferCreateInfo buffer;
    };
};
struct ResourceInfo {
    std::string name;

    Entity eResource; // Contains at least ResourceCreateInfo

    // Indices in the pass stack
    uint lifetimeBegin = std::numeric_limits<uint>::max();
    uint lifetimeEnd = 0;

    std::vector<ResourceUsage> read;
    ResourceUsage written;
};
// To pass intermediate data between functions
struct RenderGraphImpl {
    vk::AllocationCreateInfo allocInfo;
    Registry *reg = nullptr;
    vk::QueueFamilies queueFamilies;

    std::unordered_set<Entity> resourcePool;
    std::unordered_map<std::string, NodeState> nodeState;
    std::unordered_map<std::string, Pass> passes;
    std::unordered_map<std::string, ResourceInfo> resources;
    std::vector<std::string> passStack;
};

struct RenderGraphResultImpl {
    std::unordered_map<std::string, RenderPass> passes;
    std::unordered_map<std::string, ResourceInfo> resources;
    std::vector<std::string> passStack;
};

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
void RenderPassBuilder::addImageResource(std::string_view name, RenderGraphImageCreateInfo info) {
    mImageResources[std::string(name)] = info;
}
void RenderPassBuilder::addBufferResource(std::string_view name, RenderGraphBufferCreateInfo info) {
    mBufferResources[std::string(name)] = info;
}
void RenderPassBuilder::attachResourceRead(std::string_view resourceName, ResourceTraits traits) {
    mReads.emplace_back(std::string(resourceName), traits);
}
void RenderPassBuilder::attachResourceWrite(std::string_view resourceName, ResourceTraits traits) {
    mWrites.emplace_back(std::string(resourceName), traits);
}

void RenderGraphBuilder::setAllocInfo(vk::AllocationCreateInfo const &allocInfo, Registry &reg) {
    this->allocInfo = allocInfo;
    this->reg = &reg;
}
void RenderGraphBuilder::setQueueFamilies(vk::QueueFamilies const &queueFamilies) {
    this->queueFamilies = queueFamilies;
}

RenderGraphResult::RenderGraphResult(RenderGraphResultImpl *data) {
    mImpl = data;
}
RenderGraphResult::~RenderGraphResult() {
    if(mImpl)
        delete mImpl;
}
RenderGraphResult::RenderGraphResult(RenderGraphResult const &rhs) {
    *this = rhs;
}
RenderGraphResult::RenderGraphResult(RenderGraphResult &&rhs) {
    *this = std::move(rhs);
}
RenderGraphResult &RenderGraphResult::operator=(RenderGraphResult const &rhs) {
    if(rhs.mImpl) {
        mImpl = new RenderGraphResultImpl(*rhs.mImpl);
    }
    return *this;
}
RenderGraphResult &RenderGraphResult::operator=(RenderGraphResult &&rhs) {
    std::swap(mImpl, rhs.mImpl);
    return *this;
}
bool RenderGraphResult::success() const {
    return mImpl;
}
void RenderGraphResult::setResource(std::string_view name, Entity eResource) {
    assert(success());

    if(!mImpl->resources.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain resource \"{}\"", name);
        return;
    }

    mImpl->resources.at(std::string(name)).eResource = eResource;
}
Entity RenderGraphResult::getResource(std::string_view name) const {
    assert(success());

    if(!mImpl->resources.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain resource \"{}\"", name);
        return Entity();
    }

    return mImpl->resources.at(std::string(name)).eResource;
}
RenderPass const *RenderGraphResult::getPass(std::string_view name) const {
    assert(success());

    if(!mImpl->passes.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain pass \"{}\"", name);
        return nullptr;
    }

    return &mImpl->passes.at(std::string(name));
}
std::vector<std::string> const &RenderGraphResult::getPassStack() const {
    assert(success());

    return mImpl->passStack;
}

static std::string makeHtmlTimeline(RenderGraphResultImpl const &renderGraph) {
    std::string html;
    html.reserve(8192);

    html += "<style>"
              "table { border-collapse: collapse; font-family: monospace; font-size: 14px; }"
              "th, td { padding: 10px 15px; text-align: center; border: 1px solid #e0e0e0; }"
              "th { background-color: #f5f5f5; font-weight: bold; }"
              ".res-name { text-align: left; font-weight: bold; background-color: #fafafa; border-right: 2px solid #ccc; }"
              ".line-active { border-left: none; border-right: none; color: white; font-weight: bold; }"
              ".line-start { border-left: 1px solid #e0e0e0; border-top-left-radius: 4px; border-bottom-left-radius: 4px; }"
              ".line-end { border-right: 1px solid #e0e0e0; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
              ".read { background-color: #279657; }"
              ".write { background-color: #c42b30; }"
              ".active { background-color: #bfb272; }"
              ".inactive { background-color: #ffffff; color: #eeeeee; }"
            "</style>";

    html += "<table><thead><tr><th>Resource / Pass</th>";
    for(auto const &[_, pass] : renderGraph.passes) {
        html += "<th>" + pass.name + "</th>";
    }
    html += "</tr></thead><tbody>";

    for(auto const &[_, resource] : renderGraph.resources) {
        html += "<tr><td class=\"res-name\">" + resource.name + "</td>";

        for(uint col = 0; col < renderGraph.passStack.size(); ++col) {
            if(col >= resource.lifetimeBegin && col <= resource.lifetimeEnd) {
                auto const &passName = renderGraph.passStack.at(col);
                enum class Access { Read, Write, None } access = Access::None;
                if(resource.written.name == passName) {
                    access = Access::Write;
                } else {
                    for(auto const &[name, _] : resource.read) {
                        if(name == passName) {
                            access = Access::Read;
                            break;
                        }
                    }
                }
                 

                std::string classList = "line-active";
                if(col == resource.lifetimeBegin) classList += " line-start";
                if(col == resource.lifetimeEnd) classList += " line-end";
                
                std::string label = "-";
                if(access == Access::Read) {
                    classList += " read";
                    label = "R";
                } else if(access == Access::Write) {
                    classList += " write";
                    label = "W";
                } else {
                    classList += " active";
                }

                html += "<td class= &quot " + classList + " &quot >" + label + "</td>";
            } else {
                html += "<td class= &quot inactive &quot >&middot;</td>";
            }
        }
        html += "</tr>";
    }

    html += "</tbody></table>";
    return html;
}
static std::string collapseAttributes(std::vector<std::string> const &attributes) {
    std::string res;
    for(auto const &attrib : attributes)
        res.append("[").append(attrib).append("]");
    return res;
}
std::string RenderGraphResult::dumpGraphviz(int indent, GraphvizSettings settings) const {
    assert(success());

    std::stringstream ss;
    auto newline = [indent](int i){ return (indent >= 0) ? ("\n" + std::string(i, ' ')) : " "; };
    
    ss << "digraph RenderGraph {";
    for(auto const &attrib : settings.graphAttributes)
        ss << newline(indent) << attrib << ";";

    std::string nodeAttributes = collapseAttributes(settings.nodeAttributes);
    std::string edgeAttributes = collapseAttributes(settings.edgeAttributes);

    for(auto passName : mImpl->passStack) {
        auto const &pass = mImpl->passes.at(passName);
        passName.erase(std::remove(passName.begin(), passName.end(), '\"'), passName.end());

        ss << newline(indent) << "\"" << passName << "\" " << nodeAttributes << ";";
        
        for(auto const &[resourceName, _] : pass.reads) {

            auto &resource = mImpl->resources.at(resourceName);
            ss << newline(indent) << fmt::format("\"{}\" -> \"{}\" [label=\"{}\"]{}", resource.written.name, passName, resource.eResource, edgeAttributes);
        }
    }

    ss << newline(indent) << "Legend [";
    ss << newline(indent*2) << "shape=none";
    ss << newline(indent*2) << "pinned=true";
    ss << newline(indent*2) << "pos=0,5!";
    ss << newline(indent*2) << "label=\"" << makeHtmlTimeline(*mImpl) << "\";";
    ss << newline(indent) << "];";

    // std::string title;
    // for(auto index : mPassStack)
    //     title.append(mPasses.get(index).name).append(" -> ");
    // title.erase(title.size() - std::string_view(" -> ").size());
    // ss << newline(indent) << newline(indent) << "label = \"" << title << "\";";

    ss << newline(0) << "}";

    return ss.str();
}

bool setupRenderGraph(RenderGraphBuilder &&builder, RenderGraphImpl &renderGraph) {
    assert(builder.fresh && "Cannot use same RenderGraphBuilder twice!");
    builder.fresh = false;
    if(builder.allocInfo.has_value()) {
        renderGraph.reg = std::move(builder.reg);
        renderGraph.allocInfo = std::move(builder.allocInfo.value());
    }
    assert(builder.queueFamilies.has_value() && "Forgot to call RenderGraphBuilder::setQueueFamilies?");
    renderGraph.queueFamilies = std::move(builder.queueFamilies.value());
    for(auto &&[passName, pass] : builder.passes) {
        if(renderGraph.passes.contains(passName)) {
            LOG_ERROR("Render graph already contains pass \"{}\"", passName);
            return false;
        }

        auto &renderGraphPass = renderGraph.passes[passName] = {
            .name = std::move(pass.name),
            .queue = std::move(pass.queue),
            .storage = std::move(pass.storage),
        };

        RenderPassBuilder builder;
        renderGraphPass.storage->setup(builder);
        for(auto const &[name, eResource] : builder.mExternalResources) {
            if(renderGraph.resources.contains(name)) {
                LOG_ERROR("Resoure \"{}\" already exists(error in pass \"{}\")", name, pass.name); 
                return false;
            }

            auto &resourceInfo = renderGraph.resources[name];
            resourceInfo.name = name;
            resourceInfo.eResource = eResource;
            resourceInfo.eResource.emplace<ResourceCreateInfo>(
                ResourceCreateInfo::Type::External
            );
        }
        for(auto const &[name, info] : builder.mImageResources) {
            if(renderGraph.resources.contains(name)) {
                LOG_ERROR("Resoure \"{}\" already exists(error in pass \"{}\")", name, pass.name); 
                return false;
            }

            auto &resourceInfo = renderGraph.resources[name];
            resourceInfo.name = name;
            resourceInfo.eResource = renderGraph.reg->create(ResourceCreateInfo{
                .type = ResourceCreateInfo::Type::Image,
                .image = info
            });
        }
        for(auto const &[name, info] : builder.mBufferResources) {
            if(renderGraph.resources.contains(name)) {
                LOG_ERROR("Resoure \"{}\" already exists(error in pass \"{}\")", name, pass.name); 
                return false;
            }

            auto &resourceInfo = renderGraph.resources[name];
            resourceInfo.name = name;
            resourceInfo.eResource = renderGraph.reg->create(ResourceCreateInfo{
                .type = ResourceCreateInfo::Type::Buffer,
                .buffer = info
            });
        }
        for(auto const &[name, traits] : builder.mWrites) {
            if(!renderGraph.resources.contains(name)) {
                LOG_ERROR("Pass \"{}\" writes to unknown resource \"{}\"", pass.name, name);
                return false;
            }

            auto &resourceInfo = renderGraph.resources[name];

            if(!resourceInfo.written.name.empty()) {
                LOG_ERROR("Cannot write to resource \"{}\" more than once!(error in pass \"{}\")", name, pass.name);
                return false;
            }
            resourceInfo.name = name;
            resourceInfo.written = {pass.name, traits};

            renderGraphPass.writes.emplace_back(name, traits);
        }
        for(auto const &[name, traits] : builder.mReads) {
            if(!renderGraph.resources.contains(name)) {
                LOG_ERROR("Pass \"{}\" reads from unknown resource \"{}\"", pass.name, name);
                return false;
            }

            auto &resourceInfo = renderGraph.resources[name];
            if(resourceInfo.written.name == pass.name) {
                LOG_ERROR("Cannot read and write to the same resource \"{}\". Use different names!(error in pass \"{}\")", name, pass.name);
                return false;
            }

            resourceInfo.name = name;
            resourceInfo.read.emplace_back(pass.name, traits);

            renderGraphPass.reads.emplace_back(name, traits);
        }
    }

    return true;
}

// Simplified version of vk::RenderGraph's processPass
static bool processPass(std::string const &passName, RenderGraphImpl &renderGraph, uint depth, bool backtrack = false) {
    assert(!passName.empty());

    if(depth == 0) {
        LOG_ERROR("Cycle detected!");
        return false;
    }

    auto &nodeState = renderGraph.nodeState[passName];
    if(nodeState != NodeState::None)
        return true;

    auto &pass = renderGraph.passes.at(passName);

    // Cull passes results of which are not used anywhere
    bool used = false;
    for(auto const &resource : pass.writes) {
        auto const &info = renderGraph.resources.at(resource.name);
        if(!info.read.empty()) {
            used = true;
            break;
        }
    }
    if(!used) {
        return true;
    }

    for(auto const &resource : pass.reads) {
        auto const &info = renderGraph.resources.at(resource.name);
        if(!processPass(info.written.name, renderGraph, depth - 1, true)) {
            return false;
        }
    }

    if(nodeState != NodeState::Added) {
        nodeState = NodeState::Added;
        pass.index = renderGraph.passStack.size();
        renderGraph.passStack.emplace_back(pass.name);
        if(backtrack)
            return true;
    }

    for(auto const &resource : pass.writes) {
        auto const &info = renderGraph.resources.at(resource.name);
        for(auto const &[dependencyName, _] : info.read)
            if(!processPass(dependencyName, renderGraph, depth - 1, backtrack))
                return false;
    }

    return true;
}
static std::string printDependencies(Pass const &pass) {
    std::string reads;
    for(auto [name, traits] : pass.reads)
        reads.append(name).append("; ");
    if(reads.empty())
        reads = "none";
    std::string writes;
    for(auto [name, traits] : pass.writes)
        reads.append(name).append("; ");
    if(writes.empty())
        writes = "none";

    return fmt::format("reads {:>40} | writes {:>40}", reads, writes);
}
static void calculateLifetimes(RenderGraphImpl &renderGraph) {
    for(auto &[_, resource] : renderGraph.resources) {
        for(auto const &[passName, traits] : resource.read) {
            auto const &pass = renderGraph.passes.at(passName);
            resource.lifetimeEnd = std::max(resource.lifetimeEnd, pass.index);
        }
        if(!resource.written.name.empty()) {
            auto const &pass = renderGraph.passes.at(resource.written.name);
            resource.lifetimeBegin = std::min(resource.lifetimeBegin, pass.index);
        }
    }
}
static bool isCompatible(RenderGraphImageCreateInfo const &first, RenderGraphImageCreateInfo const &second) {
    return // oh god here we go again
        first.resizeToSwapchain == second.resizeToSwapchain &&
        first.imageInfo.imageType == second.imageInfo.imageType &&
        first.imageInfo.format == second.imageInfo.format &&
        first.imageInfo.dimensions.width == second.imageInfo.dimensions.width &&
        first.imageInfo.dimensions.height == second.imageInfo.dimensions.height &&
        first.imageInfo.dimensions.depth == second.imageInfo.dimensions.depth &&
        first.imageInfo.dimensions.mipLevels == second.imageInfo.dimensions.mipLevels &&
        first.imageInfo.dimensions.arrayLayers == second.imageInfo.dimensions.arrayLayers &&
        first.imageInfo.dimensions.samples == second.imageInfo.dimensions.samples &&
       (first.imageInfo.sampler.flags & second.imageInfo.sampler.flags) &&
        first.imageInfo.sampler.magFilter == second.imageInfo.sampler.magFilter &&
        first.imageInfo.sampler.minFilter == second.imageInfo.sampler.minFilter &&
        first.imageInfo.sampler.mipmapMode == second.imageInfo.sampler.mipmapMode &&
        first.imageInfo.sampler.addressModeU == second.imageInfo.sampler.addressModeU &&
        first.imageInfo.sampler.addressModeV == second.imageInfo.sampler.addressModeV &&
        first.imageInfo.sampler.addressModeW == second.imageInfo.sampler.addressModeW &&
        std::abs(first.imageInfo.sampler.mipLodBias - second.imageInfo.sampler.mipLodBias) <= 1e-6 &&
        first.imageInfo.sampler.anisotropyEnable == second.imageInfo.sampler.anisotropyEnable &&
        std::abs(first.imageInfo.sampler.maxAnisotropy - second.imageInfo.sampler.maxAnisotropy) <= 1e-6 &&
        first.imageInfo.sampler.compareEnable == second.imageInfo.sampler.compareEnable &&
        first.imageInfo.sampler.compareOp == second.imageInfo.sampler.compareOp &&
        std::abs(first.imageInfo.sampler.minLod - second.imageInfo.sampler.minLod) <= 1e-6 &&
        first.imageInfo.sampler.borderColor == second.imageInfo.sampler.borderColor &&
        std::abs(first.imageInfo.sampler.customBorderColor.r - second.imageInfo.sampler.customBorderColor.r) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.g - second.imageInfo.sampler.customBorderColor.g) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.b - second.imageInfo.sampler.customBorderColor.b) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.a - second.imageInfo.sampler.customBorderColor.a) <= 1e-6 &&
        first.imageInfo.sampler.unnormalizedCoordinates == second.imageInfo.sampler.unnormalizedCoordinates;

}
// Additionally checks the type of an external resource
static ResourceCreateInfo::Type getResourceType(RestrictedAnyEntity<ResourceCreateInfo> e) {
    auto const &ci = e.get<ResourceCreateInfo>();
    if(ci.type == ResourceCreateInfo::External) {
        assert(e.has<vk::Image>() || e.has<vk::Buffer>());
        return e.has<vk::Image>() ? ResourceCreateInfo::Image : ResourceCreateInfo::Buffer;
    } else {
        return ci.type;
    }
}
static Entity findPoolResource(ResourceCreateInfo const &ci, RenderGraphImpl &renderGraph) {
    for(auto poolRes : renderGraph.resourcePool) {
        assert(poolRes.has<ResourceCreateInfo>());
        auto type = getResourceType(poolRes);

        if(type != ci.type)
            continue;

        auto const &poolCi = poolRes.get<ResourceCreateInfo>();

        if(ci.type == ResourceCreateInfo::Image && !isCompatible(ci.image, poolCi.image)) {
            continue;
        }

        renderGraph.resourcePool.erase(poolRes);
        return poolRes;
    }

    return Entity();
}
static void aliasResources(RenderGraphImpl &renderGraph) {
    for(uint i = 0; i < renderGraph.passStack.size(); ++i) {
        auto const &pass = renderGraph.passes.at(renderGraph.passStack.at(i));

        // Allocate
        for(auto const &[resourceName, traits] : pass.writes) {
            auto &resource = renderGraph.resources.at(resourceName);
            auto &e = resource.eResource;
            auto ci = e.get<ResourceCreateInfo>();

            // TODO: external resources are not used before their lifetimes
            if(ci.type == ResourceCreateInfo::External)
                continue;

            e.destroy();
            e = findPoolResource(ci, renderGraph);

            if(!e.valid()) {
                assert(renderGraph.reg && "Should have called RenderGraphBuilder::setAllocInfo!");
                e = renderGraph.reg->create(ci);
            }

            if(!e.has<ResourcePoolAllocation>()) {
                e.emplace<ResourcePoolAllocation>();
            }

            auto &allocInfo = e.get<ResourcePoolAllocation>();
            allocInfo.imageUsage |= traits.imageTraits.usage;
            allocInfo.bufferUsage |= traits.bufferTraits.usage;
            allocInfo.bufferSize = std::max<VkDeviceSize>(allocInfo.bufferSize, traits.bufferTraits.size + traits.bufferTraits.offset);
        }

        // Free
        for(auto const &[resourceName, _] : pass.reads) {
            auto &resource = renderGraph.resources.at(resourceName);
            auto const &ci = resource.eResource.get<ResourceCreateInfo>();

            // Might use external resources if the contents are not used next frame
            if(resource.eResource.has<PleaseKeepTheImageContents>())
                continue;

            if(resource.lifetimeEnd <= i)
                renderGraph.resourcePool.emplace(resource.eResource);
        }
    }
}
static void allocateResources(RenderGraphImpl &renderGraph) {
    if(!renderGraph.reg)
        return;
    for(auto &e : renderGraph.reg->view<ResourceCreateInfo, ResourcePoolAllocation>()) {
        auto const &ci = e.get<ResourceCreateInfo>();
        auto allocInfo = e.get<ResourcePoolAllocation>();
        e.remove<ResourcePoolAllocation>();

        assert(ci.type != ResourceCreateInfo::External);

        if(ci.type == ResourceCreateInfo::Image) {
            assert(allocInfo.imageUsage != 0);
            vk::ImageCreateInfo imageCreateInfo{
                .usage = allocInfo.imageUsage,
                .allocInfo = renderGraph.allocInfo,
                .image = ci.image.imageInfo,
            };
            if(e.has<Name>())
                imageCreateInfo.name = e.get<Name>().name;
            if(ci.image.resizeToSwapchain)
                e.emplace<ResizeToSwapchain>();

            e.emplace<vk::Image>(vk::makeImage(imageCreateInfo));
            e.emplace<RenderGraphResource>();
        } else {
            assert(allocInfo.bufferUsage != 0);
            assert(allocInfo.bufferSize != 0);
            vk::BufferCreateInfo bufferCreateInfo{
                .usage = allocInfo.bufferUsage,
                .allocInfo = renderGraph.allocInfo,
                .size = allocInfo.bufferSize,
                .map = false,
            };
            if(e.has<Name>())
                bufferCreateInfo.name = e.get<Name>().name;

            e.emplace<vk::Buffer>(vk::makeBuffer(bufferCreateInfo));
        }
    }
}
static bool buildBarriers(RenderGraphImpl &renderGraph) {
    std::unordered_map<Entity, Barrier::Scope> lastScope;
    for(auto const &passName : renderGraph.passStack) {
        auto const &pass = renderGraph.passes.at(passName);
        for(auto const &[resourceName, traits] : pass.reads) {
            auto const &resource = renderGraph.resources.at(resourceName);

            uint32_t queue = VK_QUEUE_FAMILY_IGNORED;
            if(!renderGraph.queueFamilies.indices.contains(pass.queue)) {
                LOG_ERROR("Pass {} needs queue {} which is not in queue families provided to the frame graph!", pass.name, string_VkQueueFlagBits(pass.queue));
                return false;
            } else {
                queue = renderGraph.queueFamilies.indices.at(pass.queue);
            }
            lastScope[resource.eResource] = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,

            };
        }
    }

    std::unordered_map<Entity, Barrier::Scope> resourceState;
    for(auto const &passName : renderGraph.passStack) {
        auto &pass = renderGraph.passes.at(passName);
        pass.barriers.reserve(pass.reads.size() + pass.writes.size());
        uint32_t queue = renderGraph.queueFamilies.indices.at(pass.queue);

        for(auto const &[resourceName, traits] : pass.reads) {
            auto const &resource = renderGraph.resources.at(resourceName);
            bool history = resource.eResource.has<PleaseKeepTheImageContents>() && !resourceState.contains(resource.eResource);
            auto &state = resourceState[resource.eResource];
            auto prevState = history ? lastScope.at(resource.eResource) : state;

            state = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,

            };
            pass.barriers.emplace_back(Barrier{
                .resource = resource.eResource,
                .src = prevState,
                .dst = state,
                .subresourceRange = traits.imageTraits.subresourceRange,
                .offset = traits.bufferTraits.offset,
                .size = traits.bufferTraits.size,
            });
        }
        for(auto const &[resourceName, traits] : pass.writes)
        {
            auto const &resource = renderGraph.resources.at(resourceName);
            auto &state = resourceState[resource.eResource];
            auto prevState = state;
            
            state = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,
            };
            pass.barriers.emplace_back(Barrier{
                .resource = resource.eResource,
                .src = prevState,
                .dst = state,
                .subresourceRange = traits.imageTraits.subresourceRange,
                .offset = traits.bufferTraits.offset,
                .size = traits.bufferTraits.size,
            });
        }
    }

    return true;
}

RenderGraphResult buildRenderGraph(RenderGraphBuilder &&builder) {
    RenderGraphImpl renderGraph;

    if(!setupRenderGraph(std::move(builder), renderGraph)) {
        LOG_ERROR("Failed to set up render graph!");
        return RenderGraphResult(nullptr);
    }

    // TODO: validate

    for(auto const &[_, pass] : renderGraph.passes) {
        if(pass.reads.empty()) {
            if(!processPass(pass.name, renderGraph, renderGraph.passes.size() * 4)) {
                LOG_ERROR("Failed to order render graph!");
                return RenderGraphResult(nullptr);
            }
        }
    }

    LOG_TRACE("Pass stack:");
    for(uint32_t i = 0; i < renderGraph.passStack.size(); ++i)
    {
        auto passName = renderGraph.passStack[i];
        auto const &pass = renderGraph.passes.at(passName);
        LOG_TRACE("{:>2}) {:>15}: {}", i, passName, printDependencies(pass));
    }

    calculateLifetimes(renderGraph);
    aliasResources(renderGraph);
    allocateResources(renderGraph);

    if(!buildBarriers(renderGraph)) {
        LOG_ERROR("Failed to build barriers!");
        return RenderGraphResult(nullptr);
    }

    RenderGraphResultImpl *res = new RenderGraphResultImpl{
        .resources = renderGraph.resources,
        .passStack = renderGraph.passStack,
    };

    for(auto &&[name, pass] : renderGraph.passes) {
        res->passes[name] = {
            .name = std::move(pass.name),
            .reads = std::move(pass.reads),
            .writes = std::move(pass.writes),
            .barriers = std::move(pass.barriers),
            .queue = std::move(pass.queue),
            .storage = std::move(pass.storage),
        };
    }

    return RenderGraphResult(res);
}